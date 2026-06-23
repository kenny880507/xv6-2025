#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

void find(char* path, char* target, char** cmd, int cmdlen){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE:
        break;

    case T_DIR:
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0) continue;
            if(strcmp(de.name,".") == 0 || strcmp(de.name,"..") == 0) continue;

            strcpy(buf, path);
            p = buf+strlen(buf);
            *p++ = '/';
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            if(strcmp(target, de.name) == 0){
                if(cmd == 0){
                    // 原本功能：印出路徑
                    printf("%s\n", buf);
                } else {
                    // -exec 功能：組合 argv 然後 fork+exec
                    char *args[MAXARG];
                    int i;
                    for(i = 0; i < cmdlen; i++)
                        args[i] = cmd[i];
                    args[cmdlen] = buf;       // 把檔案路徑接在最後
                    args[cmdlen + 1] = 0;     // null 結尾

                    int pid = fork();
                    if(pid == 0){
                        exec(args[0], args);
                        fprintf(2, "find: exec %s failed\n", args[0]);
                        exit(1);
                    } else {
                        wait(0);
                    }
                }
            }

            int sub_fd = open(buf, 0);
            if(sub_fd >= 0){
                struct stat sub_st;
                if(fstat(sub_fd, &sub_st) >= 0 && sub_st.type == T_DIR){
                    find(buf, target, cmd, cmdlen);
                }
                close(sub_fd);
            }
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[]){
    if(argc >= 5 && strcmp(argv[3], "-exec") == 0){
        find(argv[1], argv[2], argv+4, argc-4);
    } else if(argc >= 3){
        find(argv[1], argv[2], 0, 0);
    } else {
        fprintf(2, "usage: find path target [-exec cmd [args...]]\n");
        exit(1);
    }
    exit(0);
}