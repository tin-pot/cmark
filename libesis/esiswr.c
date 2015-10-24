/* esiswr.c */

#include "esisio.h"
#include "esisio_int.h"
#include <stdlib.h>
#include <string.h>

static void
ShipTag(ESIS_Writer, unsigned what, const ESIS_Char *);
static void
ShipData(ESIS_Writer, unsigned how, const ESIS_Char *, size_t);

static void
EsisTagWriter(FILE *, unsigned, const ESIS_Char *, const ESIS_Char **);
static void
EsisDataWriter(FILE *, unsigned, const byte *, size_t);

ESIS_Writer ESISAPI
ESIS_WriterCreateInt(FILE *fp, unsigned options)
{
  ref r;
  
  ESIS_Writer pe = malloc(sizeof *pe);
  if (pe == NULL) return NULL;
  
  esisStackInit(pe->S);
  if (pe->S->buf == NULL) return NULL;
  
  pe->err  = ESIS_ERROR_NONE;
  pe->opts = options;
  r = MARK(0);
  pe->n_att  = 0U;
  pe->r_att  = r;
  pe->r_gi   = r;
  pe->fp     = fp;
  return pe;
}
              
ESIS_Writer ESISAPI
ESIS_WriterCreate(FILE *fp, unsigned options)
{
  ESIS_Writer pe = ESIS_WriterCreateInt(fp, options);
  
  pe->tagfunc  = EsisTagWriter;
  pe->datafunc = EsisDataWriter;
  
  return pe;
}

void ESISAPI
ESIS_WriterFree(ESIS_Writer pe)
{
  free(pe->S->buf);
  free(pe);
}

void ESISAPI
ESIS_Attr(ESIS_Writer pe, const ESIS_Char  *name,
                          const ESIS_Char  *val, size_t len)
{
  size_t n;
  n = strlen(name) + 1U;
  esisStackPush(pe->S, name, n);
  
  if (len == ESIS_NTS) len = strlen(val) + 1U;
  esisStackPush(pe->S, val, len);
  
  ++pe->n_att;
}

void ESISAPI
ESIS_Atts(ESIS_Writer pe, const ESIS_Char **atts)
{
  const ESIS_Char *name;
  
  if (atts == NULL) return;
  
  for (name = atts[0]; name != NULL; atts += 2) {
    const ESIS_Char *val = atts[1];
    size_t len = strlen(val) + 1U;
    ESIS_Attr(pe, name, val, len);
    
    name = atts[2];
  }
}

void ESISAPI
ESIS_Start(ESIS_Writer pe, const ESIS_Char  *elemGI,
                           const ESIS_Char **atts)
{
  size_t n;
  
  if (atts != NULL)
    ESIS_Atts(pe, atts);
  
  pe->r_gi = TOP();
  n = strlen(elemGI) + 1U;
  esisStackPush(pe->S, elemGI, n);
  
  ShipTag(pe, ESIS_START_, elemGI);
}
                            
void ESISAPI
ESIS_StartElem(ESIS_Writer pe, const ESIS_Elem  *elem)
{
  size_t n;
  
  if (elem->atts != NULL)
    ESIS_Atts(pe, elem->atts);
  
  pe->r_gi = TOP();
  n = strlen(elem->elemGI) + 1U;
  esisStackPush(pe->S, elem->elemGI, n);
  
  ShipTag(pe, ESIS_START_, elem->elemGI);
}

void ESISAPI
ESIS_Empty(ESIS_Writer pe, const ESIS_Char  *elemGI,
                           const ESIS_Char **atts)
{
  size_t n;
  
  if (atts != NULL)
    ESIS_Atts(pe, atts);
  
  pe->r_gi = TOP();
  n = strlen(elemGI) + 1U;
  esisStackPush(pe->S, elemGI, n);
  
  ShipTag(pe, ESIS_EMPTY_, elemGI);
}

