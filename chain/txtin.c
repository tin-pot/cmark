/* txtin.c */

#include <stdio.h>
#include <string.h>
#include <esisio.h>

ESIS_Writer writer;

#define LINELEN 32

static const struct def {
  const char *start_pattern;
  const char *end_pattern;
  const char *syntax;
} defs[] = {
  { "+-- ", "---\n", "z" },
  { "+== ", "-==\n", "z" },
  { "+.. ", "-..\n", "z" },
  { "%%Z",  "%%\n",  "z" },
  { "```", "```\n",  NULL },
  { NULL, NULL, NULL }
};

static const char *pline = NULL; /* HACK */

int start(const char *line)
{
  int i;

  pline = line;
  for (i = 0; defs[i].end_pattern != NULL; ++i) {
    size_t n = strlen(defs[i].start_pattern);
    if (strncmp(line, defs[i].start_pattern, n) == 0)
      return i;
  }
  return -1;
}

const char *syntax(int i)
{
  size_t n, m;
  static char buf[32];

  if (defs[i].syntax != NULL)
    return defs[i].syntax;
  n = strspn(pline, "` \t");
  if (pline[n] == '\0')
    return "code";
  
  m = strcspn(pline + n, "\n\t ");
  if (m + 1 > sizeof buf)
    m = sizeof buf - 1;
  strncpy(buf, pline + n, m);
  return buf;
}

int end(int i, const char *line)
{
  size_t n = strlen(defs[i].end_pattern);
  return strncmp(line, defs[i].end_pattern, n) == 0;
}

void pump(void)
{
  char buffer[BUFSIZ+1];

  while (fgets(buffer, sizeof buffer, stdin) != NULL) {
    int isyntax = start(buffer);
    if (isyntax < 0)
      ESIS_Cdata(writer, buffer, ESIS_NTS);
    else {
      ESIS_Attr(writer, "syntax", syntax(isyntax), ESIS_NTS);
      ESIS_Attr(writer, "mode", "block", ESIS_NTS);
      ESIS_Start(writer, "mark-up", NULL);
      ESIS_Cdata(writer, buffer, ESIS_NTS);
      while (fgets(buffer, sizeof buffer, stdin) != NULL) {
	ESIS_Cdata(writer, buffer, ESIS_NTS);
	if (end(isyntax, buffer)) break;
      }
      ESIS_End(writer, "mark-up");
    }
  }
}

int main(void)
{
  const char *atts[] = {
    "syntax", "cmark",
    "mode",   "block",
    "label",  "typescript",
    NULL
  };

  writer = ESIS_WriterCreate(stdout, 0U);
  ESIS_Start(writer, "mark-up", atts);
  pump();
  ESIS_End(writer, "mark-up");

  ESIS_WriterFree(writer);

  return 0;
}
