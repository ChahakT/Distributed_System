#include "include/server.h"
#include "hello.grpc.pb.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using tutorial::ClientRequest;
using tutorial::ServerResponse;
using tutorial::gRPCService;

class gRPCServiceImpl final : public gRPCService::Service {
//    Status open(ServerContext *context, const ClientRequest *req, ServerResponse *reply) override {
//        int fd = open(req->path(), req->flag());
//        reply->set_value(fd);
//        Stat* stat_reply;
//        stat(context, req, stat_reply);
//        reply->set_version(stat_reply->mtime());
//        return Status::OK;
//    }

    Status creat(ServerContext *context, const ClientRequest *req, ServerResponse *reply) override {
        std::cout << "Called creat " << req->path() << std::endl;
        int fd = ::open(req->path().c_str(), req->flag());
        std::cout << fd << std::endl;
        reply->set_fd(std::to_string(fd));
//        Stat* stat_reply;
//        stat(context, req, stat_reply);
//        reply->set_version(stat_reply->mtime());
        return Status::OK;
    }

//    Status stat(ServerContext *context, const ClientRequest *req, Stat *reply) override {
//        struct stat buf;
//        stat(req->path, &buf);
//        reply->set_file_size(buf.st_size);
//        reply->set_mtime(buf.st_mtimespec.tv_sec);
//        return Status::OK;
//    }

//    Status mkdir(ServerContext* context, const ClientRequest* req, Int* reply) override {
//        reply->set_value(::mkdir(req->path().c_str(), req->flag()));
//        set_time(reply->mutable_ts(), get_stat(req->path().c_str()).st_mtim);
//        return Status::OK;
//    }

//    Status rmdir(ServerContext* context, const PathNFlag* req, Int* reply) override {
//        reply->set_value(::rmdir(req->path().c_str()));
//        return Status::OK;
//    }
//
//    Status unlink() {
//
//    }
//
//    Status close() {
//
//    }
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