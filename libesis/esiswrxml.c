/* esiswrxml.c */

#include "esisio.h"
#include "esisio_int.h"
#include <stdlib.h>
#include <string.h>

static void
EsisXmlTagWriter(FILE *, unsigned, const ESIS_Char *, const ESIS_Char **);
static void
EsisXmlDataWriter(FILE *, unsigned, const byte *, size_t);

ESIS_Writer ESISAPI
ESIS_XmlWriterCreate(FILE *fp, const ESIS_Char *encoding)
{
  ESIS_Writer pe = ESIS_WriterCreateInt(fp, encoding);
  
  pe->tagfunc  = EsisXmlTagWriter;
  pe->datafunc = EsisXmlDataWriter;
  
  return pe;
}

/*====================================================================*/

static void 
WriteCDATA(FILE *outputFile, const byte *data, size_t len)
{
  unsigned k;
  
  for (k = 0; k < len; ++k) {
    byte b = data[k];
    if (b < 0x20 && b != '\n') 
      fprintf(outputFile, "&#%u;", b);
    else
      putc(b, outputFile);
  }
}

static void 
WritePCDATA(FILE *outputFile, const byte *data, size_t len)
{
  unsigned k;
  
  for (k = 0; k < len; ++k) {
    byte b = data[k];
    const char *ent = NULL;
    char buf[8];
    
    if (b < 0x20 && b != '\n')
      sprintf(buf, "&#%u;", b), ent = buf;
    else switch (b) {
      case '<':  ent = "&lt;";   break;
      case '>':  ent = "&gt;";   break;
      case '"':  ent = "&quot;"; break;
      case '&':  ent = "&amp;";  break;
      case '\n': /* &#10; for Canonical XML, but not here ... */
      default: 
        putc(b, outputFile);
        continue;
    }
    fputs(ent, outputFile);
  }
}

static void ESISAPI
EsisXmlTagWriter(FILE             *outputFile,
                 unsigned          what,
                 const ESIS_Char  *elemGI,
                 const ESIS_Char **atts)
{
  unsigned k;
  
  switch (what) {
  case ESIS_START_ :
  case ESIS_EMPTY_ :
    fprintf(outputFile, "<%s", elemGI);
    if (atts != NULL) for (k = 0; atts[k] != NULL; k += 2) {
      putc(' ', outputFile);
      WritePCDATA(outputFile, atts[k],   strlen(atts[k]));
      putc('=', outputFile);
      putc('"', outputFile);
      WritePCDATA(outputFile, atts[k+1], strlen(atts[k+1]));
      putc('"', outputFile);
    }
      
    if (what == ESIS_EMPTY_)
      putc('/', outputFile);
      
    putc('>', outputFile);
    break;
    
  case ESIS_END_ :
    fprintf(outputFile, "</%s>", elemGI);
    break;
  }
}

static void ESISAPI
EsisXmlDataWriter(FILE             *outputFile,
                  unsigned         how,
                  const byte      *data,
                  size_t           len)
{
  switch (how) {
  case ESIS_PCDATA_ : WritePCDATA(outputFile, data, len); break;
  case ESIS_CDATA_  : WriteCDATA (outputFile, data, len); break;
  }
}