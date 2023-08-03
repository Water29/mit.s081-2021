// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
    struct spinlock lock;
    uint counter[(PHYSTOP-KERNBASE) / PGSIZE];
} refcnt;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    uint idx = ((uint64)pa-KERNBASE)/PGSIZE;
    acquire(&refcnt.lock);
    if(refcnt.counter[idx] > 1){
        refcnt.counter[idx]--;
        release(&refcnt.lock);
        return;
    }
    refcnt.counter[idx] = 0;
    release(&refcnt.lock);
  struct run *r;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  if(r) {
      memset((char *) r, 5, PGSIZE); // fill with junk
  }
    if(r) {
        addref((uint64)r);
    }
    return (void*)r;
}

void *
kalloc_nolock(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if(r)
        kmem.freelist = r->next;
    release(&kmem.lock);
    if(r) {
        memset((char *) r, 5, PGSIZE); // fill with junk
    }
    if(r) {
        refcnt.counter[((uint64)r - KERNBASE)/PGSIZE]++;
    }
    return (void*)r;
}

int
cowcopy(uint64 va, pagetable_t page)
{
    va = PGROUNDDOWN(va);
    pte_t *pte = walk(page, va, 0);
    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);

    if(!(flags & PTE_COW)){
        printf("not cow\n");
        return -1;
    }

    uint idx = (pa - KERNBASE)/PGSIZE;
    acquire(&refcnt.lock);
    if(refcnt.counter[idx] > 1){
        uint64 pa_new = (uint64)kalloc_nolock();
        if(pa_new == 0) {
            release(&refcnt.lock);
            return -1;
        }
        refcnt.counter[idx]--;
        memmove((char*)pa_new, (char*)pa, PGSIZE);
        if(mappages(page, va, PGSIZE, pa_new, ((flags & (~PTE_COW)) | PTE_W)) != 0){
            kfree((char*)pa_new);
            release(&refcnt.lock);
            return -1;
        }
    }else{
        (*pte) = ((*pte) & (~PTE_COW)) | PTE_W;
    }
    release(&refcnt.lock);
    return 0;
}


void addref(uint64 pa)
{
    acquire(&refcnt.lock);
    refcnt.counter[(pa - KERNBASE)/PGSIZE]++;
    release(&refcnt.lock);
}