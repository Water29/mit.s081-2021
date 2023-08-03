// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKET 13
#define NONE -1
struct {
  struct spinlock biglock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // 不同桶对应的链表头节点与保护锁
  struct spinlock bucket_lock[BUCKET];
  struct buf head[BUCKET];
} bcache;

// 块号->桶号
int get_bucket_index(uint blockno) { return blockno % BUCKET; }

// 从双向链表中删除该节点
void delete_entry(struct buf *p) {
  p->next->prev = p->prev;
  p->prev->next = p->next;
}

// 在双向链表中插入该节点
void add_entry(struct buf *head, struct buf *b) {
  b->prev = head;
  b->next = head->next;
  head->next->prev = b;
  head->next = b;
}

// 选择最久未使用的buffer
struct buf *least_recent_used_bufffer() {
  int index = -1;
  uint min_tick = 0xffffffff;
  for (int i = 0; i < NBUF; i++) {
    // 该buffer未被使用
    if (bcache.buf[i].owner == NONE) {
      return &bcache.buf[i];
    }

    // 选择引用计数为0且访问时间最小的buffer
    if (bcache.buf[i].refcnt == 0 && bcache.buf[i].access_time < min_tick) {
      index = i;
      min_tick = bcache.buf[i].access_time;
    }
  }
  if (index == -1) return 0;
  return &bcache.buf[index];
}


void binit(void) {
  struct buf *b;
  char buf[16];
  initlock(&bcache.biglock, "bcache-bigblock");
  // 初始化各桶的链表头部与保护锁
  for (int i = 0; i < BUCKET; i++) {
    snprintf(buf, 16, "bcache-%d", i);
    initlock(&bcache.bucket_lock[i], buf);
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  // 初始化各个buffer的拥有者与保护锁
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    b->owner = NONE;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;
  int index = get_bucket_index(blockno);
  acquire(&bcache.bucket_lock[index]);
  // 遍历该桶链表，查询是否缓存该块
  for (b = bcache.head[index].next; b != &bcache.head[index]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;

      release(&bcache.bucket_lock[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket_lock[index]);
  // 未缓存该块，分配新块存放对应数据
  // 先获取大锁，再获取bucket锁，避免死锁发生
  acquire(&bcache.biglock);
  acquire(&bcache.bucket_lock[index]);
  // 再次检查是否存在对应buffer，避免为同一个块分配两个buffer
  for (b = bcache.head[index].next; b != &bcache.head[index]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bucket_lock[index]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 分配一个buffer存放相应数据
  while (1) {
    b = least_recent_used_bufffer();
    if (b == 0) {
      printf("no buffer\n");
      continue;
    }
    int old_owner = b->owner;
    // 若拥有者就是本身或未使用则不需要加其他锁
    if (old_owner == NONE || old_owner == index) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->owner = index;

      // 之前未使用，需加入到对应链表
      if (old_owner == NONE) add_entry(&bcache.head[index], b);

      release(&bcache.bucket_lock[index]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    } else {
      // 拥有者为其他bucket，需要加锁
      acquire(&bcache.bucket_lock[old_owner]);
      if (b->refcnt != 0) {  // 引用计数不为0，中途被改变，重新执行分配过程
        printf("reference count change. b->refcnt:%d\n", b->refcnt);
        release(&bcache.bucket_lock[old_owner]);
        continue;
      }
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->owner = index;

      delete_entry(b);                    // 从原有链表中删除该节点
      add_entry(&bcache.head[index], b);  // 在本桶链表中加入该节点

      release(&bcache.bucket_lock[old_owner]);
      release(&bcache.bucket_lock[index]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket_lock[index]);
  release(&bcache.biglock);
  panic("bget: no buffers");
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("brelse");
  releasesleep(&b->lock);
  int index = get_bucket_index(b->blockno);
  acquire(&bcache.bucket_lock[index]);
  b->refcnt--;
  // 引用计数为0时记录访问时间
  if (b->refcnt == 0) {
    b->access_time = ticks;
  }
  release(&bcache.bucket_lock[index]);
}

void bpin(struct buf *b) {
  int index = get_bucket_index(b->blockno);
  acquire(&bcache.bucket_lock[index]);
  b->refcnt++;
  release(&bcache.bucket_lock[index]);
}

void bunpin(struct buf *b) {
  int index = get_bucket_index(b->blockno);
  acquire(&bcache.bucket_lock[index]);
  b->refcnt--;
  release(&bcache.bucket_lock[index]);
}



