/* esisrd.c */

#include "esisio.h"
#include "esisio_int.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef ESIS_XMLPARSE /* NOT IMPLEMENTED */
ESIS_Parser ESISAPI
ESIS_XmlParserCreate(XML_Parser xmlParser)
{
  return NULL;
}
#endif


#ifndef NDEBUG /* NOT IMPLEMENTED */
ESIS_Bool ESISAPI
ESIS_ParserReset(ESIS_Parser pe, const ESIS_Char *encoding)
{
  return ESIS_FALSE;
}
#endif


void ESISAPI
ESIS_SetElementHandler(ESIS_Parser pe,
                       ESIS_ElementHandler handler,
                       const ESIS_Char    *elemGI,
                       long                elemID,
                       void               *userData)
{
  size_t n;
  struct hi *p_hi;
  ref r_gi;
  ref r_hi;
  
  if (elemGI == NULL) {
  
    pe->handler  = handler;
    pe->elemID   = elemID;
    pe->userData = userData;
    
  } else {
  
    n = strlen(elemGI) + 1;
    r_gi = pe->HD->top;
    esisStackPush(pe->HD, elemGI, n);
    
    r_hi = esisStackMark(pe->HI, sizeof *p_hi);
    p_hi = (struct hi *)(pe->HI->buf + r_hi);
    
    p_hi->r_ElemGI = r_gi;
    p_hi->elemGI   = NULL; /* Set when parsing begins. */
    p_hi->elemID   = elemID;
    p_hi->userData = userData;
    p_hi->handler  = handler;
    
    ++pe->n_hi;
  }
}


#ifndef NDEBUG /* NOT IMPLEMENTED */
size_t ESISAPI
ESIS_Env(ESIS_Parser pe, ESIS_Elem outElems[], size_t depth)
{
  return 0U;
}
#endif

static int cmp_hi(const void *lhs, const void *rhs)
{
  const struct hi *lhi = lhs, *rhi = rhs;
  return strcmp(lhi->elemGI, rhi->elemGI);
}

static int store_attr(ESIS_Parser pe)
{
  int ch;
  ref r = TOP();
  FILE *fp = pe->infp;
  
  while ((ch = getc(fp)) != EOF) {
    if (ch == '\n' || ch == ' ')
      break;
    PUSH_CHAR(ch);
  }
  if (ch != ' ') {
    RELEASE(r);
    return ESIS_ERROR_SYNTAX;
  }
  
  while ((ch = getc(fp)) != EOF)
    if (ch == '\n' || ch == ' ')
      break;
  if (ch != ' ') {
    RELEASE(r);
    return ESIS_ERROR_SYNTAX;
  }
  
  PUSH_CHAR('\0');
  while ((ch = getc(fp)) != EOF) {
    if (ch == '\n')
      break;
    PUSH_CHAR(ch);
  }
  PUSH_CHAR('\0');
  return ESIS_ERROR_NONE;
}

static int store_name(ESIS_Parser pe)
{
  int ch;
  FILE *fp = pe->infp;
  ref r = TOP();
  
  if ((ch = getc(fp)) != EOF) {
    if (ch == '\n')
      return ESIS_ERROR_SYNTAX;
    do
      PUSH_CHAR(ch);
    while ((ch = getc(fp)) != EOF && ch != '\n');
  }
  PUSH_CHAR('\0');
  return ESIS_ERROR_NONE;
}

static void put_cdata(FILE *fp, const byte *data, size_t len)
{
  unsigned k;
  
  putc('-', fp);
  
  for (k = 0; k < len; ++k) {
    byte b = data[k];
    if (b >= 0x20)
      putc(b, fp);
    else if (b == '\n') {
      fputs("\\012\\n", fp);
    } else {   
      fprintf(fp, "\\%.3o", b);
    } 
  }
  
  putc('\n', fp);
}

#define DIG_MAX 7 /* Enough for 1,114,112 UCS code points. */

