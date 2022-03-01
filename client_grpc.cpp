#include <fcntl.h>
#include <fuse.h>
#include <grpcpp/grpcpp.h>
#include <openssl/sha.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <cassert>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "includes/hello.grpc.pb.h"
#include "includes/hello.pb.h"

using afs::gRPCService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class GRPCClient {
   public:
    GRPCClient(std::shared_ptr<Channel> channel)
        : stub_(gRPCService::NewStub(channel)) {}

    int c_getattr(const char *path, struct stat *st) {
        printf("[getattr] %s\n", path);
        afs::PathRequest request;
        request.set_path(path);
        afs::GetAttrResponse reply;
        ClientContext context;
        Status status = stub_->s_getattr(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        if (reply.ret() < 0) {
            return reply.ret();
        }
        st->st_dev = reply.stat().dev();
        st->st_ino = reply.stat().ino();
        st->st_mode = reply.stat().mode();
        st->st_nlink = reply.stat().nlink();
        st->st_uid = reply.stat().uid();
        st->st_gid = reply.stat().gid();
        st->st_rdev = reply.stat().rdev();
        st->st_size = reply.stat().size();
        st->st_blksize = reply.stat().blksize();
        st->st_blocks = reply.stat().blocks();
        st->st_atime = reply.stat().atime();
        st->st_mtime = reply.stat().mtime();
        st->st_ctime = reply.stat().ctime();
        return 0;
    }

    int c_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi) {
        printf("[readdir] %s\n", path);
        afs::PathRequest request;
        request.set_path(path);
        afs::ReadDirResponse reply;
        ClientContext context;
        Status status = stub_->s_readdir(&context, request, &reply);
        if (!status.ok()) {
            return -ENONET;
        }
        if (reply.ret() < 0) {
            return reply.ret();
        }
        for (auto &s : reply.entries()) {
            filler(buffer, s.c_str(), NULL, 0);
        }
        return 0;
    }

   private:
    std::unique_ptr<gRPCService::Stub> stub_;
};