#include <ctype.h> /* toupper() */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "cmark.h"

#include "cmark_ctype.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"
#include "scanners.h"

#define NUL  0
#define SOH  1
#define STX  2
#define ETX  3
#define HT   9
#define LF  10
#define CR  13
#define SO  14
#define SI  15
#define SP  32

#define EOL LF

#define NTS (~0U)

/*
 * Make available the Git commit ident and the repository URL.
 * (Use -DWITH_GITIDENT=0 to supress this.)
 */
#ifndef WITH_GITIDENT
#define WITH_GITIDENT 1
#endif

#if WITH_GITIDENT
extern const char cmark_gitident[];
extern const char cmark_repourl[];
#else
static const char cmark_gitident[] = "";
static const char cmark_repourl[]  = "";
#endif

/*
 * For each CommonMark node type we use a GI conforming to the
 * ISO 8879 SGML Reference Concrete Syntax, which has 
 *
 *     NAMING LCNMSTRT ""
 *            UCNMSTRT ""
 *            LCNMCHAR "-."
 *            UCNMCHAR "-."
 *            NAMECASE GENERAL YES
 *                     ENTITY  NO
 *
 * The Reference Quantity Set sets NAMELEN to 8, so these GIs are
 * somewhat shorter than the ones in the CommonMark DTD -- which is
 * a good thing IMO.
 *
 * (All this is of course purely cosmetic and/or a nod to SGML, where
 * all this "structural mark-up" stuff came from -- You could define
 * and use any GI and any NMSTART / NMCHAR character classes you want
 * for the names of CommonMark node types.)
 *
 */
#define NAMELEN 8
#define ISUCNMSTRT(C) ( 'A' <= (C) && (C) <= 'Z' )
#define ISLCNMSTRT(C) ( 'a' <= (C) && (C) <= 'z' )
#define ISUCNMCHAR(C) ( ISUCNMSTRT(C) || (C) == '-' || (C) == '.' )
#define ISLCNMCHAR(C) ( ISLCNMSTRT(C) || (C) == '-' || (C) == '.' )

#define NODE_MAX       CMARK_NODE_LAST_INLINE
#define NODENAME_LEN   NAMELEN

static const char* const nodename[NODE_MAX+1] = {
    "NONE", /* Should *never* occur! */
    "DOC",
    "QUOTE-BL",
    "LIST",
    "ITEM",
    "CODE-BL",
    "HTML-BL",
    "PARA",
    "HEADER",
    "HRULE",
    "TEXT",
    "SOFT-BR",
    "LINE-BR",
    "CODE",
    "HTML",
    "EMPH",
    "STRONG",
    "LINK",
    "IMAGE",
};

/*
 * Character classification.
 */

#define ISDIGIT(C)   ( '0' <= (C) && (C) <= '9' )
#define ISHEX(C)     ( ISDIGIT(C) || \
                      ('A'<=(C) && (C)<='F') || ('a'<=(C) && (C)<='f') )
#define ISSPACE(C)   ( (C) == SP || (C) == EOL || (C) == HT )
#define ISNMSTART(C) ( ISDIGIT(C) || ISUCNMSTRT(C) || ISLCNMSTRT(C) )
#define ISNMCHAR(C)  ( ISNMSTART(C) || (C) == '-' || (C) == '.' )

FILE *outfp;

#if 0
/*
 * Unicode
 */

typedef long ucs_t;

#define U(X)   ((0x ## X) & 0xFFFFFFL)
#define UCS(c) ((ucs_t)((c) & 0xFF))
#define UEOF   (-1L)
#endif /* Not needed yet. */

void error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(EXIT_FAILURE);
}

/*
 * Dublin Core and document meta.
 */
 
static const char DEFAULT_CSS[]   = "default.css";
static const char DEFAULT_TITLE[] = "Untitled document";

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

/*
 * Translation definition.
 */
 
#define STAG_BIT 1
#define ETAG_BIT 2

typedef size_t textidx_t; /* Index into textbuf */
typedef size_t attridx_t; /* Index into attrbuf */

struct trans_ {
  const char* stag_repl;
  const char* etag_repl;
  unsigned defined; /* STAG_BIT, ETAG_BIT */
};

/*
 * The translation definitions.
 */
 
#define INIT_SIZE  2048

static struct trans_ trans[NODE_MAX];

/*
 * Replacement texts.
 */
static cmark_strbuf textbuf;

