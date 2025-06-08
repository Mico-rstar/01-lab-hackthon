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

// reference count
struct
{
  struct spinlock lock;
  char cnt[(PHYSTOP - KERNBASE)/PGSIZE];
  uint64 end_;
} rcnt;

void
rinit()
{
  initlock(&rcnt.lock, "rcnt");
  acquire(&rcnt.lock);
  rcnt.end_=(uint64)end;
  for(int i=0; i<(PHYSTOP - KERNBASE)/PGSIZE; i++)
  {
    rcnt.cnt[i] = 0;
  }
  release(&rcnt.lock);
}

unsigned int
index(uint64 pa)
{
  int res = (pa - rcnt.end_) / PGSIZE;
  if(res < 0 || res >= (PHYSTOP - KERNBASE)/PGSIZE)
  {
    printf("pa = %p, index = %d\n", pa, res);
    panic("index: illegal index");
  }
  return res;
}

void
incr(uint64 pa)
{
  acquire(&rcnt.lock);
  rcnt.cnt[index(pa)]++;
  release(&rcnt.lock);
}

void
decr(uint64 pa)
{
  acquire(&rcnt.lock);
  rcnt.cnt[index(pa)]--;
  release(&rcnt.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  rinit();
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

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(rcnt.cnt[index((uint64)pa)] >= 1)
  {
    decr((uint64)pa);
    return;
  }
  else if(rcnt.cnt[index((uint64)pa)] < 0)
  {
    panic("kfree: unexpected ref cnt");
  }
  decr((uint64)pa);

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);

  // set the reference count = 1
  if(r)
    incr((uint64)r);
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
