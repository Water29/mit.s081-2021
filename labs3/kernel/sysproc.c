#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
extern pte_t *walk(pagetable_t,uint64,int);
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 BitMask =0;

  uint64 StartVA;
  int NumberOfPages;
  uint64 BitMaskVA;

  if(argint(1,&NumberOfPages)<0)return -1;

  if(NumberOfPages>MAXSCAN)return -1;

  if(argaddr(0,&StartVA)<0)return -1;
  if(argaddr(2,&BitMaskVA)<0)return -1;

  int i;
  pte_t *pte;

  for(i=0;i<NumberOfPages;StartVA+=PGSIZE,++i){
    if((pte=walk(myproc()->pagetable,StartVA,0))==0)
      panic("pgaccess : walk failed");
    if(*pte & PTE_A){
      BitMask |= 1<<i;
      *pte &= ~PTE_A;
    }
  }

  copyout(myproc()->pagetable,BitMaskVA,(char*)&BitMask,sizeof(BitMask));
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}



