#include <fcntl.h>
#include <grpcpp/grpcpp.h>
#include <signal.h>
#include <sys/stat.h>

#include <iostream>
#include <memory>
#include <string>

#include "hello.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using tutorial::ClientRequest;
using tutorial::gRPCService;
using tutorial::ServerResponse;

class GRPCClient {
   public:
    GRPCClient(std::shared_ptr<Channel> channel)
        : stub_(gRPCService::NewStub(channel)) {}

    std::string c_create(const std::string& path, int flag) {
        ClientRequest request;
        request.set_path(path);
        request.set_flag(flag);
        ServerResponse reply;
        ClientContext context;
        Status status = stub_->creat(&context, request, &reply);
        if (status.ok()) {
            return std::string(reply.fd());
        } else {
            std::cout << status.error_code() << ": " << status.error_message()
                      << std::endl;
            return "RPC failed";
        }
    }

   private:
    std::unique_ptr<gRPCService::Stub> stub_;
};

int main(int argc, char* argv[]) {
    // "ctrl-C handler"
    //    signal(SIGINT, sigintHandler);
    const std::string target_str = "0.0.0.0:50051";
    grpc::ChannelArguments ch_args;

    ch_args.SetMaxReceiveMessageSize(INT_MAX);
    ch_args.SetMaxSendMessageSize(INT_MAX);

    GRPCClient gRPCClient(grpc::CreateCustomChannel(
        target_str, grpc::InsecureChannelCredentials(), ch_args));

    std::string reply = gRPCClient.c_create("/users/Chahak/Fs/files/a.txt",
                                            O_WRONLY | O_CREAT | O_TRUNC);
    std::cout << reply << std::endl;
    return 0;
}