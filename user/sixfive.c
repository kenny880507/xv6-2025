#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
	int fd = open(argv[1],O_RDONLY);
	char buff[512];

	char seperator[9] = " -\r\t\n./,";

	int num = 0;
	while(1)
	{
		if(read(fd,buff,1)==0) break;
		char c = buff[0];
		if(strchr(seperator,c) != 0){
			if(num != 0 && (num % 5 == 0 || num % 6 ==0))
				printf("%d\n",num);
			num = 0;
		} else {
			num *= 10;
			num += atoi(&c); 
		}
	}
	if(num != 0 && (num % 5 == 0 || num % 6 == 0))
    printf("%d\n",num);
	close(fd);
  exit(0);
}