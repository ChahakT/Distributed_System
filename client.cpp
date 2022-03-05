#define FUSE_USE_VERSION 35

#include <fuse.h>
#include <sys/types.h>

#include <iostream>
#include <memory>

#include "client_grpc.cpp"

#define BIND_OPERATION(op) \
    operations.op = [](auto... args) { return client->c_##op(args...); }

class PrivateData {
   public:
    std::string target_str;
    std::string cache_path;
};

static std::unique_ptr<GRPCClient> client;

static void* do_init(struct fuse_conn_info* conn) {
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(INT_MAX);
    ch_args.SetMaxSendMessageSize(INT_MAX);
    client = std::make_unique<GRPCClient>(
        grpc::CreateCustomChannel(GET_PDATA->target_str,
                                  grpc::InsecureChannelCredentials(), ch_args),
        GET_PDATA->cache_path);
    return nullptr;
}

static struct fuse_operations operations;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("%s [server addr:port] [cache path] [fuse options]\n", argv[0]);
        return 1;
    }

    PrivateData private_data;
    private_data.target_str = argv[1];
    private_data.cache_path = argv[2];

    operations.init = do_init;
    BIND_OPERATION(getattr);
    BIND_OPERATION(readdir);
    BIND_OPERATION(open);
    BIND_OPERATION(read);
    BIND_OPERATION(mkdir);
    BIND_OPERATION(rmdir);
    BIND_OPERATION(write);
    BIND_OPERATION(create);
    BIND_OPERATION(flush);
    BIND_OPERATION(unlink);
    BIND_OPERATION(rename);
    BIND_OPERATION(fsync);
    BIND_OPERATION(release);

    return fuse_main(argc - 2, argv + 2, &operations, &private_data);
}
