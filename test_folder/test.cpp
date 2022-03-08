#include "test.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "iostream"
#include <memory>
#include "cstring"

extern int errno;

int main(int argc, char* argv[]) {
    std::string mnt_dir = argv[1];
    std::string file1 = argv[2];
    std::string file2 = argv[3];
    int check;
    const char dirname[] = mnt_dir + "/dir1";
    check = mkdir(dirname, 0777);

    // check if directory is created or not
    if (!check)
        printf("Directory created\n");
    else {
        printf("Unable to create directory %s\n", strerror(errno));
        exit(1);
    }
    check = rmdir(dirname);
    if (!check)
        std::cout << "Directory deleted" << std::endl;

    std::string test_file_name = mnt_dir + "/" + file1;
    std::string test_file_name2 = mnt_dir + "/" + file2;
    int fd = open(test_file_name.c_str(), O_RDONLY);

    if (fd < 0) {
        std::cout << "File cannot be opened " << errno << std::endl;
    }

    char buf[4096];
    std::memset(buf, 0, sizeof(buf));
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        std::cout << buf;
    }
    std::cout << std::endl;
    std::cout << "read completed" << std::endl;

    lseek(fd, 0, SEEK_SET);
    std::memset(buf, 0, sizeof(buf));
    int fdnew = open(test_file_name2.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0777);

    if (fdnew < 0) {
        std::cout << "File cannot be opened " << errno << std::endl;
    }

    lseek(fdnew, 0, SEEK_SET);
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        std::cout << "Writing file" << std::endl;
        write(fdnew, buf, n);
    }

    std::cout << "write completed" << std::endl;
    std::cout.flush();
    close(fd);
    close(fdnew);
    return 0;
}