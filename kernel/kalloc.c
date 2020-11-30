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

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void
kinit()
{
  for(int i=0;i<NCPU;i++){
    initlock(&kmems[i].lock, "kmem");
  }
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
  struct run *r;
  push_off();
  int cur_cpu = cpuid();
  pop_off();
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[cur_cpu].lock);
  r->next = kmems[cur_cpu].freelist;
  kmems[cur_cpu].freelist = r;
  release(&kmems[cur_cpu].lock);
}

void * 
steal(){
  struct run * r=0;
  for(int i=0;i<NCPU;i++){
    if(holding(&kmems[i].lock)){    //避免冲突
      continue;
    }
    acquire(&kmems[i].lock);
    if(kmems[i].freelist!=0){
      r=kmems[i].freelist;
      kmems[i].freelist=r->next;
      release(&kmems[i].lock);
      return (void *)r;
    }
    release(&kmems[i].lock);
  }
  return (void *)r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cur_cpu = cpuid();
  pop_off();
  acquire(&kmems[cur_cpu].lock);
  r = kmems[cur_cpu].freelist;
  if(r)
    kmems[cur_cpu].freelist = r->next;
  release(&kmems[cur_cpu].lock);
  if(!r){ //当前的freelist为空，则从其他CPU偷
    r = steal();
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

