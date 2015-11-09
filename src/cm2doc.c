#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <esisio.h>

#include "config.h"
#include "cmark.h"

#include "cmark_ctype.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"
#include "scanners.h"

extern const char cmark_gitident[];
extern const char cmark_repourl[];

#define SUB '\23' /* ASCII SUB = Subst attr val */
#define SUBS "\23"

#define NODE_MAX   (1U <<  5)
#define TRANS_MAX  (1U << 11)
#define ATTR_MAX   (1U <<  8)
#define STAG_MAX   (1U << 16)

enum {
  DC_TITLE,
  DC_AUTHOR,
  DC_DATE,
  NUM_DCITEM
};

struct dcdata {
  const char *item[NUM_DCITEM];
};

struct meta_ {
  const char *css;
  const char *title;
  struct dcdata dc;
};

struct trans_ {
  const ESIS_Char *GI;
  const ESIS_Char **atts;
};

static const char DEFAULT_CSS[]   = "default.css";
static const char DEFAULT_TITLE[] = "Untitled document";

/*
 * The translation definitions.
 */
 
static struct trans_ trans[NODE_MAX] = { NULL, NULL };

static ESIS_Char x_buf[TRANS_MAX];
static ESIS_Char *x_atts[TRANS_MAX];
static size_t nxbuf = 0U;
static size_t nxatts = 0U;

/*
 * Add a string into the string buffer.
 */
ESIS_Char *push_xbuf(const ESIS_Char *s)
{
    ESIS_Char *p = x_buf + nxbuf;
    size_t n = strlen(s) + 1;
    
    memcpy(p, s, n);
    nxbuf += n;
    return p;
}

/*
 * Add a string pointer into the atts pointer buffer,
 * and the pointed-to string into the string buffer.
 */
ESIS_Char *push_xatts(const ESIS_Char *s)
{
    ESIS_Char *p = NULL;
    
    if (s != NULL) 
	p = push_xbuf(s);
	
    return x_atts[nxatts++] = p;
}

/*
 * Add a translation to the table.
 */
void x_add(cmark_node_type nt,
           const ESIS_Char *GI,
           const ESIS_Char **atts)
{
    ESIS_Char *p_GI;
    ESIS_Char **p_atts;
    size_t k, natt = 0U;
    
    p_atts = x_atts + nxatts;
    p_GI = push_xbuf(GI);
    if (atts != NULL) for (k = 0; atts[2*k] != NULL; ++k) {
        ESIS_Char *p_name, *p_val;
        p_name = push_xatts(atts[2*k+0]);
        p_val  = push_xatts(atts[2*k+1]);
        push_xatts(NULL);
        ++natt;
    }
    trans[nt].GI = p_GI;
    trans[nt].atts = p_atts;
}

#define RANKGI(_GI, _R) ( sprintf(gi, "%s%d", (_GI), (_R)), gi )

void print_usage() {
  printf("Usage:   cm2html spec [FILE*]\n");
  printf("\nspec is the output specification.\n\n");
  printf("Options:\n");
  printf("  -t --title TITLE Set the document title\n");
  printf("  -c --css CSS     Set the document style sheet to CSS\n");
  printf("  --sourcepos      Include source position attribute\n");
  printf("  --hardbreaks     Treat newlines as hard line breaks\n");
  printf("  --safe           Suppress raw HTML and dangerous URLs\n");
  printf("  --smart          Use smart punctuation\n");
  printf("  --normalize      Consolidate adjacent text nodes\n");
  printf("  --help, -h       Print usage information\n");
  printf("  --version        Print version\n");
}

/*
 * Translating start and end tags for cmark node types.
 */
 
static ESIS_Char *buf_atts[2*ATTR_MAX + 1];
static ESIS_Char buf_name[STAG_MAX + 1];
static ESIS_Char buf_val[STAG_MAX + 1];
size_t nbuf_name = 0U;
size_t nbuf_val = 0U;
size_t natt = 0U;

#define reset_atts() do { natt = nbuf_name = nbuf_val = 0U; } while (0)

