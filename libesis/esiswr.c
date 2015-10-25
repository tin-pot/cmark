/* esiswr.c */

#include "esisio.h"
#include "esisio_int.h"
#include <stdlib.h>
#include <string.h>

static void
ShipTag(ESIS_Writer, unsigned what, const ESIS_Char *, unsigned, ref);
static void
ShipData(ESIS_Writer, unsigned how, const ESIS_Char *, size_t);

static void
EsisTagWriter(FILE *, unsigned, const ESIS_Char *, const ESIS_Char **);
static void
EsisDataWriter(FILE *, unsigned, const byte *, size_t);

ESIS_Writer ESISAPI
ESIS_WriterCreateInt_(FILE *fp, unsigned options)
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
  ESIS_Writer pe = ESIS_WriterCreateInt_(fp, options);
  
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
  
  if (len == ESIS_NTS) {
    esisStackPush(pe->S, val, strlen(val) + 1U);
  } else {
    esisStackPush(pe->S, val, len);
    PUSH_CHAR('\0');
  }
  
  ++pe->n_att;
}

void ESISAPI
ESIS_Atts(ESIS_Writer pe, const ESIS_Char **atts)
{
  const ESIS_Char *name;
  
  if (atts == NULL) return;
  
  for (name = atts[0]; name != NULL; atts += 2) {
    const ESIS_Char *val = atts[1];
    size_t len = strlen(val);
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
  
  ShipTag(pe, ESIS_START_, elemGI, pe->n_att, pe->r_att);
  pe->n_att = 0;
}
                            
void ESISAPI
ESIS_StartElem(ESIS_Writer pe, ESIS_Elem  *elem)
{
  size_t n;
  
  if (elem->atts != NULL)
    ESIS_Atts(pe, elem->atts);
  
  pe->r_gi = TOP();
  n = strlen(elem->elemGI) + 1U;
  esisStackPush(pe->S, elem->elemGI, n);
  
  ShipTag(pe, ESIS_START_, elem->elemGI, pe->n_att, pe->r_att);
  pe->n_att = 0U;
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
  
  ShipTag(pe, ESIS_EMPTY_, elemGI, pe->n_att, pe->r_att);
  pe->n_att = 0U;
}

void ESISAPI
ESIS_EmptyElem(ESIS_Writer pe, ESIS_Elem  *elem)
{
  size_t n;
  
  if (elem->atts != NULL)
    ESIS_Atts(pe, elem->atts);
  
  pe->r_gi = TOP();
  n = strlen(elem->elemGI) + 1U;
  esisStackPush(pe->S, elem->elemGI, n);
  
  ShipTag(pe, ESIS_EMPTY_, elem->elemGI, pe->n_att, pe->r_att);
  pe->n_att = 0U;
}

void ESISAPI
ESIS_End(ESIS_Writer pe, const ESIS_Char *elemGI)
{
  ShipTag(pe, ESIS_END_, elemGI, 0, pe->r_att);
  pe->n_att = 0U;
}

void ESISAPI
ESIS_EndElem(ESIS_Writer pe, ESIS_Elem *elem)
{
  ShipTag(pe, ESIS_END_, elem->elemGI, 0, pe->r_att);
  pe->n_att = 0U;
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
ShipTag(ESIS_Writer pe, unsigned what, const ESIS_Char *elemGI, 
                        unsigned n_att, ref r_att)
{
  const ESIS_Char **atts;
  static const ESIS_Char *null_atts[2] = { NULL, NULL };
  
  if ((what & ESIS_START_) && n_att) {
    atts = ESIS_Atts_((ESIS_Parser)pe, n_att, r_att);
  } else {
    null_atts[0] = null_atts[1] = NULL;
    atts = null_atts;
  }
  
  what |= (pe->opts & ~ESIS_NONOPTION_);
  pe->tagfunc(pe->fp, what, elemGI, atts);
  
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
    if (b == '\\') {   
      fputs("\\\\", outputFile);
    } else if (b >= 0x20)
      putc(b, outputFile);
    else if (b == '\n') {
      fputs("\\012\\n", outputFile);
    } else {
      fprintf(outputFile, "\\%.3o", b);
    } 
  }
  
  putc('\n', outputFile);
}