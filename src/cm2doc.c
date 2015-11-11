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

/*#include "houdini.h"*/
/*#include "scanners.h"*/


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
 * For each CommonMark node type we define a GI conforming to the
 * ISO 8879 SGML Reference Concrete Syntax, which has:
 *
 *     NAMING LCNMSTRT ""
 *            UCNMSTRT ""
 *            LCNMCHAR "-."
 *            UCNMCHAR "-."
 *            NAMECASE GENERAL YES
 *                     ENTITY  NO
 *
 * The Reference Quantity Set also sets NAMELEN to 8, so these GIs are
 * somewhat shorter than the ones in the CommonMark DTD -- which is
 * a good thing IMO.
 *
 * (All this is of course purely cosmetic and/or a nod to SGML, where
 * all this "structural mark-up" stuff came from. -- You could define
 * and use any GI and any NMSTART / NMCHAR character classes you want
 * for giving names to the CommonMark node types.)
 */
#define NAMELEN    8
#define ATTCNT    40
#define ATTSPLEN 960

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
 
#define NUL  0
#define SOH  1

#define HT   9 /* SEPCHAR */
#define LF  10 /* RS */
#define CR  13 /* RE */
#define SP  32 /* SPACE */

#define EOL          LF
#define ATTR_SUBST   SOH

#define RE           LF
#define RS           CR
#define SPACE        SP
#define ISSEPCHAR(C) ((C) == HT)

#define NTS (~0U)

#define ISDIGIT(C)   ( '0' <= (C) && (C) <= '9' )
#define ISHEX(C)     ( ISDIGIT(C) || \
                      ('A'<=(C) && (C)<='F') || ('a'<=(C) && (C)<='f') )
#define ISSPACE(C)   ( (C) == RS || (C) == RE || (C) == SPACE || \
                                                          ISSEPCHAR(C) )
#define ISNMSTART(C) ( ISDIGIT(C) || ISUCNMSTRT(C) || ISLCNMSTRT(C) )
#define ISNMCHAR(C)  ( ISNMSTART(C) || (C) == '-' || (C) == '.' )

FILE *outfp;

FILE *replfp         = NULL;
const char *filename = "";
unsigned lineno      = 0U;
unsigned colno       = 0U;

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
 
#define STAG_REPL 001
#define ETAG_REPL 002
#define STAG_BOL  010
#define ETAG_BOL  020

typedef size_t textidx_t; /* Index into textbuf */
typedef size_t attridx_t; /* Index into attrbuf */
static const textidx_t NULLIDX = 0U;

struct trans_ {
  const char* stag_repl;
  const char* etag_repl;
  unsigned defined; /* STAG_REPL, ETAG_REPL, STAG_BOL, ETAG_BOL. */
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
 * We "misuse" a `cmark_strbuf` here to store a growing array
 * of `attridx_t` (not `char`) elements. There are no alignment issues
 * as long as the array stays homogenuous, as the buffer is from
 * `malloc()`, and thus suitably aligned.
 *
 * These `attridx_t` array members are indices into the `attrbuf`
 * buffer and come in pairs:
 *
 *     elem[2*n+0]: Start index of attribute name.
 *     elem[2*n+1]: Start index of attribute value.
 *
 * An attribute name index of 0U marks the end of the attribute
 * list (of the currently active node).
 */
static cmark_strbuf attsbuf;
static size_t natts = 0U;
/*
 * The elem[k] in the `attsbuf` array as an lvalue.
 */
#define ATTS(K) ( *((attridx_t*)attsbuf.ptr+(K)) )

/*
 * Append one more `attridx_t` element to the `attsbuf` array.
 */
#define PUT_ATTS(I) ( cmark_strbuf_put(&attsbuf, \
                              (unsigned char*)&(I), sizeof(attridx_t)) )

/*
 * Remove pairs (ie "pop") from the end of the `attsbuf` array until 
 * (0U, 0U) pair is gone.
 */
 
#define POP_ATTS() do while (natts > 1U) {   \
	attridx_t i; --natts;		    \
	i = ATTS(2*(natts));		    \
	if (i == 0U) break;		    \
    } while (0)

/*
 * Add a translation to the table.
 */
