/* esisrd.c */

#include "esisio.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cmark_ctype.h"
#include "config.h"
#include "cmark.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"
#include "scanners.h"

extern const char cmark_gitident[];
extern const char cmark_repourl[];

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#endif

struct render_state {
  cmark_strbuf *esis;
  cmark_node *plain;
};


static const char *GI[] = {
  "none",
  "document",
  "block_quote",
  "list",
  "item",
  "code_block",
  "html",
  "paragraph",
  "header",
  "hrule",
  "text",
  "softbreak",
  "linebreak",
  "code",
  "inline_html",
  "emph",
  "strong",
  "link",
  "image",
};

/* ESIS output */

static void attribn(cmark_strbuf *out, const char *attr,
                                            const char *val, size_t len)
{
  cmark_strbuf_putc(out, 'A');
  cmark_strbuf_puts(out, attr);
  cmark_strbuf_puts(out, " CDATA ");
  cmark_strbuf_put(out, val, len);
  cmark_strbuf_putc(out, '\n');
}

static void attrib(cmark_strbuf *out, const char *attr, const char *val)
{
  cmark_strbuf_putc(out, 'A');
  cmark_strbuf_puts(out, attr);
  cmark_strbuf_puts(out, " CDATA ");
  cmark_strbuf_puts(out, val);
  cmark_strbuf_putc(out, '\n');
}

static void stag(cmark_strbuf *out, cmark_node_type t)
{
  const char *gi = GI[t];

  cmark_strbuf_putc(out, '(');
  cmark_strbuf_puts(out, gi);
  cmark_strbuf_putc(out, '\n');
}

static void etag(cmark_strbuf *out, cmark_node_type t)
{
  const char *gi = GI[t];

  cmark_strbuf_putc(out, ')');
  cmark_strbuf_puts(out, gi);
  cmark_strbuf_putc(out, '\n');
}

static void cdata(cmark_strbuf *out, char *text, size_t len)
{
  size_t k;

  if (len == 0) return;

  cmark_strbuf_putc(out, '-');
  for (k = 0; k < len; ++k) {
    unsigned ch = 0xFF & text[k];
    if (ch == '\\') {
      cmark_strbuf_putc(out, '\\');
      cmark_strbuf_putc(out, '\\');
    } else if (ch == '\n') {
      cmark_strbuf_putc(out, '\\');
      cmark_strbuf_putc(out, 'n');
      cmark_strbuf_putc(out, '\\');
      cmark_strbuf_putc(out, '0');
      cmark_strbuf_putc(out, '1');
      cmark_strbuf_putc(out, '2');
    } else if (ch < 32) {
      char buf[4];
      sprintf(buf, "%03o", ch);
      cmark_strbuf_putc(out, '\\');
      cmark_strbuf_putc(out, buf[0]);
      cmark_strbuf_putc(out, buf[1]);
      cmark_strbuf_putc(out, buf[2]);
    } else {
      cmark_strbuf_putc(out, ch);
    }
  }
  cmark_strbuf_putc(out, '\n');
}

/* Rendering */

static int S_render_node(cmark_node *node, cmark_event_type ev_type,
                         struct render_state *state, cmark_option_t options) {
  cmark_strbuf *esis = state->esis;
  bool literal = false;
  cmark_delim_type delim;
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  char buffer[100];

  if (entering) {
    literal = false;

    switch (node->type) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
    case CMARK_NODE_HTML:
    case CMARK_NODE_INLINE_HTML:
      stag(esis, node->type);
      cdata(esis, node->as.literal.data, node->as.literal.len);
      etag(esis, node->type);
      literal = true;
      break;
    case CMARK_NODE_LIST:
      switch (cmark_node_get_list_type(node)) {
      case CMARK_ORDERED_LIST:
        attrib(esis, "type", "ordered");
        sprintf(buffer, "%d", cmark_node_get_list_start(node));
        attrib(esis, "start", buffer);
        delim = cmark_node_get_list_delim(node);
        if (delim == CMARK_PAREN_DELIM) {
          attrib(esis, "delim", "paren");
        } else if (delim == CMARK_PERIOD_DELIM) {
          attrib(esis, "delim", "period");
        }
        break;
      case CMARK_BULLET_LIST:
        attrib(esis, "type", "bullet");
        break;
      default:
        break;
      }
      attrib(esis, "tight", cmark_node_get_list_tight(node) ?
                                                      "true" : "false");
      stag(esis, node->type);
      break;
    case CMARK_NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      attrib(esis, "level", buffer);
      stag(esis, node->type);
      break;
    case CMARK_NODE_CODE_BLOCK:
      if (node->as.code.info.len > 0) {
        attribn(esis, "info", node->as.code.info.data, node->as.code.info.len);
      }
      stag(esis, node->type);
      cdata(esis, node->as.code.literal.data, node->as.code.literal.len);
      etag(esis, node->type);
      literal = true;
      break;
    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      attribn(esis, "destination", node->as.link.url.data, node->as.link.url.len);
      attribn(esis, "title", node->as.link.title.data, node->as.link.title.len);
      stag(esis, node->type);
      break;
    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      stag(esis, node->type);
      etag(esis, node->type);
      break;
    case CMARK_NODE_DOCUMENT:
      cmark_strbuf_puts(esis, "?xml version=\"1.0\" encoding=\"UTF-8\"\n");
      stag(esis, node->type);
      break;
    default:
      stag(esis, node->type);
      break;
    }
  } else if (node->first_child) {
    etag(esis, node->type);
  }

  return 1;
}

char *cmark_render_esis(cmark_node *root, cmark_option_t options)
{
  char *result;
  cmark_strbuf html = GH_BUF_INIT;
  cmark_event_type ev_type;
  cmark_node *cur;
  struct render_state state = {&html, NULL};
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node(cur, ev_type, &state, options);
  }
  result = (char *)cmark_strbuf_detach(&html);

  cmark_iter_free(iter);
  return result;
}

void print_usage() {
  printf("Usage:   cmesis [FILE*]\n");
  printf("Options:\n");
  printf("  --sourcepos      Include source position attribute\n");
  printf("  --hardbreaks     Treat newlines as hard line breaks\n");
  printf("  --safe           Suppress raw HTML and dangerous URLs\n");
  printf("  --smart          Use smart punctuation\n");
  printf("  --normalize      Consolidate adjacent text nodes\n");
  printf("  --help, -h       Print usage information\n");
  printf("  --version        Print version\n");
}

int main(int argc, char *argv[]) {
  int i, numfps = 0;
  int *files;
  char buffer[4096];
  char *title = NULL;
  char *css = NULL;
  
  cmark_parser *parser;
  size_t bytes;
  cmark_node *document;
  cmark_option_t options = CMARK_OPT_DEFAULT | CMARK_OPT_ISO;

#if defined(_WIN32) && !defined(__CYGWIN__)
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  files = (int *)malloc(argc * sizeof(*files));

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("cmesis %s", CMARK_VERSION_STRING
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
    bool in_header = true;
    
    if (fp == NULL) {
      fprintf(stderr, "Error opening file %s: %s\n", argv[files[i]],
              strerror(errno));
      exit(1);
    }

    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      cmark_parser_feed(parser, buffer, bytes);
    }

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

  document = cmark_parser_finish(parser);
  cmark_parser_free(parser);

  printf("%s", cmark_render_esis(document, options));

  cmark_node_free(document);

  free(files);

  return 0;
}
