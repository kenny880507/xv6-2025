#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
    char buff[512];
    char seperator[9] = " -\r\t\n./,";

    for(int f = 1; f < argc; f++){
        int fd = open(argv[f], O_RDONLY);
        int num = 0;
        while(1)
        {
            if(read(fd,buff,1)==0) break;
            char c = buff[0];
            if(strchr(seperator,c) != 0){
                if(num != 0 && (num % 5 == 0 || num % 6 == 0))
                    printf("%d\n",num);
                num = 0;
            } else {
                num *= 10;
                num += c - '0';
            }
        }
        if(num != 0 && (num % 5 == 0 || num % 6 == 0))
            printf("%d\n",num);
        close(fd);
    }
    exit(0);
}