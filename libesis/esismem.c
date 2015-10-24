/* esismem.c */
#include "esisio.h"
#include "esisio_int.h"
#include <stdlib.h>
#include <string.h>

void esisStackInit(struct esis_stack_ *p)
{
  const size_t n = STACK_CHUNK;
  
  p->buf = malloc(n);
  p->lim = n;
  p->top = p->mark = 0U;
}

void esisStackGrow(struct esis_stack_ *p, size_t n)
{
  byte *pb;
  const size_t nb = STACK_CHUNK *
                                ((n + 2*STACK_CHUNK - 1) / STACK_CHUNK);
  
  pb = realloc(p->buf, n);
  p->buf = pb;
  p->lim = n;
}

ref esisStackMark(struct esis_stack_ *p, size_t n)
{
  if (p->mark + n > p->lim)
    esisStackGrow(p, n);
  p->top = p->mark;
  p->mark += n;
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
  const ref t = p->top;
  
  if (p->top + n > p->lim)
    esisStackGrow(p, p->top + n - p->lim);
    
  if (v != NULL)
    memcpy(p->buf + t, v, n);
  p->top += n;
  
  if (p->top > p->mark)
    p->mark = p->top;

  return p->top;
}

ref esisStackPop(struct esis_stack_ *p, void *v, size_t n)
{
  const ref t = p->top -= n;
  
  if (v != NULL) 
    memcpy(v, p->buf + t, n);
    
  return t;
}