void set_trans(cmark_node_type nt,
	   unsigned tag_bit, /* STAG_REPL or ETAG_REPL */
           const char *repl)
{
    switch (tag_bit) {
    case STAG_REPL: trans[nt].stag_repl = repl; break;
    case ETAG_REPL: trans[nt].etag_repl = repl; break;
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

#define reset_atts() do {             \
	natts = 0U;                   \
	cmark_strbuf_clear(&attrbuf); \
	cmark_strbuf_clear(&attsbuf); \
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
    
    PUT_ATTS(nameidx);
    PUT_ATTS(validx);
    ++natts;
}

#if 0
void push_atts(const char **atts)
{
    size_t k;
    
    if (atts != NULL) for (k = 0; atts[2*k] != NULL; ++k)
	push_att(atts[2*k+0], atts[2*k+1], NTS);
	    
}
#endif

/*
 * Find attribute in active input element (ie in buf_atts),
 * return the value.
 */
const char* attval(const char *name)
{
    size_t k;
    
    for (k = natts-1U; k > 0U; --k) {
	attridx_t iname = ATTS(2*k+0U), ival = ATTS(2*k+1U);
	if (iname == 0U)
	    break; /* List end for current element */
	if (!strcmp(attrbuf.ptr+iname, name))
	    return attrbuf.ptr+ival;
    }
	    
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

void put_repl(const char *repl)
{
    const char *p = repl;
    char ch;
    
    while ((ch = *p++) != NUL) {
	if (ch != ATTR_SUBST)
	    putc(ch, outfp);
	else {
	    const char *name = p;
	    p = name + strlen(name)+1U;
	    putval(name);
	}
    }
}

void do_Start(cmark_node_type nt)
{
    const struct trans_ *tr = &trans[nt];
    
    if (tr->defined & STAG_REPL)
	put_repl(tr->stag_repl);
    
    PUT_ATTS(NULLIDX); /* Must use a lvalue here. */
    PUT_ATTS(NULLIDX);
    ++natts;
}

void do_Empty(cmark_node_type nt)
{
    do_Start(nt);
    
    POP_ATTS();
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
    
    POP_ATTS();
    
    if (tr->defined & ETAG_REPL) 
	put_repl(tr->etag_repl);
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
      do_Start(node->type);
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
      do_Start(node->type);
      break;

    case CMARK_NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      do_Attr("level", buffer, NTS);
      do_Start(node->type);
      break;

    case CMARK_NODE_CODE_BLOCK:
      if (node->as.code.info.len > 0U)
        do_Attr("info", 
		       node->as.code.info.data, node->as.code.info.len);
      do_Start(node->type);
      do_Cdata(
	         node->as.code.literal.data, node->as.code.literal.len);
      do_End(node->type);
      break;

    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      do_Attr("destination", node->as.link.url.data, node->as.link.url.len);
      do_Attr("title", node->as.link.title.data, node->as.link.title.len);
      do_Start(node->type);
      break;

    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      do_Empty(node->type);
      break;

    case CMARK_NODE_DOCUMENT:
    default:
      do_Start(node->type);
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
            
        ibol = (p - buffer) + 1U; /* One after '\n'. */
        
        /*
         * We copy buffer[ifield .. ibol-2], ie the line content
         * from ifield to just before the '\n', and append a NUL 
         * terminator, of course.
         */
        nalloc = ibol - ifield;
        if (nalloc > 1U) {
            char *pitem = malloc(nalloc);
            if (pitem == NULL)
    		break;
            memcpy(pitem, buffer+ifield, nalloc-1U);
            pitem[nalloc-1U] = '\0';
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
 

#define COUNT_EOL() (++lineno, colno = 0U)

#define GETC()     ( ch = getc(replfp),			    \
                     ((ch == EOL) ? COUNT_EOL() : ++colno), \
                     ch )
                   
#define UNGETC(ch) ( ungetc(ch, replfp),		    \
                     --colno,				    \
                     ch )

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

void tag_repl(int ch, unsigned bits)
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
    
    /*
     * Look up the "GI" for a CommonMark node type.
     */
    for (nt = CMARK_NODE_FIRST_BLOCK; nt <= CMARK_NODE_LAST_INLINE; ++nt)
	if (!strcmp(nodename[nt], name))
	    break;
    if (nt > CMARK_NODE_LAST_INLINE) {
	syntax_error("\"%s\": Not a CommonMark node type.", name);
	return;
    }

    cmark_strbuf_init(&repl, 16);
    
    while (ch != EOF) {

	while ((ch = GETC()) != EOF) {
	    if (ISSPACE(ch))
		continue;
	    else if (ch == '%') {
		comment();
		continue;
	    } else if (ch == '"') {
		break;
	    }
	    
	    if (ch != '<') {
		syntax_error("'%c': Unexpected.\n", ch);
		continue;
	    }
	    
	    UNGETC(ch);
	    goto done;
	}
    
	while ((ch = GETC()) != EOF && ch != '"') {
	    switch (ch) {
	    case '\\':
		ch = GETC();
		switch (ch) {
		case '\\': ch = '\\'; break;
		case  'n': ch = '\n'; break;
		case  'r': ch = '\r'; break;
		case  's': ch =  SP ; break;
		case  't': ch = '\t'; break;
		case  '[': ch = '[' ; break;
		case  '"': ch = '"' ; break;
		default:   cmark_strbuf_putc(&repl, '\\');
		}
		cmark_strbuf_putc(&repl, ch);
		break;
	    case '[':
		cmark_strbuf_putc(&repl, ATTR_SUBST);
		while ((ch = GETC()) != EOF && ch != ']') {
		    if (ch == '"') {
			syntax_error("Unclosed attribute reference (missing ']').\n");
			UNGETC(ch);
			ch = ']';
		    } else if (ISSPACE(ch))
			syntax_error("Space in attribute name.\n");
		    else
			cmark_strbuf_putc(&repl, ch);
		}
		cmark_strbuf_putc(&repl, NUL);
		break;
	    default:
		cmark_strbuf_putc(&repl, ch);
		break;
	    }
	}
	
    }
    
done:
    if (repl.size > 0U) {
	cmark_strbuf_putc(&repl, NUL);
    
	set_trans(nt, bits, repl.ptr);
    } else
	set_trans(nt, bits, NULL);
}


void setup(const char *repl_filename)
{
    int ch;
    
    filename = repl_filename;
    replfp = fopen(filename, "r");
    
    if (replfp == NULL)
	error("Can't open replacement file \"%s\": %s.",
	                                               strerror(errno));
    
    COUNT_EOL();
    
    cmark_strbuf_init(&textbuf, INIT_SIZE);
    cmark_strbuf_init(&attrbuf, ATTSPLEN);
    cmark_strbuf_putc(&attrbuf, NUL); /* Ensure index = 0U is unsused.*/
    cmark_strbuf_init(&attsbuf, ATTCNT * 2 * sizeof(attridx_t) + 2);
    PUT_ATTS(NULLIDX);
    PUT_ATTS(NULLIDX);
    ++natts;

    while ((ch = GETC()) != EOF) 
	if (ISSPACE(ch))
	    continue;
	else if (ch == '<') {
    	    if ((ch = GETC()) == '/') {
    		ch = GETC();
    		if (ISNMSTART(ch))
    		    tag_repl(ch, ETAG_REPL);
		else
		    syntax_error("\'%c\' after '</': Not a NMSTART.\n",
		                                                    ch);
	    } else if (ISNMSTART(ch)) {
    		tag_repl(ch, STAG_REPL);
	    } else
		syntax_error("\'%c\' after '<': Not a NMSTART.\n", ch);
	} else if (ch == '%')
	    comment();
	else
	    syntax_error("Unexpected character \'%c\'.\n", ch);
		
    fclose(replfp);
    replfp = NULL;
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
  outfp = stdout;
  
  in_header = true;
  
  switch (argc - argi) do {
  default:
    fp = freopen(argv[argi], "r", stdin);
    
    if (fp == NULL)
      error("Can't open \"%s\": %s\n", argv[argi], strerror(errno));

  case 0:
    while ((bytes = fread(buffer, 1U, sizeof buffer, fp)) > 0) {
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
  gen_document(document, options, &meta);

  /*
   * Discard the document tree.
   */
  cmark_node_free(document);

  return EXIT_SUCCESS;
}
