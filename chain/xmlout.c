/* xmlout.c */

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

struct {
    const char *elemGI;
    long elemID;
} elems[] = {
    "none",		CMARK_NODE_NONE,
    "document",		CMARK_NODE_DOCUMENT,
    "block_quote",	CMARK_NODE_BLOCK_QUOTE,
    "list",		CMARK_NODE_LIST,
    "item",		CMARK_NODE_ITEM,
    "code_block",	CMARK_NODE_CODE_BLOCK,
    "html",		CMARK_NODE_HTML,
    "paragraph",	CMARK_NODE_PARAGRAPH,
    "header",		CMARK_NODE_HEADER,
    "hrule",		CMARK_NODE_HRULE,
    "text",		CMARK_NODE_TEXT,
    "softbreak",	CMARK_NODE_SOFTBREAK,
    "code",		CMARK_NODE_CODE,
    "inline_html",	CMARK_NODE_INLINE_HTML,
    "emph",		CMARK_NODE_EMPH,
    "strong",		CMARK_NODE_STRONG,
    "link",		CMARK_NODE_LINK,
    "image",		CMARK_NODE_IMAGE,
    NULL,		0L
};

struct trans xmltab[NELEM] = {
  { "none",           NULL,   EL_OMIT                    },
  { "document",       NULL,   0                          },
  { "block_quote",    NULL,   0                          },
  { "list",           NULL,   0                          },
  { "item",           NULL,   0                          },
  { "code_block",     NULL,   EL_PCDATA,                 },
  { "html",           NULL,   EL_CDATA | EL_OMIT,        },
  { "paragraph",      NULL,   0,                         },
  { "header",         NULL,   0,                         },
  { "hrule",          NULL,   EL_EMPTY,                  },
  { "text",           NULL,   EL_PCDATA | EL_OMIT,       },
  { "softbreak",      NULL,   EL_EMPTY,                  },
  { "linebreak",      NULL,   EL_EMPTY,                  },
  { "code",           NULL,   EL_PCDATA,                 },
  { "inline_html",    NULL,   EL_CDATA | EL_OMIT,        },
  { "emph",           NULL,   0,                         },
  { "strong",         NULL,   0,                         },
  { "link",           NULL,   0,                         },
  { "image",          NULL,   0,                         },
};


ESIS_Bool handler(void               *userData,
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

int main(int argc, char *argv)
{
  int i;
  
  parser = ESIS_ParserCreate(NULL);
  writer = ESIS_XmlWriterCreate(stdout, ESIS_CANONICAL);
  
  for (i = 0; elems[i].elemGI != NULL; ++i)
    ESIS_SetElementHandler(parser, handler,
	                   elems[i].elemGI, elems[i].elemID,
			   writer);
			   
  if (ESIS_ParseFile(parser, stdin) == 0) {
    ESIS_Error err = ESIS_GetParserError(parser);
    fprintf(stderr, "ESIS_Error: %d\n", (int)err);
  }
  
  ESIS_WriterFree(writer);
  ESIS_ParserFree(parser);
  return 0;
}
