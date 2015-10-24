/* xmlin.c */

#ifdef _MSC_VER
#define _CRT_DISABLE_PERFCRIT_LOCKS
#endif

#include <stdio.h>
#include <esisio.h>
#include <xmlparse.h>


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
  int       level;
  size_t    col;
  FILE     *infp;
  FILE     *outfp;
};

static void
startElement(void *userData, const char *name, const char **atts)
{
  int i;
  struct state *const sp = userData;
  FILE *const fp = sp->outfp;
  
  if (sp->level == 0)
    fputs("?xml version=\"1.0\" encoding=\"UTF-8\"", fp);
    
  if (sp->col > 0U)
    putc('\n', fp);
  
  for (i = 0; atts[i]; i += 2) {
    fprintf(fp, "A%s CDATA %s\n", atts[i], atts[i+1]);
  }
  fprintf(fp, "(%s\n", name);
  
  sp->col = 0U;
  sp->level++;
}

static void
characterData(void *userData, const XML_Char *s, int len)
{
  int i;
  struct state *sp = userData;
  FILE *fp = sp->outfp;
  
  if (len == 0) return;
     
  if (sp->col == 0) putc('-', fp);
  
  for (i = 0; i < len; ++i)
    if (s[i] == '\n') {
	putc('\\', fp);
	putc('n', fp);
	sp->col += 2;
	putc('\\', fp);
	putc('0', fp);
	putc('1', fp);
	putc('2', fp);
	sp->col += 4;
    } else if (s[i] == '\\') {
	putc('\\', fp);
	putc('\\', fp);
	sp->col += 2;
    } else if (s[i] == '\r') {
	putc('\\', fp);
	putc('0', fp);
	putc('1', fp);
	putc('2', fp);
	sp->col += 4;
    } else {
	putc(s[i], fp);
	sp->col += 1;
    }
}

static void
endElement(void *userData, const char *name)
{
  struct state *sp = userData;
  FILE *fp = sp->outfp;
  
  if (sp->col > 0U) putc('\n', fp);
  
  fprintf(fp, ")%s\n", name);
  sp->level--;
  sp->col = 0U;
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
  s.col   = 0U;
  s.infp  = stdin;
  s.outfp = stdout;
  
  do {
    int len = (int)fread(buf, 1, sizeof(buf), s.infp);
    done = len < sizeof(buf);
    if (XML_Parse(parser, buf, len, done) != XML_ERROR_NONE) {
      fprintf(stderr,
              "%s at line %" XML_FMT_INT_MOD "u\n",
              XML_ErrorString(XML_GetErrorCode(parser)),
              XML_GetCurrentLineNumber(parser));
      return 1;
    }
  } while (!done);
  
  if (s.col > 0) putc('\n', s.outfp);
  if (s.level == 0) fputs("C", s.outfp);
  
  XML_ParserFree(parser);
  return 0;
}
