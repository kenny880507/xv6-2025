#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	if(argc > 2) exit(1);
  
	int i = atoi(argv[1]);
	if(i < 0) exit(1);
	pause(i);
  
  exit(0);
}