static size_t store_cdata(ESIS_Parser pe)
{
  int ch;
  FILE *fp = pe->infp;
  size_t n = 0U;
  unsigned num, ndig;
  char dig[DIG_MAX];
  
  while ((ch = getc(fp)) != EOF) {
    if (ch == '\n')
      break;
    else if (ch == '\\') {
      ch = getc(fp);
      switch (ch) {
       case EOF:
       case '\n':
         return n;
       case 'n':
         PUSH_CHAR('\n'), ++n;
         break;
       case '0': case '1': case '2': case '3':
       case '4': case '5': case '6': case '7':
         num = 0;
         ndig = 0;
         do {
           num = 8 * num + (ch - '0');
           dig[ndig++] = ch;
           if (ndig == 3) break;
         } while ((ch = getc(fp)) != EOF && '0' <= ch && ch <= '7');
         if (num != '\012') /* Ignore RS character. */
           PUSH_CHAR(num & 0xFF), ++n;
         break;
       case '#':
         num = 0;
         ndig = 0;
         while ((ch = getc(fp)) != EOF && '0' <= ch && ch <= '9') {
           num = 10 * num + (ch - '0');
           dig[ndig++] = ch;
           if (ndig == DIG_MAX) break;
         }
         if (ch == ';')
           PUSH_CHAR(num & 0xFF), ++n; /* :TODO: num -> UTF-8 */
         else {
           unsigned k;
           PUSH_CHAR('\\'), ++n;
           PUSH_CHAR('#'), ++n;
           for (k = 0U; k < ndig; ++k)
             PUSH_CHAR(dig[k]), ++n;
           PUSH_CHAR(';'), ++n;
         }
       default:
         PUSH_CHAR(ch), ++n;
      }
    } else
      PUSH_CHAR(ch), ++n;
  }
  return n;
}

#ifndef NDEBUG
#define FRAMETOKEN     0xfeedU
#define STACKTOKEN     0xbeefU
#define DECL_TOKEN     unsigned short token;
#else
#define DECL_TOKEN    
#endif

#define CHECK_STACK    assert(frame.token == STACKTOKEN)
#define CHECK_FRAME    assert(frame.token == FRAMETOKEN)
#define SET_STACK      assert(frame.token =  STACKTOKEN)
#define SET_FRAME      assert(frame.token =  FRAMETOKEN)


