#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "cmark.h"
#include "bench.h"

#include "repourl.h"
#include "gitident.h"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#endif


void print_usage() {
  printf("Usage:   cm2html [FILE*]\n");
  printf("Options:\n");
  printf("  -t --title TITLE Set the document title\n");
  printf("  --sourcepos      Include source position attribute\n");
  printf("  --hardbreaks     Treat newlines as hard line breaks\n");
  printf("  --safe           Suppress raw HTML and dangerous URLs\n");
  printf("  --smart          Use smart punctuation\n");
  printf("  --normalize      Consolidate adjacent text nodes\n");
  printf("  --help, -h       Print usage information\n");
  printf("  --version        Print version\n");
}

static void print_document(cmark_node *document,
                           int options, const char *title) {
  char *result;

  puts("<!DOCTYPE HTML PUBLIC \"ISO/IEC 15445:2000//DTD HTML//EN\">");
  puts("<HTML>");
  puts("<HEAD>");
  puts("  <META name=\"GENERATOR\" content=\"cmark "
       CMARK_VERSION_STRING
       " (" REPOURL " " GITIDENT ")\">");
  puts("  <META http-equiv=\"Content-Type\"\n"
       "        content=\"text/html; charset=UTF-8\">");
  puts("  <LINK rel=\"stylesheet\"\n"
       "        type=\"text/css\"\n"
       "        href=\"default.css\">");
  printf("  <TITLE>%s</TITLE>\n", title);
  puts("</HEAD>");
  puts("<BODY>");

  result = cmark_render_html(document, options);
  printf("%s", result);
  free(result);
  puts("</BODY>");
  puts("</HTML>");
}

int main(int argc, char *argv[]) {
  int i, numfps = 0;
  int *files;
  char buffer[4096];
  const char *title = "Untitled Document";
  cmark_parser *parser;
  size_t bytes;
  cmark_node *document;
  int options = CMARK_OPT_DEFAULT | CMARK_OPT_ISO;

#if defined(_WIN32) && !defined(__CYGWIN__)
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  files = (int *)malloc(argc * sizeof(*files));

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("cmark %s", CMARK_VERSION_STRING
	                               " (" REPOURL " " GITIDENT ")\n");
      printf(" - CommonMark converter\n(C) 2014, 2015 John MacFarlane\n");
      exit(0);
    } else if ((strcmp(argv[i], "--title") == 0) ||
               (strcmp(argv[i], "-t") == 0)) {
      title = argv[++i];
    } else if (strcmp(argv[i], "--sourcepos") == 0) {
      options |= CMARK_OPT_SOURCEPOS;
    } else if (strcmp(argv[i], "--hardbreaks") == 0) {
      options |= CMARK_OPT_HARDBREAKS;
    } else if (strcmp(argv[i], "--smart") == 0) {
      options |= CMARK_OPT_SMART;
    } else if (strcmp(argv[i], "--safe") == 0) {
      options |= CMARK_OPT_SAFE;
    } else if (strcmp(argv[i], "--normalize") == 0) {
      options |= CMARK_OPT_NORMALIZE;
    } else if (strcmp(argv[i], "--validate-utf8") == 0) {
      options |= CMARK_OPT_VALIDATE_UTF8;
    } else if ((strcmp(argv[i], "--help") == 0) ||
               (strcmp(argv[i], "-h") == 0)) {
      print_usage();
      exit(0);
    } else if (*argv[i] == '-') {
      print_usage();
      exit(1);
    } else { // treat as file argument
      files[numfps++] = i;
    }
  }

  parser = cmark_parser_new(options);
  for (i = 0; i < numfps; i++) {
    FILE *fp = fopen(argv[files[i]], "r");
    if (fp == NULL) {
      fprintf(stderr, "Error opening file %s: %s\n", argv[files[i]],
              strerror(errno));
      exit(1);
    }

    start_timer();
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      cmark_parser_feed(parser, buffer, bytes);
      if (bytes < sizeof(buffer)) {
        break;
      }
    }
    end_timer("processing lines");

    fclose(fp);
  }

  if (numfps == 0) {

    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
      cmark_parser_feed(parser, buffer, bytes);
      if (bytes < sizeof(buffer)) {
        break;
      }
    }
  }

  start_timer();
  document = cmark_parser_finish(parser);
  end_timer("finishing document");
  cmark_parser_free(parser);

  start_timer();
  print_document(document, options, title);
  end_timer("print_document");

  start_timer();
  cmark_node_free(document);
  end_timer("free_blocks");

  free(files);

  return 0;
}
