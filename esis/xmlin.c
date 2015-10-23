/* This is simple demonstration of how to use expat. This program
   reads an XML document from standard input and writes 
*/

#include <stdio.h>
#include "esisio.h"
#include "xmlparse.h"

#ifndef XMLCALL
#define XMLCALL ESISCALL
#endif

#if defined(__amigaos__) && defined(__USE_INLINE__)
#include <proto/expat.h>
#endif

#ifdef XML_LARGE_SIZE
#if defined(XML_USE_MSC_EXTENSIONS) && _MSC_VER < 1400
#define XML_FMT_INT_MOD "I64"
#else
#define XML_FMT_INT_MOD "ll"
#endif
#else
#define XML_FMT_INT_MOD "l"
#endif

struct state {
  int level;
  size_t ncdata;
};

static void XMLCALL
startElement(void *userData, const char *name, const char **atts)
{
  int i;
  struct state *sp = userData;
  
  if (sp->level == 0)
    puts("?xml version=\"1.0\" encoding=\"UTF-8\"");
    
  if (sp->ncdata > 0U)
    putchar('\n');
  
  for (i = 0; atts[i]; i += 2) {
    printf("A%s CDATA %s\n", atts[i], atts[i+1]);
  }
  printf("(%s\n", name);
  
  sp->ncdata = 0U;
  sp->level++;
}

static void XMLCALL
characterData(void *userData, const XML_Char *s, int len)
{
  int i;
  struct state *sp = userData;
  
  if (len == 0) return;
     
  if (sp->ncdata == 0) putchar('-');
  
  for (i = 0; i < len; ++i)
    if (s[i] == '\n') {
	putchar('\\');
	putchar('n');
	sp->ncdata += 2;
	putchar('\\');
	putchar('0');
	putchar('1');
	putchar('2');
	sp->ncdata += 4;
    } else if (s[i] == '\\') {
	putchar('\\');
	putchar('\\');
	sp->ncdata += 2;
    } else if (s[i] == '\r') {
	putchar('\\');
	putchar('0');
	putchar('1');
	putchar('2');
	sp->ncdata += 4;
    } else {
	putchar(s[i]);
	sp->ncdata += 1;
    }
}

static void XMLCALL
endElement(void *userData, const char *name)
{
  struct state *sp = userData;
  
  if (sp->ncdata > 0U) putchar('\n');
  
  printf(")%s\n", name);
  sp->level--;
  sp->ncdata = 0U;
}

int
main(int argc, char *argv[])
{
  char buf[BUFSIZ];
  XML_Parser parser = XML_ParserCreate(NULL);
  int done;
  struct state s;
  
  XML_SetUserData(parser, &s);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser,
                              characterData);
  s.level = 0;
  s.ncdata = 0U;
  
  do {
    int len = (int)fread(buf, 1, sizeof(buf), stdin);
    done = len < sizeof(buf);
    if (XML_Parse(parser, buf, len, done) != XML_ERROR_NONE) {
      fprintf(stderr,
              "%s at line %" XML_FMT_INT_MOD "u\n",
              XML_ErrorString(XML_GetErrorCode(parser)),
              XML_GetCurrentLineNumber(parser));
      return 1;
    }
  } while (!done);
  
  if (s.ncdata > 0) putchar('\n');
  if (s.level == 0) puts("C");
  
  XML_ParserFree(parser);
  return 0;
}
