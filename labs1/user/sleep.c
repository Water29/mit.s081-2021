#include "kernel/types.h"
#include "user/user.h"

void main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(2, "Usage: %s <num> \n", argv[0]);
        exit(1);
    }
    if (sleep(atoi(argv[1])) < 0)
    {
        fprintf(2, "Sleep error\n");
    }
    exit(0);
}