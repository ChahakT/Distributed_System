#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <string>
#include <vector>

#define RET_ERR_SER(stmt)                                \
    if ((stmt) < 0) {                                    \
        perror("server error");                          \
        fprintf(stderr, "server error: %d\n", __LINE__); \
        res.ret = -errno;                                \
        return res;                                      \
    }

const std::string SERVER_FOLDER{"./server_folder/"};

static std::string to_server_path(const std::string &path) {
    return (SERVER_FOLDER + path);
}

static std::pair<int, std::string> get_tmp_file() {
    char tmpla[] = "/tmp/myfuseXXXXXX";
    int ret = mkstemp(tmpla);
    return std::make_pair(ret, tmpla);
}

struct getattr_req {
    std::string path;
};

struct getattr_res {
    int ret = 0;
    struct stat st;
};

static getattr_res server_getattr(getattr_req req) {
    getattr_res res;
    if (lstat(to_server_path(req.path).c_str(), &res.st) == -1) {
        res.ret = -errno;
    }
    return res;
}

struct readdir_req {
    std::string path;
};

struct readdir_res {
    int ret = 0;
    std::vector<std::string> files;
};

// TODO: Optimize with fill_dir_plus?
static readdir_res server_readdir(readdir_req req) {
    readdir_res res;
    DIR *dp;
    struct dirent *de;
    dp = opendir(to_server_path(req.path).c_str());
    if (dp == NULL) {
        res.ret = -errno;
        return res;
    }
    while ((de = readdir(dp)) != NULL) {
        res.files.push_back(de->d_name);
    }
    closedir(dp);
    return res;
}

struct open_req {
    std::string path;
};

struct open_res {
    int ret = 0;
    char buf[4096];
    time_t atime;
    time_t mtime;
};

// TODO: Better download logic
static open_res server_open(open_req req) {
    open_res res;
    int fd;
    RET_ERR_SER(fd = open(to_server_path(req.path).c_str(), O_RDWR));
    struct stat st;
    RET_ERR_SER(memset(res.buf, 0, sizeof(res.buf)));
    RET_ERR_SER(read(fd, &res.buf, 4096));
    RET_ERR_SER(fstat(fd, &st));
    RET_ERR_SER(close(fd));
    res.atime = st.st_atime;
    res.mtime = st.st_mtime;
    return res;
}

struct flush_req {
    std::string path;
    char buf[4096];
    time_t atime;
    time_t mtime;
};

struct flush_res {
    int ret = 0;
};

// TODO: Better upload logic
static flush_res server_flush(flush_req req) {
    flush_res res;
    int fd;
    auto [tmp_fd, tmp_path] = get_tmp_file();
    RET_ERR_SER(write(tmp_fd, req.buf, strlen(req.buf)));
    if (res.ret == -1) {
        res.ret = -errno;
        return res;
    }
    RET_ERR_SER(close(tmp_fd));
    struct utimbuf times;
    times.actime = req.atime;
    times.modtime = req.mtime;
    RET_ERR_SER(utime(tmp_path.c_str(), &times));
    RET_ERR_SER(rename(tmp_path.c_str(), to_server_path(req.path).c_str()));
    return res;
}