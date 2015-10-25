/* xmlout.c */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esisio.h>
#include <cmark.h>

#define NELEM 19

#define EL_EMPTY   0001
#define EL_CDATA   0002
#define EL_PCDATA  0004
#define EL_OMIT    0010

#define ATTR_MAX    128
#define BUF_SIZE   4096
#define NAME_SIZE   128

#define RE_CHAR '\xA'
#define RS_CHAR '\0'

struct trans {
  const char *ingi;
  const char *outgi;
  unsigned flags;
};

ESIS_Parser parser;
ESIS_Writer writer;
/*
 *  xml & ~trans => CommonMark XML native
 *  xml &  trans => XHTML
 * ~xml &  trans => HTML
 * ~xml & ~trans => CommonMark SGML native
 */
ESIS_Bool   xml   = ESIS_TRUE;
ESIS_Bool   trans = ESIS_TRUE;

struct {
    char elemGI[12];
    long elemID;
    unsigned prop;
} elems[] = {
    "none",		CMARK_NODE_NONE,          EL_OMIT,
    "document",		CMARK_NODE_DOCUMENT,      0,
    "block_quote",	CMARK_NODE_BLOCK_QUOTE,   0,
    "list",		CMARK_NODE_LIST,          0,
    "item",		CMARK_NODE_ITEM,          0,
    "code_block",	CMARK_NODE_CODE_BLOCK,    EL_PCDATA,
    "html",		CMARK_NODE_HTML,          EL_CDATA | EL_OMIT,
    "paragraph",	CMARK_NODE_PARAGRAPH,     0,
    "header",		CMARK_NODE_HEADER,        0,
    "hrule",		CMARK_NODE_HRULE,         EL_EMPTY,
    "text",		CMARK_NODE_TEXT,          EL_PCDATA,
    "softbreak",	CMARK_NODE_SOFTBREAK,     EL_EMPTY,
    "linebreak",	CMARK_NODE_LINEBREAK,     EL_EMPTY,
    "code",		CMARK_NODE_CODE,          EL_PCDATA,
    "inline_html",	CMARK_NODE_INLINE_HTML,   EL_CDATA | EL_OMIT,
    "emph",		CMARK_NODE_EMPH,          0,
    "strong",		CMARK_NODE_STRONG,        0,
    "link",		CMARK_NODE_LINK,          0,
    "image",		CMARK_NODE_IMAGE,         EL_EMPTY,
    "*",		-1L,                      0,
};

char HTML_GI[][12] = {
    "",
    "BODY",
    "BLOCKQUOTE",
    "UL",
    "LI",
    "CODE",
    "",
    "P",
    "H1",
    "HR",
    "SPAN",
    "BR",
    "BR",
    "CODE",
    "",
    "EM",
    "STRONG",
    "A",
    "IMG",
    "*",
};

ESIS_Bool def_handler(void               *userData,
                      ESIS_ElemEvent      elemEvent,
                      long                elemID,
                      const  ESIS_Elem   *elem,
                      const  ESIS_Char   *charData,
                      size_t              len)
{
  ESIS_Writer w = userData;
  
  switch (elemEvent) {
  case ESIS_START: ESIS_Start(w, elem->elemGI, elem->atts); break;
  case ESIS_CDATA: ESIS_PCdata(w, charData, len);           break;
  case ESIS_END:   ESIS_End(w, elem->elemGI);               break;
  }   
  return ESIS_TRUE;
}

static const char *findatt(const char **atts, const char *name)
{
  unsigned k;
  
  for (k = 0; atts[k] != NULL; k+=2)
    if (strcmp(atts[k], name) == 0)
      return atts[k+1];
  return NULL;
}