static void ParseLoop(ESIS_Parser pe)
{
  int ch;
  FILE *fp = pe->infp;
  FILE *outfp = pe->outfp;
  int err = ESIS_ERROR_NONE;
  const byte *data;
  size_t len;
  const char **atts;
  unsigned short n_att; /* Number of attributes collected. */
  ref r;
  
  static const ESIS_Char *const null_atts[2] = { NULL, NULL };
  /*
   * After the attributes and the GI are copied from the input
   * onto the S stack, we push information about the now open
   * element above it, in a "frame" that also links back to
   * the enclosing element: the "frame" structure for this one
   * is right in front of r_att.
   */
  struct fr {
    DECL_TOKEN
    unsigned short  n_att;  /* Number of attributes. */
    ref             r_att;  /* Start of pushed attributes. */
    ref             r_gi;   /* Start of pushed GI. */
    void           *userData;
    long            elemID;
    ESIS_Elem       elem;
    ESIS_ElementHandler handler;
  } frame;
  
  const size_t frsize = sizeof frame;
  
  static const struct fr null_frame = {
#ifndef NDEBUG
    0U, /* TOKEN */
#endif
    0, 0, 0, NULL, -1L,
    { NULL, NULL, 0U },
    (ESIS_ElementHandler)NULL
  };

  frame = null_frame;
  n_att = 0U;
  SET_FRAME;
  
  while ((ch = getc(fp)) != EOF) {
    
    switch (ch) {
      case '?':
        /* :TODO: PI - Store for handler ? */
        while ((ch = getc(fp)) != EOF)
          if (ch == '\n')
            break;
          else if (outfp != NULL)
            putc(ch, outfp);
        break;

      case 'A':
          /*
           * Push frame of enclosing element.
           */
           
        CHECK_FRAME;
        if (n_att == 0) {
          SET_STACK;
          r = esisStackPush(pe->S, &frame, frsize);
          frame = null_frame;
          SET_FRAME;
          frame.r_att = r;
        } else
          r = TOP();
        
        err = store_attr(pe);
        ERROR_SET(err);
        if (err) {
          RELEASE(r);
          if (n_att == 0) {
            CHECK_FRAME;
            r = esisStackPop(pe->S, &frame, frsize);
            CHECK_STACK; SET_FRAME;
          }
        } else
          ++n_att;
        break;

      case '(':
 
        CHECK_FRAME;
        if (n_att == 0) {
          SET_STACK;
          r = esisStackPush(pe->S, &frame, frsize);
          frame = null_frame;
          SET_FRAME;
          frame.r_att = r;
        }
        frame.n_att = n_att;
        
        frame.r_gi = TOP();
        err = store_name(pe);
        ERROR_SET(err);
        if (!err) {
          struct hi hi, *p_hi;
          
          hi.elemGI = P(frame.r_gi);
          p_hi = HANDLER;
          p_hi = bsearch(&hi, p_hi, pe->n_hi, sizeof hi, cmp_hi);
          
          if (p_hi != NULL || pe->handler != NULL) {
            ESIS_ElementHandler handler;
            void *userData;
            long elemID;
            
            ref r_atts = TOP();
            
            if (p_hi != NULL) {
              handler  = p_hi->handler;
              userData = p_hi->userData;
              elemID   = p_hi->elemID;
            } else {
              handler  = pe->handler;
              userData = pe->userData;
              elemID   = pe->elemID;
            }
            
            frame.handler  = handler;
            frame.userData = userData;
            frame.elemID   = elemID;
            
            frame.elem.userData = 0U;
            
            atts = (n_att > 0) ?
                                ESIS_Atts_(pe, n_att, frame.r_att)
                              : null_atts;
            
            frame.elem.atts   = atts;
            frame.elem.elemGI = P(frame.r_gi);
            
            handler(userData, ESIS_START, elemID, &frame.elem, NULL, 0U);
            
            RELEASE(r_atts);
          } else if (outfp != NULL) {
            unsigned k;
            char *p = P(frame.r_att);
            
            for (k = 0; k < n_att; ++k) {
               char *q = p + strlen(p) + 1;
               fprintf(outfp, "A%s CDATA %s\n", p, q);
               p = q + strlen(q) + 1;
            }
            p = P(frame.r_gi);
            fprintf(outfp, "(%s\n", p);
          }
          n_att = 0U;
        }
        break;

      case '-':
        r = TOP();
        len = store_cdata(pe);
        data = P(r);
        if (frame.handler != NULL && len > 0U) {
          CHECK_FRAME;
          frame.elem.elemGI = P(frame.r_gi);
          frame.elem.atts   = NULL;
          frame.handler(frame.userData, ESIS_CDATA,
                        frame.elemID, &frame.elem, data, len);
        } else if (outfp != NULL)
          put_cdata(outfp, data, len);
          
        RELEASE(r);
        break;       
        
      case ')':
        r = TOP();
        store_name(pe);
        if (frame.handler != NULL) {
          CHECK_FRAME;
          frame.elem.elemGI = P(frame.r_gi);
          frame.elem.atts   = NULL;
          frame.handler(frame.userData, ESIS_END,
                        frame.elemID, &frame.elem, NULL, 0U);
        } else if (outfp != NULL)
          fprintf(outfp, ")%s\n", P(frame.r_gi));
          
        RELEASE(r);
        /*
         * Pop frame of closed element, get outer element info in frame.
         */
        r = frame.r_att;
        RELEASE(r);
        CHECK_FRAME;
        if (r >= frsize) {
          r = esisStackPop(pe->S, &frame, frsize);
          CHECK_STACK;
          SET_FRAME;
        } else {
          frame = null_frame;
          SET_FRAME;
        }
        break;
               
      default:
        ;
    }
  }
}

