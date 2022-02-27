#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <string>
#include <vector>
struct getattr_req {
    std::string path;
};

struct getattr_res {
    int ret;
    struct stat st;
};

const std::string prefix = "./server_folder";

std::string to_local_path(const std::string &path) { return (prefix + path); }

getattr_res server_getattr(getattr_req req) {
    std::cout << req.path << std::endl;
    getattr_res res;
    std::cout << to_local_path(req.path) << std::endl;
    res.ret = lstat(to_local_path(req.path).c_str(), &res.st);
    return res;
}

struct readdir_req {
    std::string path;
};

struct readdir_res {
    int ret;
    std::vector<std::string> files;
};

// TODO: Optimize with fill_dir_plus?
readdir_res server_readdir(readdir_req req) {
    readdir_res res;
    DIR *dp;
    struct dirent *de;
    dp = opendir(to_local_path(req.path).c_str());
    if (dp == NULL) {
        res.ret = -errno;
        return res;
    }
    while ((de = readdir(dp)) != NULL) {
        res.files.push_back(de->d_name);
    }
    closedir(dp);
    res.ret = 0;
    return res;
}