#include <dirent.h>
#include <fcntl.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>
#include <utime.h>

#include <filesystem>

#include "includes/hello.grpc.pb.h"

using aafs::gRPCService;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

constexpr char kServerTempFolder[] = "./aafs_server_temp/";
constexpr char kServerTransferTemplate[] =
    "./aafs_server_temp/aafs_serverXXXXXX";

class gRPCServiceImpl final : public gRPCService::Service {
   public:
    explicit gRPCServiceImpl(const std::string server_folder)
        : kServerFolder(std::move(server_folder)) {}

   private:
    const std::string kServerFolder;

    std::string to_server_path(const std::string &path) {
        return kServerFolder + path;
    }

    static std::pair<int, std::string> get_tmp_file() {
        char transfer_template[sizeof(kServerTransferTemplate)];
        memcpy(transfer_template, kServerTransferTemplate,
               sizeof(transfer_template));
        int ret = mkstemp(transfer_template);
        chmod(transfer_template, 0777);
        return std::make_pair(ret, transfer_template);
    }

    Status s_getattr(ServerContext *context, const aafs::PathRequest *req,
                     aafs::GetAttrResponse *reply) override {
        struct stat st {};
        int ret = lstat(to_server_path(req->path()).c_str(), &st);
        if (ret == -1) {
            reply->set_ret(-errno);
            return Status::OK;
        }
        reply->set_ret(ret);

        auto stat = std::make_unique<aafs::Stat>();
        stat->set_ino(st.st_ino);
        stat->set_mode(st.st_mode);
        stat->set_nlink(st.st_nlink);
        stat->set_uid(st.st_uid);
        stat->set_gid(st.st_gid);
        stat->set_rdev(st.st_rdev);
        stat->set_size(st.st_size);
        stat->set_blocks(st.st_blocks);
        stat->set_atime(st.st_atime);
        stat->set_mtime(st.st_mtime);

        reply->set_allocated_stat(stat.release());
        return Status::OK;
    }

    Status s_readdir(ServerContext *context, const aafs::PathRequest *req,
                     aafs::ReadDirResponse *reply) override {
        DIR *dp;
        struct dirent *de;
        dp = opendir(to_server_path(req->path()).c_str());
        if (dp == nullptr) {
            reply->set_ret(-errno);
            return Status::OK;
        }
        while ((de = readdir(dp)) != nullptr) {
            reply->add_entries(de->d_name);
        }
        closedir(dp);
        return Status::OK;
    }

    Status s_download(ServerContext *context, const aafs::PathRequest *req,
                      ServerWriter<aafs::DownloadResponse> *writer) override {
        int fd = open(to_server_path(req->path()).c_str(), O_RDONLY);
        if (fd == -1) {
            return Status::CANCELLED;
        }
        struct stat st {};
        fstat(fd, &st);
        aafs::DownloadResponse reply;

        auto matime = std::make_unique<aafs::MATime>();
        matime->set_atime(st.st_atime);
        matime->set_mtime(st.st_mtime);
        reply.set_allocated_time(matime.release());
        writer->Write(reply);

        constexpr int buf_size = 4096;
        auto buf = std::make_unique<std::string>(buf_size, '\0');
        ssize_t n;
        while ((n = read(fd, buf->data(), sizeof(buf))) > 0) {
            buf->resize(n);
            reply.set_allocated_data(buf.release());
            writer->Write(reply);
            buf = std::make_unique<std::string>(buf_size, '\0');
        }
        fsync(fd);
        close(fd);
        return Status::OK;
    }

    Status s_upload(ServerContext *context,
                    ServerReader<aafs::UploadRequest> *reader,
                    aafs::StatusResponse *reply) override {
        aafs::UploadRequest req;

        if (!reader->Read(&req)) return Status::CANCELLED;
        if (!req.has_meta()) return Status::CANCELLED;
        std::string path = req.meta().path();
        int atime = req.meta().atime();
        int mtime = req.meta().mtime();

        auto [tmp_fd, tmp_name] = get_tmp_file();
        if (tmp_fd < 0) {
            reply->set_ret(-errno);
            return Status::OK;
        }

        while (reader->Read(&req)) {
            if (!req.has_data()) {
                close(tmp_fd);
                return Status::CANCELLED;
            }
            int ret = write(tmp_fd, req.data().c_str(), req.data().size());
            if (ret < 0) {
                reply->set_ret(-errno);
                close(tmp_fd);
                return Status::OK;
            }
        }

        fsync(tmp_fd);
        close(tmp_fd);
        struct utimbuf times {};
        times.actime = atime;
        times.modtime = mtime;
        utime(tmp_name.c_str(), &times);
        rename(tmp_name.c_str(), to_server_path(path).c_str());

        reply->set_ret(0);
        return Status::OK;
    }

    Status s_unlink(ServerContext *context, const aafs::PathRequest *req,
                    aafs::StatusResponse *reply) override {
        int ret = unlink(to_server_path(req->path()).c_str());
        reply->set_ret(ret);
        return Status::OK;
    }

    Status s_creat(ServerContext *context, const aafs::PathRequest *req,
                   aafs::StatusResponse *reply) override {
        int fd = creat(to_server_path(req->path()).c_str(), 0777);
        if (fd == -1) {
            reply->set_ret(-errno);
            return Status::OK;
        }
        close(fd);
        return Status::OK;
    }

    Status s_mkdir(ServerContext *context, const aafs::PathRequest *req,
                   aafs::StatusResponse *reply) override {
        int ret = mkdir(to_server_path(req->path()).c_str(), 0777);
        if (ret == -1) {
            reply->set_ret(-errno);
            return Status::OK;
        }
        reply->set_ret(ret);
        return Status::OK;
    }

    Status s_rmdir(ServerContext *context, const aafs::PathRequest *req,
                   aafs::StatusResponse *reply) override {
        int ret = rmdir(to_server_path(req->path()).c_str());
        if (ret == -1) {
            reply->set_ret(-errno);
            return Status::OK;
        }
        reply->set_ret(ret);
        return Status::OK;
    }

    Status s_rename(ServerContext *context, const aafs::RenameRequest *req,
                    aafs::StatusResponse *reply) override {
        int ret = rename(to_server_path(req->oldpath()).c_str(),
                         to_server_path(req->newpath()).c_str());
        reply->set_ret(ret);
        return Status::OK;
    }
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("%s [server folder]\n", argv[0]);
        return 1;
    }

    int ret = mkdir(kServerTempFolder, 0755);
    if (ret != 0) {
        if (errno == EEXIST) {  // delete all dirty files
            for (const auto &entry :
                 std::filesystem::directory_iterator(kServerTempFolder))
                std::filesystem::remove_all(entry.path());
        } else {
            assert_perror(errno);
        }
    }
    std::string server_address("localhost:50051");
    gRPCServiceImpl service(argv[1]);
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.SetMaxSendMessageSize(INT_MAX);
    builder.SetMaxReceiveMessageSize(INT_MAX);
    builder.SetMaxMessageSize(INT_MAX);

    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}
