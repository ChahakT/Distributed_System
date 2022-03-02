#define FUSE_USE_VERSION 35

#include <fuse.h>
#include <sys/types.h>

#include <iostream>
#include <memory>

#include "client_grpc.cpp"

class PrivateData {
   public:
    GRPCClient client;
    explicit PrivateData(const std::shared_ptr<Channel>& channel)
        : client(channel) {}
};

static int do_getattr(const char* path, struct stat* st) {
    return GET_PDATA->client.c_getattr(path, st);
}

static int do_readdir(const char* path, void* buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info* fi) {
    return GET_PDATA->client.c_readdir(path, buffer, filler, offset, fi);
}

static int do_open(const char* path, struct fuse_file_info* fi) {
    return GET_PDATA->client.c_open(path, fi);
}

static int do_read(const char* path, char* buffer, size_t size, off_t offset,
                   struct fuse_file_info* fi) {
    printf("[read] %s\n", path);
    ssize_t ret;
    RET_ERR(ret = pread(fi->fh, buffer, size, offset));
    return ret;
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
    operations.open = do_open;
    operations.read = do_read;

    return fuse_main(argc, argv, &operations, &private_data);
}