/*
 *Attribute names and values of current node.
 */
static cmark_strbuf attrbuf;

/*
 * Attribute name/value list.
 *
 * ATTCNT is the 
 * 
 * > Number of attribute names and name tokens in an element's
 * > attribute definitions.
 *
 * Reference Quantity Set: 40.
 */
#define ATTCNT 255

static attridx_t atts[2*ATTCNT+1];
static size_t natts = 0U;


/*
 * Add a translation to the table.
 */
void set_trans(cmark_node_type nt,
	   unsigned tag_bit, /* STAG_BIT or ETAG_BIT */
           const char *repl)
{
    switch (tag_bit) {
    case STAG_BIT: trans[nt].stag_repl = repl; break;
    case ETAG_BIT: trans[nt].etag_repl = repl; break;
    default:
	error("Invalid tag_bit");
    }
    trans[nt].defined |= tag_bit;
}


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

#define reset_atts() do { \
	natts = 0U; cmark_strbuf_clear(&attrbuf); \
    } while (0)

void push_att(const char *name, const char *val, size_t len)
{
    attridx_t nameidx, validx;   
    
    if (len == NTS) len = strlen(val);
    
    cmark_strbuf_putc(&attrbuf, NUL);
    nameidx = attrbuf.size;
    cmark_strbuf_puts(&attrbuf, name);
    cmark_strbuf_putc(&attrbuf, NUL);
    validx = attrbuf.size;
    cmark_strbuf_put (&attrbuf, val, len);
    cmark_strbuf_putc(&attrbuf, NUL);
    
    atts[2*natts+0] = nameidx;
    atts[2*natts+1] = validx;
    ++natts;
}

void push_atts(const char **atts)
{
    size_t k;
    
    if (atts != NULL) for (k = 0; atts[2*k] != NULL; ++k)
	push_att(atts[2*k+0], atts[2*k+1], NTS);
	    
}

/*
 * Find attribute in active input element (ie in buf_atts),
 * return the value.
 */
const char* attval(const char *name)
{
    size_t k;
    
    for (k = 0; k < natts; ++k)
	if (!strcmp(attrbuf.ptr+atts[2*k+0], name))
	    return attrbuf.ptr+atts[2*k+1];
	    
    return NULL;
}

void putval(const char *name)
{
    const char *val = attval(name);
    if (val != NULL)
	fputs(val, outfp);
}

void do_Attr(const char *name, const char *val, size_t len)
{
    push_att(name, val, len);
}


void do_Start(cmark_node_type nt, const char *atts[])
{
    const struct trans_ *tr = &trans[nt];
    const char *repl;
    const char *p;
    char ch;
    
    if ((tr->defined & STAG_BIT) == 0U) 
	return;

    repl = tr->stag_repl;
	
    for (p = repl; (ch = *p) != NUL; ++p) {
	if (ch != SOH)
	    putc(ch, outfp);
	else {
	    const char *name = p + 1;
	    p = name + strlen(name);
	    putval(name);
	}
    }
    
    reset_atts();
}

void do_Empty(cmark_node_type nt, const char *atts[])
{
    do_Start(nt, atts);
}

void do_Cdata(const char *cdata, size_t len)
{
    size_t k;
    
    if (len == NTS) len = strlen(cdata);
    for (k = 0U; k < len; ++k)
	putc(cdata[k], outfp);
}

void do_End(cmark_node_type nt)
{
    const struct trans_ *tr = &trans[nt];
    const char *repl;
    
    if ((tr->defined & STAG_BIT) == 0U) 
	return;

    repl = tr->etag_repl;
	
    fputs(repl, outfp);
}
 
 
/*
 * Rendering into the translator.
 */
 
