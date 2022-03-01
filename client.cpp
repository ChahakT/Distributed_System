#define FUSE_USE_VERSION 35

#include <fuse.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <cassert>
#include <iomanip>
#include <iostream>
#include <memory>

#include "client_grpc.cpp"

#define GET_PDATA static_cast<PrivateData*>(fuse_get_context()->private_data)

class PrivateData {
   public:
    GRPCClient client;
    PrivateData(std::shared_ptr<Channel> channel) : client(channel) {}
};

static int do_getattr(const char* path, struct stat* st) {
    return GET_PDATA->client.c_getattr(path, st);
}

static int do_readdir(const char* path, void* buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info* fi) {
    return GET_PDATA->client.c_readdir(path, buffer, filler, offset, fi);
}

static struct fuse_operations operations;

int main(int argc, char* argv[]) {
    const std::string target_str = "0.0.0.0:50051";
    grpc::ChannelArguments ch_args;

    ch_args.SetMaxReceiveMessageSize(INT_MAX);
    ch_args.SetMaxSendMessageSize(INT_MAX);

    PrivateData private_data = PrivateData(grpc::CreateCustomChannel(
        target_str, grpc::InsecureChannelCredentials(), ch_args));

    operations.getattr = do_getattr;
    operations.readdir = do_readdir;

    return fuse_main(argc, argv, &operations, &private_data);
}