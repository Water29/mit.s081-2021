#include "kernel/types.h"
#include "user/user.h"
#define INDEX_READ 0
#define INDEX_WRITE 1

void child(int p_c[])
{
 close(p_c[INDEX_WRITE]);
 int i;
 if (read(p_c[INDEX_READ], &i, sizeof(i)) == 0)
 {
 close(p_c[INDEX_READ]);
 exit(0);
 }
 printf("prime %d\n", i);
 int num = 0;

 int c_gc[2];
 pipe(c_gc);
 int pid;

 if ((pid = fork()) == 0)
 {
 child(c_gc);
 }
 else
 {
 close(c_gc[INDEX_READ]);
 while (read(p_c[INDEX_READ], &num, sizeof(num)) > 0)
 {
 if (num % i != 0)
 {
 write(c_gc[INDEX_WRITE], &num, sizeof(num));
 }
 }
 close(c_gc[INDEX_WRITE]);
 wait(0);
 }
 exit(0);
}
int main(int argc, char *argv[])
{
 int p_c[2];
 pipe(p_c);
 int pid;
 if ((pid = fork()) == 0)
 {
 child(p_c);
 }
 else
 {
 close(p_c[INDEX_READ]);
 for (int i = 2; i <= 35; i++)
 {
 write(p_c[INDEX_WRITE], &i, sizeof(i));
 }
 close(p_c[INDEX_WRITE]);
 wait(0);
 }
 exit(0);
}