static int S_render_node(cmark_node *node, cmark_event_type ev_type)
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
      do_Start(node->type, NULL);
      do_Cdata(node->as.literal.data, node->as.literal.len);
      do_End(node->type);
      break;

    case CMARK_NODE_LIST:
      switch (cmark_node_get_list_type(node)) {
      case CMARK_ORDERED_LIST:
        do_Attr("type", "ordered", NTS); 
        sprintf(buffer, "%d", cmark_node_get_list_start(node));
        do_Attr("start", buffer, NTS);
        delim = cmark_node_get_list_delim(node);
        do_Attr("delim", (delim == CMARK_PAREN_DELIM) ?
                              "paren" : "period", NTS);
        break;
      case CMARK_BULLET_LIST:
        do_Attr("type", "bullet", NTS);
        break;
      default:
        break;
      }
      do_Attr("tight", cmark_node_get_list_tight(node) ?
                                            "true" : "false", NTS);
      do_Start(node->type, NULL);
      break;

    case CMARK_NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      do_Attr("level", buffer, NTS);
      do_Start(node->type, NULL);
      break;

    case CMARK_NODE_CODE_BLOCK:
      if (node->as.code.info.len > 0)
        do_Attr("info", 
		       node->as.code.info.data, node->as.code.info.len);
      do_Start(node->type, NULL);
      do_Cdata(
	         node->as.code.literal.data, node->as.code.literal.len);
      do_End(node->type);
      break;

    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      do_Attr("destination", node->as.link.url.data, node->as.link.url.len);
      do_Attr("title", node->as.link.title.data, node->as.link.title.len);
      do_Start(node->type, NULL);
      break;

    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      do_Empty(node->type, NULL);
      break;

    case CMARK_NODE_DOCUMENT:
    default:
      do_Start(node->type, NULL);
      break;
    } /* entering switch */
  } else if (node->first_child) { /* NOT entering */
    do_End(node->type);
  }

  return 1;
}

char *cmark_render_esis(cmark_node *root)
{
  cmark_event_type ev_type;
  cmark_node *cur;
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node(cur, ev_type);
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

  cmark_render_esis(document);
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

/*
 * setup -- Parse the replacement definition file.
 */
 
static const char *filename = "";
static FILE *fp             = NULL;
static unsigned lineno      = 0U;
static unsigned colno       = 0U;

#define COUNT_EOL() (++lineno, colno = 0U)

#define GETC() (   ch = getc(fp), \
                 ((ch == EOL) ? COUNT_EOL() : ++colno), \
                   ch )
                   
#define UNGETC(ch) ungetc(ch, fp)

void syntax_error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "%s(%u:%u): error: ", filename, lineno, colno);
    vfprintf(stderr, msg, va);
    va_end(va);
}

void comment(void)
{
    int ch;

    while ((ch = GETC()) != EOF)
	if (ch == EOL)
	    return;
}

void stag_repl(int ch)
{
    char name[NODENAME_LEN+1];
    char *p = name;
    cmark_node_type nt;
    cmark_strbuf repl;
    
    *p++ = toupper(ch);
    
    while ((ch = GETC()) != EOF && ch != '>')
	if (ISNMCHAR(ch))
	    *p++ = toupper(ch);
	else
	    syntax_error("'%c': Not a NMCHAR.\n", ch);    
	    
    *p = NUL;
    
    for (nt = CMARK_NODE_FIRST_BLOCK; nt <= CMARK_NODE_LAST_INLINE; ++nt)
	if (!stricmp(nodename[nt], name))
	    break;
    if (nt > CMARK_NODE_LAST_INLINE)
	syntax_error("\"%s\": Not a node type.", name);

    cmark_strbuf_init(&repl, 64);
    
    while ((ch = GETC()) != EOF && ISSPACE(ch))
	;
	
    if (ch != '"') {
	UNGETC(ch);
	return;
    }
    
string:
    while ((ch = GETC()) != EOF && ch != '"') {
	switch (ch) {
	case '\\':
	    ch = GETC();
	    switch (ch) {
	    case '\\': cmark_strbuf_putc(&repl, '\\');
	    case 'n': cmark_strbuf_putc(&repl, '\n');
	    case 'r': cmark_strbuf_putc(&repl, '\r');
	    case 's': cmark_strbuf_putc(&repl,  SP );
	    case 't': cmark_strbuf_putc(&repl, '\t');
	    case '[': cmark_strbuf_putc(&repl, '[' );
	    case '"': cmark_strbuf_putc(&repl, '"' );
	    default:  cmark_strbuf_putc(&repl, '\\');
	              cmark_strbuf_putc(&repl,  ch );
            }
            break;
	case '[':
	    cmark_strbuf_putc(&repl, SOH);
	    while ((ch = GETC()) != EOF && ch != ']')
		cmark_strbuf_putc(&repl, ch);
	    cmark_strbuf_putc(&repl, NUL);
	    break;
	default:
	    cmark_strbuf_putc(&repl, ch);
	    break;
	}
    }
    
    while ((ch = GETC()) != EOF && ISSPACE(ch))
	;
	
    if (ch == '"')
	goto string;
	    
    UNGETC(ch);
    cmark_strbuf_putc(&repl, NUL);
    
    set_trans(nt, STAG_BIT, repl.ptr);
}

