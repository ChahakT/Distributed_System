#include <dirent.h>
#include <fcntl.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>

#include "includes/hello.grpc.pb.h"

using aafs::gRPCService;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

#define SERVER_FOLDER "./server_folder/"

class gRPCServiceImpl final : public gRPCService::Service {
   private:
    static std::string to_server_path(const std::string &path) {
        return (SERVER_FOLDER + path);
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
                      ServerWriter<aafs::FileContent> *writer) override {
        int fd = open(to_server_path(req->path()).c_str(), O_RDONLY);
        if (fd == -1) {
            return Status::CANCELLED;
        }
        aafs::FileContent dl;
        ssize_t n;
        char buf[4096];
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            dl.set_data(buf, n);
            writer->Write(dl);
        }
        return Status::OK;
    }

    Status s_unlink(ServerContext *context, const aafs::PathRequest *req,
                    aafs::StatusResponse *reply) override{
                        
        int ret = unlink(to_server_path(req->path()).c_str());
        reply->set_ret(ret);
        return Status::OK;
    }

    Status s_creat(ServerContext *context, const aafs::PathRequest *req,
                    aafs::StatusResponse *reply) override{
        int ret = creat(to_server_path(req->path()).c_str(), 0777);
        reply->set_ret(ret);
        return Status::OK;
    }

    Status s_mkdir(ServerContext *context, const aafs::PathRequest *req,
                     aafs::StatusResponse *reply) override {
	    int ret = mkdir(to_server_path(req->path()).c_str(), 0777);
        reply->set_ret(ret);    
	    return Status::OK;
    }

    Status s_rmdir(ServerContext *context, const aafs::PathRequest *req,
                     aafs::StatusResponse *reply) override {
        int ret = rmdir(to_server_path(req->path()).c_str());
        reply->set_ret(ret);
        return Status::OK;
    }
};

int main() {
    std::string server_address("localhost:50051");
    gRPCServiceImpl service;
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
