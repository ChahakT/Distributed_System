#include <fcntl.h>
#include <fuse.h>
#include <grpcpp/grpcpp.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <set>
#include <string>

#include "includes/hello.grpc.pb.h"
#include "includes/hello.pb.h"

using aafs::gRPCService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

#define RET_ERR(stmt)                                                       \
    if ((stmt) < 0) {                                                       \
        fprintf(stderr, "Error: line %d, %s\n", __LINE__, strerror(errno)); \
        return -errno;                                                      \
    }
#define GET_PDATA static_cast<PrivateData*>(fuse_get_context()->private_data)

constexpr char kClientCacheFolder[] = "/aafs_client_cache/";
constexpr char kClientTempFolder[] = "/aafs_client_temp/";
constexpr char kClientTransferTemplate[] =
    "/aafs_client_temp/aafs_transferXXXXXX";

class GRPCClient {
   public:
    explicit GRPCClient(const std::shared_ptr<Channel>& channel,
                        const std::string cache_path)
        : stub_(gRPCService::NewStub(channel)),
          kCachePath(std::move(cache_path)) {
        int ret = mkdir((kCachePath + kClientCacheFolder).c_str(), 0755);
        if (ret != 0 && errno != EEXIST) {
            assert_perror(errno);
        }
        ret = mkdir((kCachePath + kClientTempFolder).c_str(), 0755);
        if (ret != 0) {
            if (errno == EEXIST) {  // delete all dirty files
                for (const auto& entry : std::filesystem::directory_iterator(
                         kCachePath + kClientTempFolder))
                    std::filesystem::remove_all(entry.path());
            } else {
                assert_perror(errno);
            }
        }
    }