void etag_repl(int ch)
{
    char name[NODENAME_LEN+1];
    char *p = name;
    cmark_node_type nt;
    cmark_strbuf repl;
    
    *p++ = toupper(ch);
    
    while ((ch = GETC()) != EOF && ch != '>')
	if (ISNMCHAR(ch))
	    *p++ = toupper(ch);
	else
	    syntax_error("'%c': Not a NMCHAR.\n", ch);    
    *p = NUL;
    
    for (nt = CMARK_NODE_FIRST_BLOCK; nt <= CMARK_NODE_LAST_INLINE; ++nt)
	if (!strcmp(nodename[nt], name))
	    break;
    if (nt > CMARK_NODE_LAST_INLINE)
	syntax_error("\"%s\": Not a node type.", name);
    
    cmark_strbuf_init(&repl, 64);
    
    while ((ch = GETC()) != EOF && ISSPACE(ch)) 
	;
	
    if (ch != '\"') {
	UNGETC(ch);
	return;
    }
	
string:
    while ((ch = GETC()) != EOF && ch != '"') {
	switch (ch) {
	case '\\':
	    ch = GETC();
	    switch (ch) {
	    case '\\': cmark_strbuf_putc(&repl, '\\');
	    case 'n':  cmark_strbuf_putc(&repl, '\n');
	    case 'r':  cmark_strbuf_putc(&repl, '\r');
	    case 's':  cmark_strbuf_putc(&repl,  SP );
	    case 't':  cmark_strbuf_putc(&repl, '\t');
	    case '[':  cmark_strbuf_putc(&repl, '[' );
	    case '"':  cmark_strbuf_putc(&repl, '"' );
	    default:   cmark_strbuf_putc(&repl, '\\');
	               cmark_strbuf_putc(&repl,  ch );
            }
            break;
	default:
	    cmark_strbuf_putc(&repl, ch);
	    break;
	}
    }
    
    while ((ch = GETC()) != EOF && ISSPACE(ch))
	;
	
    if (ch == '"')
	goto string;
	    
    UNGETC(ch);
    cmark_strbuf_putc(&repl, NUL);
    
    set_trans(nt, ETAG_BIT, repl.ptr);
}

void setup(const char *repl_filename)
{
    int ch;
    
    filename = repl_filename;
    fp = fopen(filename, "r");
    
    if (fp == NULL)
	error("Can't open repl file \"%s\": %s.", strerror(errno));
    
    COUNT_EOL();
    
    cmark_strbuf_init(&textbuf, INIT_SIZE);

    while ((ch = GETC()) != EOF) 
	if (ISSPACE(ch))
	    continue;
	else if (ch == '<') {
    	    if ((ch = GETC()) == '/') {
    		ch = GETC();
    		if (ISNMSTART(ch))
    		    etag_repl(ch);
		else
		    syntax_error("\'%c\' after '</': Not a NMSTART.\n", ch);
	    } else if (ISNMSTART(ch)) {
    		stag_repl(ch);
	    } else
		syntax_error("\'%c\' after '<': Not a NMSTART.\n", ch);
	} else if (ch == '%')
	    comment();
	else
	    syntax_error("Unexpected character \'%c\'.\n", ch);
		
    fclose(fp);
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
  
  if (argc <= 2) {
      print_usage();
      exit(EXIT_FAILURE);
  }
  setup(argv[1]);
  
  meta.css = NULL;
  meta.title = NULL;
  
  for (argi = 2; argi < argc; ++argi) {
    if (strcmp(argv[argi], "--version") == 0) {
      printf("cmark %s", CMARK_VERSION_STRING
                                    " ( %s %s )\n",
                                    cmark_repourl, cmark_gitident);
      printf(" - CommonMark converter\n(C) 2014, 2015 John MacFarlane\n");
      exit(EXIT_SUCCESS);
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
      exit(EXIT_SUCCESS);
    } else if (*argv[argi] == '-') {
      print_usage();
      exit(EXIT_FAILURE);
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
    
    if (fp == NULL)
      error("Can't open \"%s\": %s\n", argv[argi], strerror(errno));

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
  outfp = stdout;
  gen_document(document, options, &meta);

  /*
   * Discard the document tree.
   */
  cmark_node_free(document);

  return EXIT_SUCCESS;
}
