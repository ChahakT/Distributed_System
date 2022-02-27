/**
 * Simple & Stupid Filesystem.
 *
 * Mohammed Q. Hussain - http://www.maastaar.net
 *
 * This is an example of using FUSE to build a simple filesystem. It is a part
 * of a tutorial in MQH Blog with the title "Writing a Simple Filesystem Using
 * FUSE in C":
 * http://www.maastaar.net/fuse/linux/filesystem/c/2016/05/21/writing-a-simple-filesystem-using-fuse/
 *
 * License: GNU GPL
 */

#define FUSE_USE_VERSION 35

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#include "server.hpp"

static int do_getattr(const char *path, struct stat *st) {
    printf("[getattr]\n");

    getattr_req req;
    std::cout << path << std::endl;
    req.path = path;

    auto res = server_getattr(req);

    memcpy(st, &res.st, sizeof(struct stat));

    return res.ret;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    printf("[readdir]\n");

    readdir_req req;
    req.path = path;

    auto res = server_readdir(req);

    for (auto &file : res.files) {
        filler(buffer, file.c_str(), NULL, 0);
    }

    return res.ret;
}

static int do_read(const char *path, char *buffer, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    printf("read\n");
    return 0;
}

static int do_open(const char *path, struct fuse_file_info *fi) {
    printf("open\n");
    return 0;
}

static int do_flush(const char *path, struct fuse_file_info *fi) {
    printf("flush\n");
    return 0;
}

static int do_unlink(const char *path) {
    printf("unlink\n");
    return 0;
}

static int do_mkdir(const char *path, mode_t mode) {
    printf("mkdir\n");
    return 0;
}

static int do_rmdir(const char *path) {
    printf("rmdir\n");
    return 0;
}

static int do_write(const char *path, const char *buffer, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    printf("write\n");
    return 0;
}

static struct fuse_operations operations;

int main(int argc, char *argv[]) {
    operations.getattr = do_getattr;
    operations.mkdir = do_mkdir;
    operations.unlink = do_unlink;
    operations.rmdir = do_rmdir;
    operations.open = do_open;
    operations.read = do_read;
    operations.write = do_write;
    operations.flush = do_flush;
    operations.readdir = do_readdir;
    return fuse_main(argc, argv, &operations, NULL);
}