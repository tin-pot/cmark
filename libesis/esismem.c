/* esismem.c */
#include "esisio.h"
#include "esisio_int.h"
#include <stdlib.h>
#include <string.h>

void esisStackInit(struct esis_stack_ *p)
{
  const size_t n = STACK_CHUNK;
  
  p->buf = malloc(n);
  if (p->buf != NULL) {
    p->lim = n;
    p->top = p->mark = 0U;
    p->err = ESIS_ERROR_NONE;
  } else
    p->err = ESIS_ERROR_NO_MEMORY;
}

void esisStackGrow(struct esis_stack_ *p, size_t n)
{
  int err = p->err;
  byte *pb;
  const size_t nb = STACK_CHUNK *
                                ((n + 2*STACK_CHUNK - 1) / STACK_CHUNK);
  
  if (!err)
    pb = realloc(p->buf, n);
  if (!err && pb != NULL) {
    p->buf = pb;
    p->lim = n;
  } else
    p->err = ESIS_ERROR_NO_MEMORY;
}

ref esisStackMark(struct esis_stack_ *p, size_t n)
{
  int err = p->err;
  
  if (!err && p->mark + n > p->lim)
    esisStackGrow(p, n);
  if (p->err == ESIS_ERROR_NONE) {
    p->top = p->mark;
    p->mark += n;
  }
  
  return p->top;
}

ref esisStackRelease(struct esis_stack_ *p, ref r)
{
  p->mark = r;
  
  if (p->top > r)
    p->top = r;
    
  return r;
}

ref esisStackPush(struct esis_stack_ *p, void *v, size_t n)
{
  int err = ESIS_ERROR_NONE;
  
  const ref t = p->top;
  
  if (!err && p->top + n > p->lim)
    esisStackGrow(p, p->top + n - p->lim);
    
  if (p->err == ESIS_ERROR_NONE) {
    if (v != NULL)
      memcpy(p->buf + t, v, n);
    p->top += n;
  
    if (p->top > p->mark)
      p->mark = p->top;
  }
  
  return p->top;
}

ref esisStackPop(struct esis_stack_ *p, void *v, size_t n)
{
  ref t = p->top;
  
  if (!p->err && v != NULL) {
    t -= n;
    memcpy(v, p->buf + t, n);
  }
    
  return t;
}

