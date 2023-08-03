#include "kernel/types.h"
#include "user/user.h"
#define BUFFER_SIZE 16
#define INDEX_READ 0
#define INDEX_WRITE 1

int main(int argc, char *argv[])
{
 int p_c[2]; 
 int c_p[2]; 

 pipe(c_p);
 pipe(p_c);
 int pid;
 pid = fork();

 if (pid == 0)
 {

 close(c_p[INDEX_READ]);
 close(p_c[INDEX_WRITE]);

 char buf[BUFFER_SIZE];
 if (read(p_c[INDEX_READ], buf, 1) == 1)
 {
 printf("%d: received ping\n", getpid());
 }

 write(c_p[INDEX_WRITE], "f", 1);
 exit(0);
 }

 else
 {

 close(c_p[INDEX_WRITE]);
 close(p_c[INDEX_READ]);

 write(p_c[INDEX_WRITE], "f", 1);

 char buf[BUFFER_SIZE];
 wait((int *)0);
 if (read(c_p[INDEX_READ], buf, 1) == 1)
 {
 printf("%d: received pong\n", getpid());
 }
 }

 exit(0);
}