void push_att(
            const ESIS_Char *name,
            const ESIS_Char *val, size_t len)
{
    const size_t avail_val = sizeof buf_val - nbuf_val;
    const size_t avail_name = sizeof buf_name - nbuf_name;
    ESIS_Char *bname, *bval;
    size_t n;
    
    if (len == ESIS_NTS) len = (val == NULL) ? 0U : strlen(val);
    
    if (avail_val < len) {
	exit(EXIT_FAILURE);
    }
    if (avail_name < (n = strlen(name) + 1)) {
	exit(EXIT_FAILURE);
    }
    if (natt >= ATTR_MAX) {
	exit(EXIT_FAILURE);
    }
    
    memcpy((bname = buf_name + nbuf_name), name, n);
    memcpy((bval  = buf_val  + nbuf_val),  val,  len);
    nbuf_name += n;
    nbuf_val += len;
    buf_val[nbuf_val++] = '\0';
    
    buf_atts[2*natt+0] = bname;
    buf_atts[2*natt+1] = bval;
    buf_atts[2*natt+2] = NULL;
    ++natt;
}

void push_atts(const ESIS_Char **atts)
{
    size_t k;
    
    if (atts != NULL) for (k = 0; atts[2*k] != NULL; ++k)
	push_att(atts[2*k+0], atts[2*k+1], ESIS_NTS);
	    
}

/*
 * Find attribute in active input element (ie in buf_atts),
 * return the value.
 */
const ESIS_Char* attval(const ESIS_Char *name)
{
    size_t k;
    
    for (k = 0; k < natt; ++k)
	if (!strcmp(buf_atts[2*k], name))
	    return buf_atts[2*k+1];
    return NULL;
}

void x_Attr(ESIS_Writer w,
            const ESIS_Char *name, const ESIS_Char *val, size_t len)
{
    push_att(name, val, len);
}

void x_Start(ESIS_Writer w,
             cmark_node_type nt, const ESIS_Char *atts[])
{
    size_t k;
    const struct trans_ *tr = &trans[nt];
    const ESIS_Char *GI;
    ESIS_Char GI_buf[24];
    
    if (tr != NULL && (GI = tr->GI) != NULL) {
	const ESIS_Char *p;
    
        if ((p = strchr(GI, SUB)) != NULL) {
	    const ESIS_Char *v;
	    ESIS_Char *q;
	    
	    strcpy(GI_buf, GI);
	    q = GI_buf + (p - GI);
	    v = attval(p + 1);
	    if (v != NULL)
		strcpy(q, v);
	    else
		q[0] = '\0';
	    GI = GI_buf;
        }
        
	push_atts(atts);
    
	for (k = 0; tr->atts[2*k] != NULL; ++k) {
	    const ESIS_Char *name, *val;
	    const ESIS_Char *xval;
	    
	    name = tr->atts[2*k+0];
	    val  = tr->atts[2*k+1];
	    
	    xval = (val[0] == SUB) ? attval(val+1) : val;

	    ESIS_Attr(w, name, xval, ESIS_NTS);
	}
	
	ESIS_Start(w, GI, NULL);
    }
    
    reset_atts();
}

void x_Empty(ESIS_Writer w,
             cmark_node_type nt, const ESIS_Char *atts[])
{
    size_t k;
    const struct trans_ *tr = &trans[nt];
    const ESIS_Char *GI;
    
    if (tr != NULL && (GI = tr->GI) != NULL) {
    
	push_atts(atts);
	    
	for (k = 0; tr->atts[2*k] != NULL; ++k) {
	    const ESIS_Char *name, *val;
	    const ESIS_Char *xval;
	    
	    name = tr->atts[2*k+0];
	    val  = tr->atts[2*k+1];
	    
	    xval = (val[0] == SUB) ? attval(val+1) : val;

	    ESIS_Attr(w, name, xval, ESIS_NTS);
	}
	
	ESIS_Empty(w, GI, NULL);
    }
    
    reset_atts();
}

