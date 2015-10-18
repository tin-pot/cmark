#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cmark_ctype.h"
#include "config.h"
#include "cmark.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"
#include "scanners.h"

// Functions to convert cmark_nodes to HTML strings.

static void escape_html(cmark_strbuf *dest, const unsigned char *source,
                        bufsize_t length) {
  houdini_escape_html0(dest, source, length, 0);
}

static inline void cr(cmark_strbuf *html) {
  if (html->size && html->ptr[html->size - 1] != '\n')
    cmark_strbuf_putc(html, '\n');
}

struct render_state {
  cmark_strbuf *html;
  cmark_node *plain;
};

static void S_render_sourcepos(cmark_node *node, cmark_strbuf *html,
                               int options) {
  char buffer[100];
  if (CMARK_OPT_SOURCEPOS & options) {
    sprintf(buffer, " data-sourcepos=\"%d:%d-%d:%d\"",
            cmark_node_get_start_line(node), cmark_node_get_start_column(node),
            cmark_node_get_end_line(node), cmark_node_get_end_column(node));
    cmark_strbuf_puts(html, buffer);
  }
}

static int S_render_node(cmark_node *node, cmark_event_type ev_type,
                         struct render_state *state, int options) {
  cmark_node *parent;
  cmark_node *grandparent;
  cmark_strbuf *html = state->html;
  char start_header[] = "<H0";
  char end_header[] = "</H0";
  bool tight;
  char buffer[100];

  bool entering = (ev_type == CMARK_EVENT_ENTER);

  if (state->plain == node) { // back at original node
    state->plain = NULL;
  }

  if (state->plain != NULL) {
    switch (node->type) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
    case CMARK_NODE_INLINE_HTML:
      escape_html(html, node->as.literal.data, node->as.literal.len);
      break;

    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_SOFTBREAK:
      cmark_strbuf_putc(html, ' ');
      break;

    default:
      break;
    }
    return 1;
  }

  switch (node->type) {
  case CMARK_NODE_DOCUMENT:
    break;

  case CMARK_NODE_BLOCK_QUOTE:
    if (entering) {
      cr(html);
      cmark_strbuf_puts(html, "<BLOCKQUOTE");
      S_render_sourcepos(node, html, options);
      cmark_strbuf_puts(html, ">\n");
    } else {
      cr(html);
      cmark_strbuf_puts(html, "</BLOCKQUOTE>\n");
    }
    break;

  case CMARK_NODE_LIST: {
    cmark_list_type list_type = node->as.list.list_type;
    int start = node->as.list.start;

    if (entering) {
      cr(html);
      if (list_type == CMARK_BULLET_LIST) {
        cmark_strbuf_puts(html, "<UL");
        S_render_sourcepos(node, html, options);
        cmark_strbuf_puts(html, ">\n");
      } else if (start == 1) {
        cmark_strbuf_puts(html, "<OL");
        S_render_sourcepos(node, html, options);
        cmark_strbuf_puts(html, ">\n");
      } else {
        if (options & CMARK_OPT_ISO)
          sprintf(buffer, "<OL"); /* ISO HTML has no `start` attrib. */
        else
          sprintf(buffer, "<OL start=\"%d\"", start);
        cmark_strbuf_puts(html, buffer);
        S_render_sourcepos(node, html, options);
        cmark_strbuf_puts(html, ">\n");
      }
    } else {
      cmark_strbuf_puts(html,
                        list_type == CMARK_BULLET_LIST ? "</UL>\n" : "</OL>\n");
    }
    break;
  }

  case CMARK_NODE_ITEM:
    if (entering) {
      cr(html);
      cmark_strbuf_puts(html, "<LI");
      S_render_sourcepos(node, html, options);
      cmark_strbuf_putc(html, '>');
    } else {
      cmark_strbuf_puts(html, "</LI>\n");
    }
    break;

  case CMARK_NODE_HEADER:
    if (entering) {
      cr(html);
      start_header[2] = (char)('0' + node->as.header.level);
      cmark_strbuf_puts(html, start_header);
      S_render_sourcepos(node, html, options);
      cmark_strbuf_putc(html, '>');
    } else {
      end_header[3] = (char)('0' + node->as.header.level);
      cmark_strbuf_puts(html, end_header);
      cmark_strbuf_puts(html, ">\n");
    }
    break;

  case CMARK_NODE_CODE_BLOCK:
    cr(html);

    if (!node->as.code.fenced || node->as.code.info.len == 0) {
      cmark_strbuf_puts(html, "<PRE");
      S_render_sourcepos(node, html, options);
      cmark_strbuf_puts(html, "><code>");
    } else {
      bufsize_t first_tag = 0;
      while (first_tag < node->as.code.info.len &&
             !cmark_isspace(node->as.code.info.data[first_tag])) {
        first_tag += 1;
      }

      cmark_strbuf_puts(html, "<PRE");
      S_render_sourcepos(node, html, options);
      cmark_strbuf_puts(html, "><code class=\"language-");
      escape_html(html, node->as.code.info.data, first_tag);
      cmark_strbuf_puts(html, "\">");
    }

    escape_html(html, node->as.code.literal.data, node->as.code.literal.len);
    cmark_strbuf_puts(html, "</CODE></PRE>\n");
    break;

  case CMARK_NODE_HTML:
    cr(html);
    if (options & CMARK_OPT_SAFE) {
      cmark_strbuf_puts(html, "<!-- raw HTML omitted -->");
    } else {
      cmark_strbuf_put(html, node->as.literal.data, node->as.literal.len);
    }
    cr(html);
    break;

  case CMARK_NODE_HRULE:
    cr(html);
    cmark_strbuf_puts(html, "<HR");
    S_render_sourcepos(node, html, options);
    cmark_strbuf_puts(html, ">\n");
    break;

  case CMARK_NODE_PARAGRAPH:
    parent = cmark_node_parent(node);
    grandparent = cmark_node_parent(parent);
    if (grandparent != NULL && grandparent->type == CMARK_NODE_LIST) {
      tight = grandparent->as.list.tight;
    } else {
      tight = false;
    }
    if (!tight) {
      if (entering) {
        cr(html);
        cmark_strbuf_puts(html, "<P");
        S_render_sourcepos(node, html, options);
        cmark_strbuf_putc(html, '>');
      } else {
        cmark_strbuf_puts(html, "</P>\n");
      }
    }
    break;

  case CMARK_NODE_TEXT:
    escape_html(html, node->as.literal.data, node->as.literal.len);
    break;

  case CMARK_NODE_LINEBREAK:
    cmark_strbuf_puts(html, "<BR>\n");
    break;

  case CMARK_NODE_SOFTBREAK:
    if (options & CMARK_OPT_HARDBREAKS) {
      cmark_strbuf_puts(html, "<BR>\n");
    } else {
      cmark_strbuf_putc(html, '\n');
    }
    break;

  case CMARK_NODE_CODE:
    cmark_strbuf_puts(html, "<CODE>");
    escape_html(html, node->as.literal.data, node->as.literal.len);
    cmark_strbuf_puts(html, "</CODE>");
    break;

  case CMARK_NODE_INLINE_HTML:
    if (options & CMARK_OPT_SAFE) {
      cmark_strbuf_puts(html, "<!-- raw HTML omitted -->");
    } else {
      cmark_strbuf_put(html, node->as.literal.data, node->as.literal.len);
    }
    break;

  case CMARK_NODE_STRONG:
    if (entering) {
      cmark_strbuf_puts(html, "<STRONG>");
    } else {
      cmark_strbuf_puts(html, "</STRONG>");
    }
    break;

  case CMARK_NODE_EMPH:
    if (entering) {
      cmark_strbuf_puts(html, "<EM>");
    } else {
      cmark_strbuf_puts(html, "</EM>");
    }
    break;

  case CMARK_NODE_LINK:
    if (entering) {
      cmark_strbuf_puts(html, "<A href=\"");
      if (!((options & CMARK_OPT_SAFE) &&
            scan_dangerous_url(&node->as.link.url, 0))) {
        houdini_escape_href(html, node->as.link.url.data,
                            node->as.link.url.len);
      }
      if (node->as.link.title.len) {
        cmark_strbuf_puts(html, "\" title=\"");
        escape_html(html, node->as.link.title.data, node->as.link.title.len);
      }
      cmark_strbuf_puts(html, "\">");
    } else {
      cmark_strbuf_puts(html, "</A>");
    }
    break;

  case CMARK_NODE_IMAGE:
    if (entering) {
      cmark_strbuf_puts(html, "<IMG src=\"");
      if (!((options & CMARK_OPT_SAFE) &&
            scan_dangerous_url(&node->as.link.url, 0))) {
        houdini_escape_href(html, node->as.link.url.data,
                            node->as.link.url.len);
      }
      cmark_strbuf_puts(html, "\" alt=\"");
      state->plain = node;
    } else {
      if (node->as.link.title.len) {
        cmark_strbuf_puts(html, "\" title=\"");
        escape_html(html, node->as.link.title.data, node->as.link.title.len);
      }

      cmark_strbuf_puts(html, "\">");
    }
    break;

  default:
    assert(false);
    break;
  }

  // cmark_strbuf_putc(html, 'x');
  return 1;
}

char *cmark_render_html(cmark_node *root, int options) {
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
