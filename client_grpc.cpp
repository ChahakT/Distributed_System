#include <fcntl.h>
#include <fuse.h>
#include <grpcpp/grpcpp.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <iomanip>

#include "includes/hello.grpc.pb.h"
#include "includes/hello.pb.h"

using aafs::gRPCService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#define RET_ERR(stmt)                                                       \
    if ((stmt) < 0) {                                                       \
        fprintf(stderr, "Error: line %d, %s\n", __LINE__, strerror(errno)); \
        return -errno;                                                      \
    }
#define GET_PDATA static_cast<PrivateData *>(fuse_get_context()->private_data)

#define CLIENT_CACHE_FOLDER "./aafs_client_cache/"
#define CLIENT_TRANSFER_TEMPLATE "./aafs_transferXXXXXX"

class GRPCClient {
   private:
    static std::string hash_str(const char *src) {
        auto digest = std::make_unique<unsigned char[]>(SHA256_DIGEST_LENGTH);
        SHA256(reinterpret_cast<const unsigned char *>(src), strlen(src),
               digest.get());
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(digest[i]);
        }
        return ss.str();
    }

    static std::string to_cache_path(const std::string &path) {
        return (CLIENT_CACHE_FOLDER + hash_str(path.c_str()));
    }

    static std::pair<int, std::string> get_tmp_file() {
        char tmpla[] = CLIENT_TRANSFER_TEMPLATE;
        int ret = mkstemp(tmpla);
        return std::make_pair(ret, tmpla);
    }

   public:
    explicit GRPCClient(const std::shared_ptr<Channel> &channel)
        : stub_(gRPCService::NewStub(channel)) {
        int ret = mkdir(CLIENT_CACHE_FOLDER, 0755);
        if (ret != 0 && errno != EEXIST) {
            assert_perror(errno);
        }
    }

    int c_getattr(const char *path, struct stat *st) {
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

    int c_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi) {
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
        for (auto &s : reply.entries()) {
            filler(buffer, s.c_str(), nullptr, 0);
        }
        return 0;
    }

    // TODO: Maybe we need to deal with server file change between lstat and
    // download ? No its getting covered with file not in cache case.
    int c_open(const char *path, struct fuse_file_info *fi) {
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
        std::unique_ptr<grpc::ClientReader<aafs::FileContent>> reader(
            stub_->s_download(&context, request));
        aafs::FileContent content;
        auto [tmp_fd, tmp_name] = get_tmp_file();
        while (reader->Read(&content)) {
            auto ret =
                write(tmp_fd, content.data().c_str(), content.data().size());
            if (ret < 0) {
                close(tmp_fd);
                return -errno;
            }
        }

        auto status = reader->Finish();
        if (!status.ok()) {
            unlink(tmp_name.c_str());
            return -ENOENT;
        }

        struct utimbuf times {};
        times.actime = server_st.st_atime;
        times.modtime = server_st.st_mtime;
        RET_ERR(utime(tmp_name.c_str(), &times));
        RET_ERR(rename(tmp_name.c_str(), cache_path.c_str()));
        fi->fh = tmp_fd;
        return 0;
    }

    int c_creat(const char *path, mode_t mode, struct fuse_file_info *fi) {
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
        auto [tmp_fd, tmp_name] = get_tmp_file();
        RET_ERR(rename(tmp_name.c_str(), cache_path.c_str()));
        fi->fh = tmp_fd;
        return tmp_fd;
    }

    int c_write(const char *path, const char *buffer, size_t size,
                off_t offset, struct fuse_file_info *fi) {
        auto cache_path = to_cache_path(path);
        auto [tmp_fd, tmp_name] = get_tmp_file();
        auto fd1 = fi->fh;
        char buf[4096];
        memset(buf, 0, sizeof(buf));
        ssize_t n;
        while ((n = read(fd1, buf, sizeof(buf))) > 0) {
            write(tmp_fd, buf, n);
        }
        size_t ret;
        RET_ERR(ret = pwrite(tmp_fd, buffer, size, offset));
        RET_ERR(rename(tmp_name.c_str(), cache_path.c_str()));
        return ret;
    }

//    int c_flush(const char* path) {
//        printf("[flush] %s\n", path);
//    }
//
    int c_unlink(const char* path) {
        printf("[unlink] %s\n", path);

        aafs::PathRequest request;
        request.set_path(path);

        aafs::StatusResponse reply;
	    ClientContext context;

        //Status status = stub_->s_unlink(&context, request, &reply);
        Status status = stub_->s_mkdir(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }

        //to do, what to do for the file in cache.
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

    int c_rmdir(const char* path){
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

   private:
    std::unique_ptr<gRPCService::Stub> stub_;
};