void x_End(ESIS_Writer w, cmark_node_type nt)
{
    const struct trans_ *tr = &trans[nt];
    const ESIS_Char *GI;
    
    if (tr != NULL && (GI = tr->GI) != NULL) {
	ESIS_End(w, GI);
    }
}
 
 
/*
 * Rendering into the translator.
 */
 
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
      x_Start(w, node->type, NULL);
      ESIS_Cdata(w, node->as.literal.data, node->as.literal.len);
      x_End(w,  node->type);
      break;

    case CMARK_NODE_LIST:
      switch (cmark_node_get_list_type(node)) {
      case CMARK_ORDERED_LIST:
        x_Attr(w, "type", "ordered", ESIS_NTS); 
        sprintf(buffer, "%d", cmark_node_get_list_start(node));
        x_Attr(w, "start", buffer, ESIS_NTS);
        delim = cmark_node_get_list_delim(node);
        x_Attr(w, "delim", (delim == CMARK_PAREN_DELIM) ?
                              "paren" : "period", ESIS_NTS);
        break;
      case CMARK_BULLET_LIST:
        x_Attr(w, "type", "bullet", ESIS_NTS);
        break;
      default:
        break;
      }
      x_Attr(w, "tight", cmark_node_get_list_tight(node) ?
                                            "true" : "false", ESIS_NTS);
      x_Start(w, node->type, NULL);
      break;

    case CMARK_NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      x_Attr(w, "level", buffer, ESIS_NTS);
      x_Start(w, node->type, NULL);
      break;

    case CMARK_NODE_CODE_BLOCK:
      if (node->as.code.info.len > 0)
        x_Attr(w, "info", 
		       node->as.code.info.data, node->as.code.info.len);
      x_Start(w, node->type, NULL);
      ESIS_Cdata(w,
	         node->as.code.literal.data, node->as.code.literal.len);
      x_End(w, node->type);
      break;

    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      x_Attr(w, "destination", node->as.link.url.data, node->as.link.url.len);
      x_Attr(w, "title", node->as.link.title.data, node->as.link.title.len);
      x_Start(w, node->type, NULL);
      break;

    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      x_Empty(w, node->type, NULL);
      break;

    case CMARK_NODE_DOCUMENT:
    default:
      x_Start(w, node->type, NULL);
      break;
    } /* entering switch */
  } else if (node->first_child) { /* NOT entering */
    x_End(w, node->type);
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
  cmark_iter_free(iter);
  return NULL;
}


/*
 * Translating a cmark_node_type into configured GI and atts.
 */
 
/*
 * Walk the parsed document tree and shove the (translated) 
 * attributes and tags as well as teh (unchanged) character 
 * data into the writer.
 */
