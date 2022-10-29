#ifndef __VECTOR_H__
#define __VECTOR_H__

/* Example:

   struct MyStruct {
     int a;
     int b;
   }

   struct MyStruct* things = vecnew(sizeof(struct MyStruct), 100);
   assert(veclen(things) == 0);

   things = vecpush(things);
   assert(veclen(things) == 1)
   things[0].a = 1;
   things[0].b = 2;

   things = vecpush(things);
   assert(veclen(things) == 2);
   things[veclast(things)].a = 3;
   things[veclast(things)].b = 4;

   vecfree(things);
 */

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

/* Reserved memory follows the vector (there's sz * cap bytes
   allocated following this structure) */
typedef struct Vector {
  size_t sz;
  size_t len;
  size_t cap;
} Vector;

/* Create a new vector, with items sized `sz` and initial capactity
   `cap`. Returns a pointer to your data (bookkeeping data is below
   the pointer). */
void* vecnew(size_t sz, size_t cap) {
  if (cap < 64) {
    cap = 64;
  }

  if (sz == 0) {
    return NULL;
  }

  Vector* v = (Vector*)malloc(sizeof(Vector) + sz * cap);
  if (v == NULL) {
    return NULL;
  }

  v->sz = sz;
  v->len = 0;
  v->cap = cap;

  void* data = (void*)v + sizeof(Vector);
  memset(data, 0, sz * cap);
  return data;
}

/* Returns the length of the vector */
size_t veclen(void* data) {
  return ((Vector*)(data - sizeof(Vector)))->len;
}

/* Returns the length-1 of the vector, or zero if empty... */
size_t veclast(void* data) {
  size_t len = veclen(data);
  if (len == 0) return 0; /* Really just don't call in this case... */
  return len-1;
}

/* Deallocates the memory associated w/ the vector */
void vecfree(void* data) {
  free(data - sizeof(Vector));
}

/* Adds a new item onto the vector, returns a pointer to the start
   (this may change if the push exceeds the capacity and a copy is
   needed) */
void* vecpush(void* data, size_t count) {
  Vector* v = (Vector*)(data - sizeof(Vector));

  if (v->len + count <= v->cap) {
    v->len += count;
    return data;
  }

  /* Copy the vector into a area w/ increased capacity */
  void* new = vecnew(v->sz, v->cap*2);
  if (new == NULL) {
    vecfree(data);
    return NULL;
  }

  memcpy(new, data, v->sz * v->len);
  ((Vector*)(new - sizeof(Vector)))->len = v->len;

  vecfree(data);
  return vecpush(new, count);
}

#endif