void ESISAPI
ESIS_EmptyElem(ESIS_Writer pe, const ESIS_Elem  *elem)
{
  size_t n;
  
  if (elem->atts != NULL)
    ESIS_Atts(pe, elem->atts);
  
  pe->r_gi = TOP();
  n = strlen(elem->elemGI) + 1U;
  esisStackPush(pe->S, elem->elemGI, n);
  
  ShipTag(pe, ESIS_EMPTY_, elem->elemGI);
}

void ESISAPI
ESIS_End(ESIS_Writer pe, const ESIS_Char *elemGI)
{
  ShipTag(pe, ESIS_END_, elemGI);
}

void ESISAPI
ESIS_EndElem(ESIS_Writer pe, const ESIS_Elem *elem)
{
  ShipTag(pe, ESIS_END_, elem->elemGI);
}

void ESISAPI
ESIS_PCdata(ESIS_Writer pe, const ESIS_Char *cd, size_t len)
{
  ShipData(pe, ESIS_PCDATA_, cd,len);
}

void ESISAPI
ESIS_Cdata(ESIS_Writer pe, const ESIS_Char *cd, size_t len)
{
  ShipData(pe, ESIS_CDATA_, cd,len);
}


/*====================================================================*/

static void
ShipTag(ESIS_Writer pe, unsigned what, const ESIS_Char *elemGI)
{
  unsigned n_att, k;
  ref r_atts;
  ESIS_Char *p, **pp;
  ESIS_Char **atts;
  size_t n;
  
  if ((what & ESIS_START_) && pe->n_att) {
    n_att = pe->n_att;
    n = (2 * n_att + 1 ) * sizeof p;
    r_atts = MARK(n + sizeof p);
    n = r_atts - r_atts % sizeof p + sizeof p;
    
    pp = P(n);
    p  = P(pe->r_att); 
    
    atts = pp;
    
    for (k = 0; k < n_att; ++k) {
      ESIS_Char *name, *val;
      
      name = p;
      p += strlen(p) + 1;
      
      val = p;
      p += strlen(p) + 1;
      
      *pp++ = name;
      *pp++ = val;
    }
    *pp = NULL;
  } else {
    atts = NULL;
    r_atts = TOP();
  }
  
  what |= (pe->opts & ~ESIS_NONOPTION_);
  pe->tagfunc(pe->fp, what, elemGI, atts);
  
  pe->n_att = 0U;
  pe->r_gi = pe->r_att = 0U;
  RELEASE(0U);
}


static void
ShipData(ESIS_Writer pe, unsigned how,
                         const ESIS_Char *data, size_t len)
{
  if (len > 0U) {
    how |= (pe->opts & ~ESIS_NONOPTION_);
    pe->datafunc(pe->fp, how, data, len);
  }
}

/*====================================================================*/

static void ESISAPI
EsisTagWriter(FILE             *outputFile,
              unsigned          what,
              const ESIS_Char  *elemGI,
              const ESIS_Char **atts)
{
  unsigned k;
  
  if (what & ESIS_START_) {
    if (atts != NULL)
      for (k = 0; atts[k] != NULL; k += 2)
        fprintf(outputFile, "A%s CDATA %s\n", atts[k], atts[k+1]);
    fprintf(outputFile, "(%s\n", elemGI);
  }
  
  if (what & ESIS_END_) {
    fprintf(outputFile, ")%s\n", elemGI);
  }
}


static void ESISAPI
EsisDataWriter(FILE             *outputFile,
               unsigned         how,
               const byte      *data,
               size_t           len)
{
  unsigned k;
  
  putc('-', outputFile);
  
  for (k = 0; k < len; ++k) {
    byte b = data[k];
    if (b >= 0x20)
      putc(b, outputFile);
    else if (b == '\n') {
      fputs("\\012\\n", outputFile);
    } else {   
      fprintf(outputFile, "\\%.3o", b);
    } 
  }
  
  putc('\n', outputFile);
}