static void gen_document(cmark_node *document,
                         ESIS_Writer writer,
                         cmark_option_t options, 
                         struct meta_ *meta
                         ) {
  /*unsigned iter;*/
  static const char *dcitem[NUM_DCITEM] = {
    "\"DC.title\"  ",
    "\"DC.creator\"",
    "\"DC.date\"   ",
  };

#if 0
  /* HTML prolog */
  puts("<!DOCTYPE HTML PUBLIC \"ISO/IEC 15445:2000//DTD HTML//EN\">");
  puts("<HTML>");
  puts("<HEAD>");
  puts("  <META http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">");
printf("  <META name=\"GENERATOR\"\n"
       "        content=\"cmark " CMARK_VERSION_STRING
                                    " ( %s %s )\">\n",
                                    cmark_repourl, cmark_gitident);
       
  for (iter = 0; iter < NUM_DCITEM; ++iter) {
    if (iter == 0) {
       puts("  <LINK rel=\"schema.DC\"   "
                          "href=\"http://purl.org/dc/elements/1.1/\">");
       puts("  <META name=\"DC.format\"  "
                  "content=\"text/html\" scheme=\"DCTERMS.IMT\">");
       puts("  <META name=\"DC.type\"    "
                  "content=\"Text\"      scheme=\"DCTERMS.DCMIType\">");
    }
    if (pdc->item[iter] != NULL)
     printf("  <META name=%s content=\"%s\">\n",
                                         dcitem[iter], pdc->item[iter]);
  }
              
  printf("  <LINK rel=\"stylesheet\"  type=\"text/css\" href=\"%s\">\n",
                                                                   css);
  printf("  <TITLE>%s</TITLE>\n", title);
  puts("</HEAD>");
  puts("<BODY>");
#endif /* HTML prolog */

  cmark_render_esis(writer, document);
#if 0 /* HTML epilog */
  puts("</BODY>");
  puts("</HTML>");
#endif
}

size_t do_pandoc(char *buffer, size_t nbuf, struct meta_ *meta)
{
    size_t nused, nalloc;
    size_t ibol;
    unsigned iter;

    for (iter = 0; iter < NUM_DCITEM; ++iter)
	meta->dc.item[iter] = NULL;
    
    ibol = 0U;
    nused = 0U;
    
    for (iter = 0; iter < 3; ++iter) {
        char *p;
        size_t ifield;
        
	/*
	 * Field starts after '%', ends before *p = LF.
	 */
        if (buffer[ibol] != '%')
	    break;
        ifield = ibol + 1U;
        if (buffer[ifield] == ' ') ++ifield;
        if (ifield >= nbuf)
            break;
        p = memchr(buffer+ifield, '\n', nbuf - ifield);
        if (p == NULL)
            break;
            
        ibol = p - buffer + 1; /* One after '\n'. */
        
        /*
         * We copy buffer[ifield .. ibol-2], ie the line content
         * from ifield to just before the '\n', and append a NUL 
         * terminator, of course.
         */
        nalloc = ibol - ifield;
        if (nalloc > 1) {
            char *pitem = malloc(nalloc);
            if (pitem == NULL)
    		break;
            memcpy(pitem, buffer+ifield, nalloc-1);
            pitem[nalloc-1] = '\0';
            meta->dc.item[iter] = pitem;
        }
        
        /*
         * The next line, if any, starts after '\n', at 
         * `buffer[ibol]`. We have used all what came before.
         */
        nused = ibol;
        if (nused >= nbuf)
            break;
    }
    
    /*
     * Set up defaults for required, but missing values.
     */
    if (meta->css == NULL)
	meta->css = DEFAULT_CSS;
    if (meta->title == NULL && meta->dc.item[DC_TITLE] == NULL)
	meta->title = meta->dc.item[DC_TITLE] = DEFAULT_TITLE;
    else if (meta->title != NULL)
	meta->dc.item[DC_TITLE] = meta->title;
    else 
	meta->title = meta->dc.item[DC_TITLE];
	
	
    return nused;
}

void setup(const char *trfile)
{
    const ESIS_Char **atts = buf_atts;
    
    x_add(CMARK_NODE_DOCUMENT,	    "CM-DOC", NULL);
    x_add(CMARK_NODE_BLOCK_QUOTE,   "CM-BQ", NULL);
    
    push_att("first", SUBS "start", ESIS_NTS);
    x_add(CMARK_NODE_LIST,	    "CM-LST", atts);
    reset_atts();
    
    x_add(CMARK_NODE_ITEM,	    "CM-LIT", NULL);
    x_add(CMARK_NODE_CODE_BLOCK,    "CM-CB", NULL);
    x_add(CMARK_NODE_HTML,	    "CM-HTMB", NULL);
    x_add(CMARK_NODE_PARAGRAPH,	    "CM-PAR", NULL);
    x_add(CMARK_NODE_HEADER,	    "CM-H" SUBS "level" , NULL);
    x_add(CMARK_NODE_HRULE,	    "CM-HR", NULL);
    
    x_add(CMARK_NODE_TEXT,          "CM-TXT", NULL);
    x_add(CMARK_NODE_SOFTBREAK,	    "CM-SBR", NULL);
    x_add(CMARK_NODE_LINEBREAK,	    "CM-LBR", NULL);
    x_add(CMARK_NODE_CODE,	    "CM-COD", NULL);
    x_add(CMARK_NODE_INLINE_HTML,   "CM-HTM", NULL);
    x_add(CMARK_NODE_EMPH,	    "CM-EMP", NULL);
    x_add(CMARK_NODE_STRONG,	    "CM-STR", NULL);
    x_add(CMARK_NODE_LINK,	    "CM-LNK", NULL);
    x_add(CMARK_NODE_IMAGE,	    "CM-IMG", NULL);
}

int main(int argc, char *argv[]) {
  FILE *fp;
  bool in_header;
  struct meta_ meta;
  
  int argi;
  char buffer[4096];
  size_t bytes;
  
  cmark_parser *parser;
  cmark_node *document;
  cmark_option_t options = CMARK_OPT_DEFAULT | CMARK_OPT_ISO;
  
  ESIS_Writer writer;

  setup("dummy.tr"); /* Get things going */
  
  meta.css = NULL;
  meta.title = NULL;
  
  for (argi = 1; argi < argc; ++argi) {
    if (strcmp(argv[argi], "--version") == 0) {
      printf("cmark %s", CMARK_VERSION_STRING
                                    " ( %s %s )\n",
                                    cmark_repourl, cmark_gitident);
      printf(" - CommonMark converter\n(C) 2014, 2015 John MacFarlane\n");
      exit(0);
    } else if ((strcmp(argv[argi], "--title") == 0) ||
               (strcmp(argv[argi], "-t") == 0)) {
      meta.title = argv[++argi];
    } else if ((strcmp(argv[argi], "--css") == 0) ||
               (strcmp(argv[argi], "-c") == 0)) {
      meta.css = argv[++argi];
    } else if (strcmp(argv[argi], "--sourcepos") == 0) {
      options |= CMARK_OPT_SOURCEPOS;
    } else if (strcmp(argv[argi], "--hardbreaks") == 0) {
      options |= CMARK_OPT_HARDBREAKS;
    } else if (strcmp(argv[argi], "--smart") == 0) {
      options |= CMARK_OPT_SMART;
    } else if (strcmp(argv[argi], "--safe") == 0) {
      options |= CMARK_OPT_SAFE;
    } else if (strcmp(argv[argi], "--normalize") == 0) {
      options |= CMARK_OPT_NORMALIZE;
    } else if (strcmp(argv[argi], "--validate-utf8") == 0) {
      options |= CMARK_OPT_VALIDATE_UTF8;
    } else if ((strcmp(argv[argi], "--help") == 0) ||
               (strcmp(argv[argi], "-h") == 0)) {
      print_usage();
      exit(0);
    } else if (*argv[argi] == '-') {
      print_usage();
      exit(1);
    } else {
      break; /* Exit loop, treat remaining args as file names. */
    }
  }

  /*
   * Set up the CommonMark parser.
   */
  parser = cmark_parser_new(options);
  
  /*
   * Either feed stdin or each of the named input files to the parser.
   */
   
  fp = stdin;
  in_header = true;
  
  switch (argc - argi) do {
  default:
    fp = freopen(argv[argi], "r", stdin);
    
    if (fp == NULL) {
      fprintf(stderr, "Error opening file %s: %s\n", argv[argi],
              strerror(errno));
      exit(1);
    }

  case 0:
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      size_t hbytes = 0U;
      
      if (in_header)
        hbytes = do_pandoc(buffer, sizeof buffer, &meta);
      in_header = false;
                         
      if (hbytes < bytes)
        cmark_parser_feed(parser, buffer + hbytes, bytes - hbytes);
        
      if (bytes < sizeof(buffer)) {
        break;
      }
    }
  } while (++argi < argc);

  /*
   * Let the parser built it's document tree, then discard it.
   */
  document = cmark_parser_finish(parser);
  cmark_parser_free(parser);

  /*
   * Walk the document tree, generating output.
   */
  writer = ESIS_XmlWriterCreate(stdout, 0U);
  gen_document(document, writer, options, &meta);

  /*
   * Discard the document tree.
   */
  ESIS_WriterFree(writer);
  cmark_node_free(document);

  return 0;
}