ESIS_Bool handler(void               *userData,
                  ESIS_ElemEvent      elemEvent,
                  long                elemID,
                  const  ESIS_Elem   *elem,
                  const  ESIS_Char   *charData,
                  size_t              len)
{
  ESIS_Writer w = userData;
  unsigned prop = elems[elemID].prop;
  const char *val, *val2;
  const char *atta[8];
  char outGI[14];
  const char **atts = (trans) ? atta : elem->atts;
  
  strcpy(outGI, (trans) ? HTML_GI[elemID] : elem->elemGI);
  atta[0] = NULL;
  
  switch (elemEvent) {
  case ESIS_START:
    if ((prop & EL_OMIT) == 0) {
      if (trans) switch (elemID) {
        case CMARK_NODE_LIST:
          if ((val = findatt(elem->atts, "type")) && !strcmp(val, "ordered"))
            strcpy(outGI, (xml) ? "ol" : "OL");
          break;
          
        case CMARK_NODE_CODE_BLOCK:
          ESIS_Start(w, "PRE", NULL);
          break;
          
        case CMARK_NODE_HEADER:
          if ((val = findatt(elem->atts, "level")) != NULL) 
            outGI[1] = val[0];
          break;
          
        case CMARK_NODE_SOFTBREAK:
          ESIS_Cdata(w, "\n", 1);
          return ESIS_TRUE;
          
        case CMARK_NODE_LINK:
        case CMARK_NODE_IMAGE:
          val = findatt(elem->atts, "destination");
          val2 = findatt(elem->atts, "title");
          if (val != NULL && val2 != NULL) {
            atta[0] = "href";
            atta[1] = val;
            atta[2] = "title";
            atta[3] = val2;
            atta[4] = NULL;
          }
        default:
          ;
      }
      
      if ((prop & EL_EMPTY) != 0)
        ESIS_Empty(w, outGI, atts);
      else
        ESIS_Start(w, outGI, atts);
    }
    break;
    
  case ESIS_CDATA:
    if ((prop & EL_CDATA) != 0)
      ESIS_Cdata(w, charData, len);
    else
      ESIS_PCdata(w, charData, len);
    break;
    
  case ESIS_END:
    if ((prop & EL_OMIT) == 0) 
      if ((prop & EL_EMPTY) == 0)
        ESIS_End(w, outGI);
    break;
  }   
  return ESIS_TRUE;
}

int main(int argc, char *argv[])
{
  int i, j;
  unsigned options = 0U;
  
  xml = ESIS_TRUE, trans = ESIS_FALSE;
  
  if (argc == 2)
    if (strcmp(argv[1], "-sgml") == 0)
      xml = ESIS_FALSE, trans = ESIS_FALSE;
    else if (strcmp(argv[1], "-html") == 0)
      xml = ESIS_FALSE, trans = ESIS_TRUE;
    else if (strcmp(argv[1], "-xml") == 0)
      xml = ESIS_TRUE, trans = ESIS_FALSE;
    else if (strcmp(argv[1], "-xhtml") == 0)
      xml = ESIS_TRUE, trans = ESIS_TRUE;
    else {
      fputs("Usage: argv[0] [-sgml | -html | -xml | -xhtml]\n", stderr);
      return 1;
    }
    
  if (!xml)
    options = ESIS_SGML;
  else
    for (i = 0; HTML_GI[i][0] != '*'; ++i)
      for (j = 0; HTML_GI[i][j] != '\0'; ++j)
        HTML_GI[i][j] = tolower(HTML_GI[i][j]);
    
  if (trans)
    elems[CMARK_NODE_TEXT].prop |= EL_OMIT;
  
  parser = ESIS_ParserCreate(NULL);
  writer = ESIS_XmlWriterCreate(stdout, options);
  
  ESIS_SetElementHandler(parser, def_handler, NULL, -1L, writer);
  
  for (i = 0; elems[i].elemID >= 0L; ++i)
    ESIS_SetElementHandler(parser, handler,
	                      elems[i].elemGI, elems[i].elemID, writer);
                         
  if (ESIS_ParseFile(parser, stdin) == 0) {
    ESIS_Error err = ESIS_GetParserError(parser);
    fprintf(stderr, "ESIS_Error: %d\n", (int)err);
  }
  
  ESIS_WriterFree(writer);
  ESIS_ParserFree(parser);
  return 0;
}