int ESISAPI
ESIS_ParseFile(ESIS_Parser pe, FILE *inputFile)
{
  unsigned n_hi = pe->n_hi;
  struct hi *p_hi = HANDLER;
  
  if (n_hi > 0U) {
    unsigned k;
    const char *hdbuf = pe->HD->buf;
    
    for (k = 0; k < n_hi; ++k)
      p_hi[k].elemGI = hdbuf + p_hi[k].r_ElemGI;
      
    qsort(p_hi, n_hi, sizeof p_hi[0], cmp_hi);
  }
  
  pe->infp  = inputFile;
  pe->outfp = NULL;
  
  ParseLoop(pe);
  
  ERROR_GET();
  return pe->err == ESIS_ERROR_NONE;
}

ESIS_FilterFile(ESIS_Parser pe, FILE *inputFile, FILE *outputFile)
{
  unsigned n_hi = pe->n_hi;
  struct hi *p_hi = HANDLER;
  
  if (n_hi > 0U) {
    unsigned k;
    const char *hdbuf = pe->HD->buf;
    
    for (k = 0; k < n_hi; ++k)
      p_hi[k].elemGI = hdbuf + p_hi[k].r_ElemGI;
      
    qsort(p_hi, n_hi, sizeof p_hi[0], cmp_hi);
  }
  
  pe->infp  = inputFile;
  pe->outfp = outputFile;
  
  ParseLoop(pe);
  
  ERROR_GET();
  return pe->err == ESIS_ERROR_NONE;
}

#ifndef NDEBUG /* NOT IMPLEMENTED */
int ESISAPI
ESIS_Parse(ESIS_Parser pe, const char *s, size_t len, int isFinal)
{
  return 0;
}
#endif


#ifndef NDEBUG /* NOT IMPLEMENTED */
void * ESISAPI
ESIS_GetBuffer(ESIS_Parser pe, size_t len)
{
  return NULL;
}
#endif


#ifndef NDEBUG /* NOT IMPLEMENTED */
int ESISAPI
ESIS_ParseBuffer(ESIS_Parser pe, size_t len, int isFinal)
{
  return 0;
}
#endif


ESIS_Parser ESISAPI
ESIS_ParserCreate(const ESIS_Char *encoding)
{
  ESIS_Parser pe = malloc(sizeof *pe);
  if (pe == NULL) return NULL;
  
  memset(pe, 0, sizeof *pe);
  
  esisStackInit(pe->HD);
  if (pe->HD->buf == NULL) goto fail;
  
  esisStackInit(pe->HI);
  if (pe->HI->buf == NULL) goto fail;
  
  esisStackInit(pe->S);
  if (pe->S->buf == NULL) goto fail;
  
  pe->err    = ESIS_ERROR_NONE;
  pe->n_hi   = 0U;
  
  pe->handler = NULL;
  pe->infp    = NULL;
  pe->outfp   = NULL;
  
  return pe;
  
fail:
  free(pe->HI->buf);
  free(pe->HD->buf);
  free(pe->S->buf);
  free(pe);
  return NULL;
}


void ESISAPI ESIS_ParserFree(ESIS_Parser pe)
{
  free(pe->S->buf);
  free(pe);
}


enum ESIS_Error ESISAPI ESIS_GetParserError(ESIS_Parser pe)
{
  ERROR_SET(ESIS_ERROR_NONE);
  return pe->err;
}


const char **ESIS_Atts_(ESIS_Parser pe, unsigned n_att, ref r_att)
{
  unsigned k;
  size_t n;
  ref r_atts;
  const ESIS_Char *p, **pp;
  const char **atts;
  
  n = (2 * n_att + 1 ) * sizeof p;
  r_atts = MARK(n + sizeof p);
  n = r_atts - r_atts % sizeof p + sizeof p;
  
  pp = P(n);
  p  = P(r_att); 
  
  atts = pp;
  
  for (k = 0; k < n_att; ++k) {
    const ESIS_Char *name, *val;
    
    name = p;
    p += strlen(p) + 1;
    
    val = p;
    p += strlen(p) + 1;
    
    *pp++ = name;
    *pp++ = val;
  }
  *pp = NULL;
  
  return atts;
}
