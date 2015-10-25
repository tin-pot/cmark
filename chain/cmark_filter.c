#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <esisio.h>
#include "cmark_ctype.h"
#include "config.h"
#include "cmark.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"
#include "scanners.h"

extern const char cmark_gitident[];
extern const char cmark_repourl[];

static const char *GI[] = {
  "none",	"document",	"block_quote",	"list",
  "item",	"code_block",	"html",		"paragraph",
  "header",	"hrule",	"text",		"softbreak",
  "linebreak",	"code",		"inline_html",	"emph",
  "strong",	"link",		"image",
};

struct UserData {
  cmark_option_t options;
  cmark_parser  *parser;
  ESIS_Writer    writer;
};

static int S_render_node(cmark_node *node, cmark_event_type ev_type,
			 ESIS_Writer w)
{
  cmark_delim_type delim;
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  char buffer[100];

  if (entering) {
    switch (node->type) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
    case CMARK_NODE_HTML:
    case CMARK_NODE_INLINE_HTML:
      ESIS_Start(w, GI[node->type], NULL);
      ESIS_Cdata(w, node->as.literal.data, node->as.literal.len);
      ESIS_End(w,  GI[node->type]);
      break;

    case CMARK_NODE_LIST:
      switch (cmark_node_get_list_type(node)) {
      case CMARK_ORDERED_LIST:
        ESIS_Attr(w, "type", "ordered", ESIS_NTS); 
        sprintf(buffer, "%d", cmark_node_get_list_start(node));
        ESIS_Attr(w, "start", buffer, ESIS_NTS);
        delim = cmark_node_get_list_delim(node);
        ESIS_Attr(w, "delim", (delim == CMARK_PAREN_DELIM) ?
                              "paren" : "period", ESIS_NTS);
        break;
      case CMARK_BULLET_LIST:
        ESIS_Attr(w, "type", "bullet", ESIS_NTS);
        break;
      default:
        break;
      }
      ESIS_Attr(w, "tight", cmark_node_get_list_tight(node) ?
                                            "true" : "false", ESIS_NTS);
      ESIS_Start(w, GI[node->type], NULL);
      break;

    case CMARK_NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      ESIS_Attr(w, "level", buffer, ESIS_NTS);
      ESIS_Start(w, GI[node->type], NULL);
      break;

    case CMARK_NODE_CODE_BLOCK:
      if (node->as.code.info.len > 0)
        ESIS_Attr(w, "info", 
		       node->as.code.info.data, node->as.code.info.len);
      ESIS_Start(w, GI[node->type], NULL);
      ESIS_Cdata(w,
	         node->as.code.literal.data, node->as.code.literal.len);
      ESIS_End(w, GI[node->type]);
      break;

    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      ESIS_Attr(w, "destination", node->as.link.url.data, node->as.link.url.len);
      ESIS_Attr(w, "title", node->as.link.title.data, node->as.link.title.len);
      ESIS_Empty(w, GI[node->type], NULL);
      break;

    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      ESIS_Empty(w, GI[node->type], NULL);
      break;

    case CMARK_NODE_DOCUMENT:
    default:
      ESIS_Start(w, GI[node->type], NULL);
      break;
    } /* entering switch */
  } else if (node->first_child) { /* NOT entering */
    ESIS_End(w, GI[node->type]);
  }

  return 1;
}

char *cmark_render_esis(ESIS_Writer w, cmark_node *root)
{
  cmark_event_type ev_type;
  cmark_node *cur;
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node(cur, ev_type, w);
  }
  ESIS_WriterFree(w);
  cmark_iter_free(iter);
  return NULL;
}

void print_usage() {
  printf("Usage:   cmark_filter [FILE*]\n");
  printf("Options:\n");
  printf("  --sourcepos      Include source position attribute\n");
  printf("  --hardbreaks     Treat newlines as hard line breaks\n");
  printf("  --safe           Suppress raw HTML and dangerous URLs\n");
  printf("  --smart          Use smart punctuation\n");
  printf("  --normalize      Consolidate adjacent text nodes\n");
  printf("  --help, -h       Print usage information\n");
  printf("  --version        Print version\n");
}

ESIS_Bool
markup(void *userData, ESIS_ElemEvent ev,
                       long elemID, const ESIS_Elem *elem,
                       const ESIS_Char *charData, size_t len)
{
  struct UserData *the = userData;
  cmark_node *document;
  
  switch (ev) {
  case ESIS_START:
    the->parser = cmark_parser_new(the->options);
    break;
  case ESIS_CDATA:
    cmark_parser_feed(the->parser, charData, len);
    break;
  case ESIS_END:
    document = cmark_parser_finish(the->parser);
    cmark_render_esis(the->writer, document);
    cmark_node_free(document);
    cmark_parser_free(the->parser);
    the->parser = NULL;
  }
  return ESIS_TRUE;
}

int main(int argc, char *argv[]) {
  struct UserData ud;
  cmark_option_t options = CMARK_OPT_DEFAULT | CMARK_OPT_ISO;
  ESIS_Parser eparser;
  int i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("cmark_filter %s", CMARK_VERSION_STRING
                                    " ( %s %s )\n",
                                    cmark_repourl, cmark_gitident);
      printf(" - CommonMark converter\n(C) 2014, 2015 John MacFarlane\n");
      exit(0);
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
    } else {
      print_usage();
      exit(1);
    }
  }

  eparser = ESIS_ParserCreate(NULL);
  
  ud.options = options;
  ud.writer  = ESIS_WriterCreate(stdout, 0U);
  ud.parser  = NULL;
  
  ESIS_SetElementHandler(eparser, markup, "mark-up",  1L, &ud);
  ESIS_SetElementHandler(eparser, markup, "document", 2L, &ud);
  
  ESIS_FilterFile(eparser, stdin, stdout);
  
  ESIS_ParserFree(eparser);
  
  return 0;
}
