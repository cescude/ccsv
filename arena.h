
#ifndef __ARENA_H__
#define __ARENA_H__

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

/* Reserved memory follows the region (ie., there will be `sz`
   bytes following this structure that can be handed out via 
   aralloc()) */
typedef struct Region {
  size_t offset;
  size_t sz;
  struct Region* next;
} Region;

typedef struct Arena {
  struct Region* regions;
} Arena;

Region* regnew(size_t bytes_needed) {
  if (bytes_needed < (4<<20)) {
    bytes_needed = 4<<20;       /* Don't allocate fewer than 4MB per region */
  }

  Region* r = (Region*)malloc(sizeof(Region) + bytes_needed);
  if (r == NULL) {
    return NULL;
  }

  r->sz = bytes_needed;
  r->offset = 0;
  r->next = NULL;

  return r;
}

void* regalloc(Region* region, size_t bytes_needed) {
  if (bytes_needed <= (region->sz-region->offset)) {
    void *ptr = (void*)region + sizeof(Region) + region->offset;
    region->offset += bytes_needed;
    return ptr;
  }
  return NULL;
}

void regreset(Region* region) {
  region->offset = 0;
}

void arinit(Arena* a) {
  a->regions = regnew(0);
}

void ardump(Arena* arena) {
  Region* r = arena->regions;
  while (r != NULL) {
    printf("Arena: %p, region=%p, sz=%zu, offset=%zu, next=%p\n", arena, r, r->sz, r->offset, r->next);
    r = r->next;
  }
}

void arreset(Arena* arena) {
  Region* r = arena->regions;
  while (r != NULL) {
    r->offset = 0;
    r = r->next;
  }
}

void arfree(Arena* arena) {
  Region* r = arena->regions;
  while (r != NULL) {
    Region* next = r->next;
    free(r);
    r = next;
  }
}

void* aralloc_raw(Arena* arena, size_t sz) {
  /* Need sz bytes, find somewhere to put it */
  Region* region = arena->regions;
  while (region != NULL) {

    /* Can we squeeze sz bytes into this arena? */
    if (sz <= (region->sz - region->offset)) {
      /* Yes? Great, we found a pre-allocated region! */
      break;
    }

    region = region->next;
  }

  if (region == NULL) {
    
    /* Couldn't find a region with enough freespace to reserve, so
       let's make a new one & stick it on the front! */
    
    region = regnew(sz);
    if (region == NULL) {
      return NULL;
    }

    region->next = arena->regions;
    arena->regions = region;
  }

  void* ptr = (void*)region + sizeof(Region) + region->offset;
  region->offset += sz;

  return ptr;
}

void* aralloc(Arena* arena, size_t sz) {
  void* ptr = aralloc_raw(arena, sz);
  memset(ptr, 0, sz);
  return ptr;
}

#endif
