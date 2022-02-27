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

#include "server.hpp"

#define RET_ERR(stmt)                                    \
    if ((stmt) < 0) {                                    \
        perror("client error");                          \
        fprintf(stderr, "client error: %d\n", __LINE__); \
        return -errno;                                   \
    }

const std::string CACHE_FOLDER{"./client_cache/"};

static std::string hash_str(const char *src) {
    auto digest = std::make_unique<unsigned char[]>(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char *>(src), strlen(src),
           digest.get());
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(digest[i]);
    }
    return ss.str();
}

static std::string to_cache_path(const char *path) {
    return CACHE_FOLDER + hash_str(path);
}

static std::string to_write_cache_path(const char *path) {
    return CACHE_FOLDER + hash_str(path) + ".dirty";
}

static int do_getattr(const char *path, struct stat *st) {
    printf("[getattr] %s\n", path);

    getattr_req req;
    req.path = path;

    auto res = server_getattr(req);

    memcpy(st, &res.st, sizeof(struct stat));
    return res.ret;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    printf("[readdir] %s\n", path);

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
    printf("[read] %s\n", path);
    ssize_t ret;
    RET_ERR(ret = pread(fi->fh, buffer, size, offset));
    return ret;
}

// Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be filtered out / handled by
// the kernel.
static int do_open(const char *path, struct fuse_file_info *fi) {
    printf("[open] %s\n", path);
    struct stat st;
    auto cache_path = to_cache_path(path);
    int ret = lstat(cache_path.c_str(), &st);

    // Non ENOENT error
    if (ret == -1 && errno != ENOENT) {
        return -errno;
    }

    if (ret == 0) {
        struct stat server_st;
        do_getattr(path, &server_st);
        // Cache file up-to-date
        if (st.st_mtime >= server_st.st_mtime) {
            int fd;
            auto write_path = to_write_cache_path(path);
            if (access(write_path.c_str(), F_OK) == 0) {
                RET_ERR(fd = open(write_path.c_str(), O_RDWR));
            } else {
                RET_ERR(fd = open(cache_path.c_str(), O_RDWR));
            }
            fi->fh = fd;
            return 0;
        }
    }
    open_req req;
    req.path = path;
    auto res = server_open(req);
    if (res.ret != 0) return res.ret;
    auto [tmp_fd, tmp_name] = get_tmp_file();
    RET_ERR(write(tmp_fd, res.buf, strlen(res.buf)));
    RET_ERR(close(tmp_fd));
    struct utimbuf times;
    times.actime = res.atime;
    times.modtime = res.mtime;
    RET_ERR(utime(tmp_name.c_str(), &times));
    RET_ERR(rename(tmp_name.c_str(), cache_path.c_str()));

    int fd;
    RET_ERR(fd = open(cache_path.c_str(), O_RDWR));
    fi->fh = fd;
    return 0;
}

static int do_flush(const char *path, struct fuse_file_info *fi) {
    printf("[flush] %s\n", path);
    auto write_path = to_write_cache_path(path);
    if (access(write_path.c_str(), F_OK) != 0) {
        return 0;
    }
    flush_req req;
    req.path = path;
    memset(req.buf, 0, sizeof(req.buf));
    pread(fi->fh, req.buf, sizeof(req.buf), 0);
    struct stat st;
    fstat(fi->fh, &st);
    req.atime = st.st_atime;
    req.mtime = st.st_mtime;
    auto res = server_flush(req);
    if (res.ret != 0) return res.ret;
    RET_ERR(rename(write_path.c_str(), to_cache_path(path).c_str()));
    return res.ret;
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
    printf("[write] %s\n", path);
    auto write_path = to_write_cache_path(path);
    // copy-on-write
    if (access(write_path.c_str(), F_OK) != 0) {
        printf("[write] copy-on-write!\n");
        auto fd1 = fi->fh;
        auto fd2 = open(write_path.c_str(), O_RDWR | O_CREAT, 0644);
        char buf[4096];
        memset(buf, 0, sizeof(buf));
        ssize_t n;
        while ((n = read(fd1, buf, sizeof(buf))) > 0) {
            write(fd2, buf, n);
        }
        dup2(fd2, fd1);
    }
    auto fd = fi->fh;
    size_t ret;
    RET_ERR(ret = pwrite(fd, buffer, size, offset));
    return ret;
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