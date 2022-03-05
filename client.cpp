#define FUSE_USE_VERSION 35

#include <fuse.h>
#include <sys/types.h>

#include <iostream>
#include <memory>

#include "client_grpc.cpp"

class PrivateData {
   public:
    std::string target_str;
    std::string cache_path;
};

static std::unique_ptr<GRPCClient> client;

static int do_getattr(const char* path, struct stat* st) {
    return client->c_getattr(path, st);
}

static int do_readdir(const char* path, void* buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info* fi) {
    return client->c_readdir(path, buffer, filler, offset, fi);
}

static int do_open(const char* path, struct fuse_file_info* fi) {
    return client->c_open(path, fi);
}

static int do_read(const char* path, char* buffer, size_t size, off_t offset,
                   struct fuse_file_info* fi) {
    printf("[read] %s\n", path);
    ssize_t ret;
    RET_ERR(ret = pread(fi->fh, buffer, size, offset));
    return ret;
}

static int do_write(const char* path, const char* buffer, size_t size,
                    off_t offset, struct fuse_file_info* fi) {
    return client->c_write(path, buffer, size, offset, fi);
}

static int do_creat(const char* path, mode_t mode, struct fuse_file_info* fi) {
    return client->c_creat(path, mode, fi);
}

static int do_mkdir(const char* path, mode_t mode) {
    return client->c_mkdir(path, mode);
}

static int do_rmdir(const char* path) { return client->c_rmdir(path); }

static int do_flush(const char* path, struct fuse_file_info* fi) {
    return client->c_flush(path, fi);
}

static int do_fsync(const char* path, int datasync, struct fuse_file_info* fi) {
    return client->c_fsync(path, datasync, fi);
}

static int do_unlink(const char* path) { return client->c_unlink(path); }

static int do_rename(const char* oldpath, const char* newpath) {
    return client->c_rename(oldpath, newpath);
}

static int do_release(const char* path, struct fuse_file_info* fi) {
    return client->c_release(path, fi);
}

static void* do_init(struct fuse_conn_info* conn) {
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(INT_MAX);
    ch_args.SetMaxSendMessageSize(INT_MAX);
    client = std::make_unique<GRPCClient>(
        grpc::CreateCustomChannel(GET_PDATA->target_str,
                                  grpc::InsecureChannelCredentials(), ch_args),
        GET_PDATA->cache_path);
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
    operations.getattr = do_getattr;
    operations.readdir = do_readdir;
    operations.open = do_open;
    operations.read = do_read;
    operations.mkdir = do_mkdir;
    operations.rmdir = do_rmdir;
    operations.write = do_write;
    operations.create = do_creat;
    operations.flush = do_flush;
    operations.unlink = do_unlink;
    operations.rename = do_rename;
    operations.fsync = do_fsync;
    operations.release = do_release;

    return fuse_main(argc - 2, argv + 2, &operations, &private_data);
}