    int c_getattr(const char* path, struct stat* st) {
        // printf("[getattr] %s\n", path);
        aafs::PathRequest request;
        request.set_path(path);
        aafs::GetAttrResponse reply;
        ClientContext context;
        Status status = stub_->s_getattr(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        if (reply.ret() < 0) {
            return reply.ret();
        }
        st->st_ino = reply.stat().ino();
        st->st_mode = reply.stat().mode();
        st->st_nlink = reply.stat().nlink();
        st->st_uid = reply.stat().uid();
        st->st_gid = reply.stat().gid();
        st->st_rdev = reply.stat().rdev();
        st->st_size = reply.stat().size();
        st->st_blocks = reply.stat().blocks();
        st->st_atime = reply.stat().atime();
        st->st_mtime = reply.stat().mtime();
        st->st_ctime = reply.stat().ctime();
        return 0;
    }

    int c_readdir(const char* path, void* buffer, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info* fi) {
        printf("[readdir] %s\n", path);
        aafs::PathRequest request;
        request.set_path(path);
        aafs::ReadDirResponse reply;
        ClientContext context;
        Status status = stub_->s_readdir(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        if (reply.ret() < 0) {
            return reply.ret();
        }
        for (auto& s : reply.entries()) {
            filler(buffer, s.c_str(), nullptr, 0);
        }
        return 0;
    }

    int c_open(const char* path, struct fuse_file_info* fi) {
        printf("[open] %s\n", path);
        struct stat client_st {};
        auto cache_path = to_cache_path(path);
        int sret = lstat(cache_path.c_str(), &client_st);

        // Non ENOENT error
        if (sret == -1 && errno != ENOENT) {
            return -errno;
        }

        struct stat server_st {};
        if (sret == 0) {
            int ret = c_getattr(path, &server_st);
            if (ret == -ENOENT) {
                // File deleted on server
                unlink(cache_path.c_str());
                return -ENOENT;
            } else if (ret < 0) {
                // Other errors
                return ret;
            }
            // Cache file up-to-date
            if (client_st.st_mtime >= server_st.st_mtime) {
                int fd = open(cache_path.c_str(), O_RDWR);
                RET_ERR(fd);
                fi->fh = fd;
                return 0;
            }
        }

        ClientContext context;
        aafs::PathRequest request;
        request.set_path(path);
        printf("[download] %s\n", path);
        std::unique_ptr<grpc::ClientReader<aafs::DownloadResponse>> reader(
            stub_->s_download(&context, request));
        aafs::DownloadResponse content;
        int atime, mtime;
        reader->Read(&content);
        if (content.has_time()) {
            atime = content.time().atime();
            mtime = content.time().mtime();
        } else {
            return -ENONET;
        }

        auto [tmp_fd, tmp_name] = get_tmp_file();
        RET_ERR(tmp_fd);
        while (reader->Read(&content)) {
            if (!content.has_data()) {
                close(tmp_fd);
                return -ENONET;
            }
            int ret =
                write(tmp_fd, content.data().c_str(), content.data().size());
            if (ret < 0) {
                close(tmp_fd);
                unlink(tmp_name.c_str());
                return -errno;
            }
        }

        auto status = reader->Finish();
        if (!status.ok()) {
            unlink(tmp_name.c_str());
            return -ENOENT;
        }

        struct utimbuf times {};
        times.actime = atime;
        times.modtime = mtime;
        RET_ERR(utime(tmp_name.c_str(), &times));
        fsync(tmp_fd);
        RET_ERR(rename(tmp_name.c_str(), cache_path.c_str()));
        fi->fh = tmp_fd;
        return 0;
    }

    int c_read(const char* path, char* buf, size_t size, off_t offset,
               struct fuse_file_info* fi) {
        printf("[read] %s\n", path);
        ssize_t ret;
        RET_ERR(ret = pread(fi->fh, buf, size, offset));
        return ret;
    }

    int c_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
        printf("[creat] %s\n", path);
        auto cache_path = to_cache_path(path);
        aafs::PathRequest request;
        request.set_path(path);
        aafs::StatusResponse reply;
        ClientContext context;
        Status status = stub_->s_creat(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        if (reply.ret() < 0) {
            return reply.ret();
        }
        auto [tmp_fd, tmp_name] = get_tmp_file();
        rename(tmp_name.c_str(), cache_path.c_str());
        fi->fh = tmp_fd;
        return 0;
    }

    int c_write(const char* path, const char* buffer, size_t size, off_t offset,
                struct fuse_file_info* fi) {
        printf("[write] %s\n", path);
        auto write_path = to_write_cache_path(path, fi->fh);
        // copy-on-write
        if (access(write_path.c_str(), F_OK) != 0) {
            printf("[write] copy-on-write!\n");
            int fd1 = fi->fh;
            int fd2 = open(write_path.c_str(), O_RDWR | O_CREAT, 0644);

            lseek(fd1, 0, SEEK_SET);
            // Ref:
            // https://stackoverflow.com/questions/52766388/how-can-i-use-the-copy-on-write-of-a-btrfs-from-c-code
            if (ioctl(fd2, FICLONE, fd1) < 0) {
                perror("ioctl");
            }

            dup2(fd2, fd1);
            close(fd2);
            dirty_fds.insert(fd1);
        }
        int fd = fi->fh;
        size_t ret;
        RET_ERR(ret = pwrite(fd, buffer, size, offset));
        return ret;
    }

    int c_flush(const char* path, struct fuse_file_info* fi) {
        printf("[flush] %s\n", path);

        if (dirty_fds.count(fi->fh) == 0) {
            return 0;
        }

        aafs::UploadRequest req;
        auto meta = std::make_unique<aafs::UploadMeta>();
        aafs::StatusResponse reply;
        ClientContext context;

        std::unique_ptr<ClientWriter<aafs::UploadRequest>> writer(
            stub_->s_upload(&context, &reply));

        fsync(fi->fh);
        struct stat st;
        fstat(fi->fh, &st);

        meta->set_path(path);
        meta->set_atime(st.st_atime);
        meta->set_mtime(st.st_mtime);
        req.set_allocated_meta(meta.release());
        if (!writer->Write(req)) {
            return -ENONET;
        }

        constexpr int buf_size = 409600;
        auto buf = std::make_unique<std::string>(buf_size, '\0');
        ssize_t n;
        lseek(fi->fh, 0, SEEK_SET);
        while ((n = read(fi->fh, buf->data(), buf_size)) > 0) {
            buf->resize(n);
            req.set_allocated_data(buf.release());
            if (!writer->Write(req)) {
                return -ENONET;
            }

            buf = std::make_unique<std::string>(buf_size, '\0');
        }

        writer->WritesDone();
        Status status = writer->Finish();
        if (!status.ok()) {
            return -ENONET;
        }
        if (rename(to_write_cache_path(path, fi->fh).c_str(),
                   to_cache_path(path).c_str()) == 0) {
            dirty_fds.erase(fi->fh);
        }
        return 0;
    }

    int c_fsync(const char* path, int, struct fuse_file_info* fi) {
        return c_flush(path, fi);
    }

    int c_unlink(const char* path) {
        printf("[unlink] %s\n", path);

        aafs::PathRequest request;
        request.set_path(path);

        aafs::StatusResponse reply;
        ClientContext context;

        Status status = stub_->s_unlink(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }

        unlink(to_cache_path(path).c_str());

        return reply.ret();
    }

    int c_mkdir(const char* path, mode_t mode) {
        printf("[mkdir] %s\n", path);
        aafs::PathRequest request;
        request.set_path(path);
        aafs::StatusResponse reply;
        ClientContext context;
        Status status = stub_->s_mkdir(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        // TODO: put in cache/ cache check?
        return reply.ret();
    }

    int c_rmdir(const char* path) {
        printf("[rmdir] %s\n", path);
        aafs::PathRequest request;
        request.set_path(path);
        aafs::StatusResponse reply;
        ClientContext context;
        Status status = stub_->s_rmdir(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        // TODO: remove from cache
        return reply.ret();
    }

    int c_rename(const char* oldpath, const char* newpath) {
        printf("[rename] %s %s\n", oldpath, newpath);

        const std::string cached_oldpath = to_cache_path(oldpath);
        const std::string cached_newpath = to_cache_path(newpath);
        const std::string tmp_path = cached_oldpath + ".tmp";

        int ret = rename(cached_oldpath.c_str(), tmp_path.c_str());
        if (ret < 0) return ret;

        aafs::RenameRequest request;
        request.set_oldpath(oldpath);
        request.set_newpath(newpath);
        aafs::StatusResponse reply;
        ClientContext context;
        Status status = stub_->s_rename(&context, request, &reply);

        if (!status.ok()) {
            unlink(tmp_path.c_str());
            return -ENONET;
        } else {
            if (reply.ret() == 0) {
                rename(tmp_path.c_str(), cached_newpath.c_str());
            } else {
                unlink(tmp_path.c_str());
            }
            return reply.ret();
        }
    }

    int c_release(const char* path, struct fuse_file_info* fi) {
        printf("[release] %s\n", path);

        return close(fi->fh);
    }

   private:
    static std::string hash_str(const char* src) {
        auto digest = std::make_unique<unsigned char[]>(SHA256_DIGEST_LENGTH);
        SHA256(reinterpret_cast<const unsigned char*>(src), strlen(src),
               digest.get());
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(digest[i]);
        }
        return ss.str();
    }

    std::string to_cache_path(const std::string& path) {
        return (kCachePath + kClientCacheFolder + hash_str(path.c_str()));
    }

    std::pair<int, std::string> get_tmp_file() {
        std::string transfer_template = kCachePath + kClientTransferTemplate;
        int ret = mkstemp(transfer_template.data());
        chmod(transfer_template.c_str(), 0777);
        return std::make_pair(ret, transfer_template);
    }

    std::string to_write_cache_path(const char* path, int fd) {
        return kCachePath + kClientTempFolder + hash_str(path) +
               std::to_string(fd) + ".dirty";
    }

    std::unique_ptr<gRPCService::Stub> stub_;
    std::unordered_set<int> dirty_fds;
    const std::string kCachePath;
};
