#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void find(char *path, char *filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_DEVICE:
    case T_FILE:
        fprintf(2, "find: %s is not a directory name\n", path);
        break;

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            fprintf(2, "find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        if(p[-1] != '/')
            *p++ = '/';

        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            memcpy(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if(stat(buf, &st) < 0){
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;
            }
            switch (st.type)
            {
            case T_DEVICE:
            case T_FILE:
                if(strcmp(filename, p) == 0)
                    printf("%s\n", buf);
                break;
            case T_DIR:
                if(strcmp(p, "") != 0 && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
                    find(buf, filename);
                }
                break;
            }
        }
        break;
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if(argc < 3){
        fprintf(2, "usage: find [dir] [name]");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
