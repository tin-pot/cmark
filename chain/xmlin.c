/* xmlin.c */

#ifdef _MSC_VER
#define _CRT_DISABLE_PERFCRIT_LOCKS
#endif

#include <stdio.h>
#include <esisio.h>
#include <xmlparse.h>

int
main(int argc, char *argv[])
{
  int done;
  char buf[BUFSIZ];
  FILE *infile, *outfile;
  
  XML_Parser parser = XML_ParserCreate(NULL);
  ESIS_Writer writer = ESIS_WriterCreate(stdout, NULL);
  
  if (argc == 2) {
    infile = fopen(argv[1], "r");
    if (infile == NULL) {
      perror(argv[1]);
      return 1;
    }
  } else 
    infile = stdin;
    
  outfile = stdout;
  
  XML_SetUserData(parser, writer);
  XML_SetElementHandler(parser,
      ESIS_Start,
      ESIS_End);
  XML_SetCharacterDataHandler(parser,
      (XML_CharacterDataHandler)ESIS_Cdata);
  
  do {
    size_t len = fread(buf, 1, sizeof(buf), infile);
    done = len < sizeof(buf);
    
    if (XML_Parse(parser, buf, (int)len, done) == 0) {
      fprintf(stderr,
              "%s at line %d\n",
              XML_ErrorString(XML_GetErrorCode(parser)),
              XML_GetCurrentLineNumber(parser));
      return 1;
    }
  } while (!done);
  
  XML_ParserFree(parser);
  ESIS_WriterFree(writer);
  
  return 0;
}
