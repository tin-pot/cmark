/*== cm2doc.c =========================================================*

NAME
    cm2doc -
	    .
            .

SYNOPSIS
    cm2doc [ cmark-opts ] [(--title | -t) string] [(--css | -c) url]
           [ --rast | { ( --repl | -r ) replfile } ] file ...

DESCRIPTION
	...
	...
	...

	 1. ...

	 2. ...
	    ...


OPTIONS
    --rast
	...

    --repl
    -r

    --title
    -t
	...

    --css
    -c
	...

EXIT STATUS
	...
	...

ENVIRONMENT
	XXX	...
		...

BUGS
	...
	...
	...


------------------------------------------------------------------------

COPYRIGHT NOTICE AND LICENSE

Copyright (C) 2015 Martin Hofmann <mh@tin-pot.net>

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions 
are met:

   1. Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copy-
      right notice, this list of conditions and the following dis-
      claimer in the documentation and/or other materials provided 
      with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY 
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLU-
DING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*=====================================================================*/

#if !defined(NDEBUG) && defined(_MSC_VER)
#include <CrtDbg.h>
#define BREAK() _CrtDbgBreak()
#endif
#include <assert.h>

#include <ctype.h> /* toupper() */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef _MSC_VER
#define snprintf _snprintf
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>  /* strftime(), gmtime(), time() */

#include "octetbuf.h"
#include "escape.h"
#include "xchar.h" /* Unicode utilities - UTF-8 conversion. */

/*
 * CommonMark library
 */
#include "config.h"
#include "cmark.h"
#include "cmark_ctype.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"


#define NUL  0

#define U(X)                    ((xchar_t)(0x ## X ##UL))
#define BMP(X)                  ((xchar_t)((X) & 0xFFFFUL))

/*
 * Prototypes here, because of ubiquituous use.
 */
void error(const char *msg, ...);
void syntax_error(const char *msg, ...);

/*== ESIS API ========================================================*/

/*
 * Callback function types for document content transmittal.
 */

/*
 * A convenience mechanism: In an API using arguments like
 *
 *     ..., char *data, size_t len, ...
 *
 * placing the value NTS in the actual parameter `len` indicates that
 * `data` is a NUL-terminated (byte) string aka NTBS, so the *callee*
 * can determine the correct value for `len` from `data`.
 */
#ifndef SIZE_MAX /* C99: in <stdint.h> (but in <limits.h> in MSC!) */
#define SIZE_MAX    (~(size_t)0U)
#endif
#ifndef NTS
#define NTS SIZE_MAX
#endif

typedef void *ESIS_UserData;

typedef void (ESIS_Attr)(ESIS_UserData,    const char *name,
                                           const char *val, size_t);
typedef void (ESIS_Start)(ESIS_UserData,               cmark_node_type);
typedef void (ESIS_Cdata)(ESIS_UserData,   const char *cdata, size_t);
typedef void (ESIS_End)(ESIS_UserData,                 cmark_node_type);

typedef struct ESIS_CB_ {
    ESIS_Attr	 *attr;
    ESIS_Start	 *start;
    ESIS_Cdata	 *cdata;
    ESIS_End	 *end;
} ESIS_CB;

typedef struct ESIS_Port_ {
    const ESIS_CB *cb;
    ESIS_UserData  ud;
} ESIS_Port;

/*
 * We have:
 *
 *  - input file parsers,
 *  - output file generators, and
 *  - ESIS filters.
 */
 
int        parse_cmark(FILE *from, ESIS_Port *to, cmark_option_t,
                                                    const char *meta[]);
                                   
int        parse_esis(FILE *from, ESIS_Port *to, unsigned options);

ESIS_Port* generate_repl(FILE *to,               unsigned options);
ESIS_Port* generate_rast(FILE *to,               unsigned options);
ESIS_Port* generate_esis(FILE *to,               unsigned options);

ESIS_Port* filter_toc(ESIS_Port*to,              unsigned options);

#define DO_ATTR(N, V, L)   esis_cb->attr(esis_ud, N, V, L)
#define DO_START(NT)       esis_cb->start(esis_ud, NT)
#define DO_CDATA(D, L)     esis_cb->cdata(esis_ud, D, L)
#define DO_END(NT)         esis_cb->end(esis_ud, NT)


/*== cmark_strbuf_dup() ==============================================*/

char *cmark_strbuf_dup(cmark_strbuf *pbuf)
{
    size_t n = pbuf->size;
    char *p;
    
    if (n == 0U) return NULL;
    p = malloc(n);
    if (p != NULL) {
	memcpy(p, pbuf->ptr, n);
	cmark_strbuf_clear(pbuf);
    } else {
	error("Out of memory! Allocating %lu bytes failed.\n", n);
    }
    return p;
}

/*== Meta-data =======================================================*/

/*
 * Names of environment variables used to 
 *  1. Point to a directory used in the search path.
 *  2. Point to a default replacement file if none given otherwise.
 */
#define REPL_DIR_VAR     "REPL_DIR"
#define REPL_DEFAULT_VAR "REPL_DEFAULT"

/*
 * Optionally make the Git commit ident and the repository URL 
 * available as character strings.
 *
 * Use -DWITH_GITIDENT=1 to switch this on (and have the strings
 * ready for the linker to find them!); or do nothing and use the
 * placeholder values given below. 
 */
 
#if WITH_GITIDENT
extern const char cmark_gitident[];
extern const char cmark_repourl[];
#else
static const char cmark_gitident[] = "n/a";
static const char cmark_repourl[]  = "https://github.com/tin-pot/cmark";
#endif

/*
 * Predefined "pseudo-attribute" names, usable in the "replacement" text
 * for #PROLOG (and for the document element), eg to set <META> ele-
 * ments in an HTML <HEAD>.
 *
 * At compile time, these names are accessible through META_... macros.
 *
 * NOTE: We use a "pseudo-namespace" for "cm2doc" (and "cmark")
 * specific "pseudo-attributes", to avoid any conflict with real
 * attributes in a document type.
 *
 * The first three are from Dublin Core, and can be set in the first
 * lines of the CommonMark input document by placing a PERCENT SIGN
 * at the very beginning of the line:
 *
 *
 *     % The Document Title
 *     % A. U. Thor
 *     % 2015-11-11T11:11:11+11
 *
 * In subsequent lines, you can set "user-defined" attributes for
 * use in the prolog, like:
 *
 *     % foo-val: Foo value
 *     % bar.val: Bar value
 *
 * *but* you can't use COLON ":" **in** the attribute name for obvious
 * reasons. (Maybe ending the attribute name at (the first) COLON
 * followed by SPACE would be a more reasonable approach ...).
 */
 /*TODO: Colon in meta attribute names `% bar:val: Bar value` */
 
#define META_DC_TITLE   "DC.title"
#define META_DC_CREATOR "DC.creator"
#define META_DC_DATE    "DC.date"
#define META_CSS        "CM.css"

/*
 * Default values for the "pseudo-attributes".
 *
 * At compile time, they are accessible through DEFAULT_... macros.
 */

/* Data and creator will be re-initialized in main(). */
static char default_date[11]    = "YYYY-MM-DD";
static char default_creator[81] = "N.N.";

#define DEFAULT_DC_CREATOR  default_creator
#define DEFAULT_DC_DATE     default_date

/* Hard-coded defaults for command-line options --title and --css. */
#define DEFAULT_DC_TITLE    "Untitled Document"
#define DEFAULT_CSS         "default.css"

/*== CommonMark Nodes ================================================*/

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
 * (This is replicated in the IS...() character class macros below.)
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
#define NAMELEN      8 /* The Reference Core Syntax value. */
#define ATTCNT      40 /* The Reference Quantity Set value. */
#define ATTSPLEN   960 /* The Reference Quantity Set value. */

#define ISUCNMSTRT(C) ( 'A' <= (C) && (C) <= 'Z' )
#define ISLCNMSTRT(C) ( 'a' <= (C) && (C) <= 'z' )
#define ISUCNMCHAR(C) ( ISUCNMSTRT(C) || (C) == '-' || (C) == '.' )
#define ISLCNMCHAR(C) ( ISLCNMSTRT(C) || (C) == '-' || (C) == '.' )

/* How many node types there are, and what the name length limit is. */
#define NODE_NUM       (CMARK_NODE_LAST_INLINE+2)
#define NODE_MARKUP     CMARK_NODE_LAST_INLINE+1
#define NODENAME_LEN    NAMELEN

static const char* const nodename[NODE_NUM+1] = {
     NULL,	/* The "none" type (enum const 0) is invalid! */
   /*12345678*/
    "CM.DOC",
    "CM.QUO-B",
    "CM.LIST",
    "CM.LI",
    "CM.COD-B",
    "CM.HTM-B",
    "CM.PAR",
    "CM.HDR",
    "CM.HR",
    "CM.TXT",
    "CM.SF-BR",
    "CM.LN-BR",
    "CM.COD",
    "CM.HTM",
    "CM.EMPH",
    "CM.STRN",
    "CM.LNK",
    "CM.IMG",
    "MARKUP"
};


/*== Replacement Backend =============================================*/

/*
 * "Reserved Names" to bind special "replacement texts" to:
 * The output document's prolog (and, if needed, epilog).
 */
enum rn_ {
    RN_INVALID,
    RN_PROLOG,
    RN_EPILOG,
    RN_NUM,	/* Number of defined "reserved names". */
};

static const char *const rn_name[] = {
    NULL,
    "PROLOG",
    "EPILOG",
    NULL
};
    
static const char *rn_repl[RN_NUM]; /* Replacement texts for RNs. */

/*--------------------------------------------------------------------*/

/*
 * Some C0 control characters (internally used to encode the 
 * replacement texts).
 */
 
#define SOH  1
#define STX  2
#define ETX  3
#define EOT  4
#define VT  11 /* Encodes the begin-of-line "+". */
#define SO  14 /* Encodes the attribute substitution "[". */
#define SI  15 /* Encodes the attribute substitution "]". */

/*
 * The C0 control characters allowed in SGML/XML; all other C0 are
 * **not** usable in a document, and thus free for our private use.
 */
#define HT   9	/* SGML SEPCHAR */
#define LF  10	/* SGML RS */
#define CR  13	/* SGML RE */
#define SP  32	/* SGML SPACE */

#define EOL          LF	    /* Per ISO C90 text stream. */
 
/*--------------------------------------------------------------------*/

/*
 * SGML function characters, character classes, and delimiters.
 */
#define RE           LF
#define RS           CR
#define SPACE        SP
#define ISSEPCHAR(C) ((C) == HT)

#define MSSCHAR      '\\'          /* Markup-scan-suppress character. */

#define LIT          "\""
#define LITA         "\'"

#define STAGO        "<"
#define ETAGO        "</"
#define TAGC         ">"
#define PLUS         "+"
#define COM          "--"
#define RNI          "#"
#define VI           "="        
#define DSO          "["
#define DSC          "]"

#define ISDIGIT(C)   ( '0' <= (C) && (C) <= '9' )
#define ISHEX(C)     ( ISDIGIT(C) || \
                      ('A'<=(C) && (C)<='F') || ('a'<=(C) && (C)<='f') )
#define ISSPACE(C)   ( (C) == RS || (C) == RE || (C) == SPACE || \
                                                          ISSEPCHAR(C) )
#define ISNMSTART(C) ( ISDIGIT(C) || ISUCNMSTRT(C) || ISLCNMSTRT(C) )
#define ISNMCHAR(C)  ( ISNMSTART(C) || (C) == '-' || (C) == '.' )

/*
 * NOTATION_DELIM is (currently pre-defined to be) 
 *
 *     U+00B4 ACUTE ACCENT (decimal 180)
 *
 * It is used to put an "info string" into an *inline* code span
 * like this:
 *
 *     dolor sit amet, `ZÂ´x %e %N` consectetuer adipiscing elit. 
 *
 * The ACUTE ACCENT was chosen to match the GRAVE ACCENT, as the
 * "backtick" is officially called, and to make for a relatively
 * unobtrusive syntax.
 *
 * This *inline* "info string" has the exact same meaning as the
 * standard "info string" on a code block fence:
 *
 *     ~~~ Z
 *     x %e %N
 *     ~~~
 *
 * Both examples produce (in the HTML output file):
 *
 *     <MARKUP notation="Z" ...>x %e %N</MARKUP>
 *
 * but the *inline* code span produces the attribute `display="inline"`,
 * while the fenced code *block* gives `display="block"` as the second
 * attribute in the `<MARKUP>` element.
 *
 * NOTE: A convenient way to enter ACUTE ACCENT using an US keyboard
 * layout in Vim is to enter the *digraph* ( Ctrl-K , "'" , "'" ) for
 * it.
 */
 
#if 0
#define NOTA_DELIM_0 '\xC2' /* UTF-8[0] of U+00B4 ACUTE ACCENT (180) */
#define NOTA_DELIM_1 '\xB4' /* UTF-8[1] of U+00B4 ACUTE ACCENT (180) */
#else
#define NOTA_DELIM_0 '|'
#define NOTA_DELIM_1 NUL
#endif

/*== Replacement Definitions =========================================*/

/*
 * Replacement definitions for a node type are hold in a 
 * `struct repl_`
 */
 
#define STAG_REPL       0x0001
#define ETAG_REPL       0x0002
#define STAG_BOL_START  0x0010
#define STAG_BOL_END    0x0020
#define ETAG_BOL_START  0x0040
#define ETAG_BOL_END    0x0080

typedef size_t textidx_t;  /* Index into cmark_strbuf text_buf. */

struct taginfo_ {
    cmark_node_type nt;
    textidx_t atts[2*ATTCNT + 2];
};

struct repl_ {
    struct taginfo_ taginfo;
    const char     *repl[2];
    bool            is_cdata;
    struct repl_   *next;
};

/*
 * Replacement definitions: one array member per node type,
 * plus the (currently unused) member at index 0 == NODE_NONE.
 */
 
static struct repl_ *repl_tab[NODE_NUM];

cmark_strbuf text_buf;



/*== Element Stack Keeping ===========================================*/

typedef size_t nameidx_t; /* Index into nameidx_buf. */
typedef size_t validx_t;  /* Index into validx_buf. */

static const size_t NULLIDX = 0U; /* Common NULL value for indices. */

/*
 * Attribute names and values of current node(s).
 */
static cmark_strbuf attr_buf;

/*
 * We "misuse" a `cmark_strbuf` here to store a growing array
 * of `attridx_t` (not `char`) elements. There are no alignment issues
 * as long as the array stays homogenuous, as the buffer is from
 * `malloc()`, and thus suitably aligned.
 *
 * An attribute name index of 0U marks the end of the attribute
 * list (of the currently active node).
 */
static cmark_strbuf nameidx_buf;
static cmark_strbuf validx_buf;
#define NATTR ( nameidx_buf.size / sizeof NAMEIDX[0] )

/*
 * The name index and value index arrays as seen as `nameidx_t *`
 * and `validx_t *` rvalues, ie as "regular C arrays".
 */
#define NAMEIDX ( (nameidx_t*)nameidx_buf.ptr )
#define VALIDX  (  (validx_t*)validx_buf.ptr )

/*
 * Append one `nameidx_t` element to the `NAMEIDX` array, and 
 * dito for `validx_t` and the `VALIDX` array.
 */
#define PUT_NAMEIDX(I) ( cmark_strbuf_put(&nameidx_buf, \
                              (unsigned char*)&(I), sizeof(nameidx_t)) )
#define PUT_VALIDX(I)  ( cmark_strbuf_put(&validx_buf, \
                              (unsigned char*)&(I), sizeof(validx_t)) )

/*
 * We use NULLIDX to delimit "activation records" for the currently
 * open elements: the first invocation of `push_att()` after a 
 * `lock_attr()` call will push a NULLIDX first, then the given
 * attribute name and value into a "new" activation record, while
 * "unlocking" the new activation record.
 */
#define close_atts(NT)   ( PUT_NAMEIDX(NULLIDX), PUT_VALIDX(NT) )
#define pop_atts()       POP_ATTS()
#define current_nt()     ( VALIDX[NATTR-1U] )

void push_att(const char *name, const char *val, size_t len)
{
    nameidx_t nameidx;
    validx_t  validx;   
    
    if (len == NTS) len = strlen(val);
    
    nameidx = attr_buf.size;
    cmark_strbuf_puts(&attr_buf, name);
    cmark_strbuf_putc(&attr_buf, NUL);
    
    validx = attr_buf.size;
    cmark_strbuf_put (&attr_buf, val, len);
    cmark_strbuf_putc(&attr_buf, NUL);
    
    PUT_NAMEIDX(nameidx);
    PUT_VALIDX(validx);
}

/*
 * Remove the current activation record. 
 */
 
#define POP_ATTS()							\
do {									\
    size_t top = NATTR;							\
    if (top > 0U) {							\
	nameidx_t nameidx = NAMEIDX[--top];				\
	assert(nameidx == NULLIDX);					\
	do {								\
	    nameidx_buf.size -= sizeof NAMEIDX[0];			\
	    validx_buf.size  -= sizeof VALIDX[0];			\
	    if (nameidx != NULLIDX) attr_buf.size = nameidx;		\
	} while (top > 0U && (nameidx = NAMEIDX[--top]) != NULLIDX);	\
    }									\
    assert(top == NATTR);						\
} while (0)

#if 1
#undef pop_atts
#endif

void pop_atts(void)
{
    nameidx_t nameidx = 0U;
    
    size_t top = NATTR;
    if (top > 0U) {
	nameidx           = NAMEIDX[--top];
	assert(nameidx == NULLIDX);
	do {
	    nameidx_buf.size -= sizeof NAMEIDX[0];
	    validx_buf.size  -= sizeof VALIDX[0];
	    assert(top == NATTR);
	    if (nameidx != NULLIDX) attr_buf.size = nameidx;
	} while (top > 0U && (nameidx = NAMEIDX[--top]) != NULLIDX);
    }
}


/*
 * Find attribute in active input element (ie in buf_atts),
 * return the value.
 */
 
const char* att_val(const char *name, unsigned depth)
{
    size_t k;
    
    assert ((k = NATTR) > 0U);
    assert (NAMEIDX[k-1U] == NULLIDX);
    assert (depth > 0U);
	
    if (depth == 0U) return NULL;
    
    for (k = NATTR; k > 0U; --k) {
	nameidx_t nameidx;
	validx_t validx;
	const size_t idx = k-1U;
	
	assert(nameidx_buf.size >= 0);
	assert(nameidx_buf.size == 0 || nameidx_buf.ptr != NULL);
	assert(idx*sizeof *NAMEIDX < (size_t)nameidx_buf.size);
	
	nameidx = NAMEIDX[idx];
	
	assert(attr_buf.size >= 0);
	assert(attr_buf.size == 0 || attr_buf.ptr != NULL);
	assert(nameidx < (size_t)attr_buf.size);
	
	if (nameidx == NULLIDX && depth-- == 0U)
	    break;
	    
	assert(validx_buf.size >= 0);
	assert(validx_buf.size == 0  || validx_buf.ptr != NULL);
	assert(idx*sizeof *VALIDX < (size_t)validx_buf.size);
	
	validx = VALIDX[idx];
	
	assert(validx  < (size_t)attr_buf.size);
	if (!strcmp(attr_buf.ptr+nameidx, name))
	    return attr_buf.ptr+validx;
    }
	    
    return NULL;
}


/*== Replacement Definitions =========================================*/


/*
 * Set the replacement text for a node type.
 */
void set_repl(struct taginfo_ *pti,
              const char *repl_text[2],
              bool is_cdata)
{
    cmark_node_type nt = pti->nt;
    struct repl_ *rp = malloc(sizeof *rp);
    
    assert(0 <= nt);
    assert(nt < NODE_NUM);
    
    rp->repl[0]  = repl_text[0];
    rp->repl[1]  = repl_text[1];
    rp->taginfo  = *pti;
    rp->is_cdata = is_cdata;
    rp->next     = repl_tab[nt];
    
    repl_tab[nt] = rp;
}

/*--------------------------------------------------------------------*/

/*
 * The output stream (right now, this is always stdout).
 */
FILE *outfp;
bool  outbol = true;

#define PUTC(ch)							\
do {									\
    putc(ch, outfp);							\
    outbol = (ch == EOL);						\
} while (0)

/*
 * Attribute substitution and replacement text output.
 */

const char *put_subst(const char *p)
{
    const char *name;
    const char *val = NULL;
    unsigned depth = *p & 0xFFU;
    
    assert(p[-1] == SO);
    name = p + 1U;
    p = name + strlen(name)+2U;
    assert(p[-1] == SI);

    val = att_val(name, depth);
    
    if (val != NULL) {
        size_t k;
        for (k = 0U; val[k] != NUL; ++k)
    	PUTC(val[k]);
    } else
	error("Undefined attribute '%s'\n", name);
    
    return p+1U;
}

void put_repl(const char *repl)
{
    const char *p = repl;
    char ch;
    
    assert(repl != NULL);
    while ((ch = *p++) != NUL) {
	if (ch == VT) {
	    if (!outbol)
		PUTC(EOL);
	} else if (ch == SO)
	    p = put_subst(p);
	else 
	    PUTC(ch);
    }
}

struct repl_ *select_rule(cmark_node_type nt)
{
    struct repl_ *rp;
    
    for (rp = repl_tab[nt]; rp != NULL; rp = rp->next) {
	textidx_t *atts = rp->taginfo.atts;
	int i;
	
	for (i = 0; atts[2*i] != NULLIDX; ++i) {
	    const char *name, *sel_val = NULL;
	    const char *cur_val;
	    
	    name = text_buf.ptr + atts[2*i+0];
	    cur_val = att_val(name, 1);
	    if (atts[2*i+1] != NULLIDX) {
		sel_val = text_buf.ptr + atts[2*i+1];
	    }
	    if (sel_val == NULL && cur_val == NULL)
		break; /* Attribute existence mismatch. */
		
	    if (sel_val != NULL && (cur_val == NULL ||
		             strcmp(cur_val, sel_val) != 0))
		break; /* Attribute value mismatched. */
	}
	if (atts[2*i] == NULLIDX)
	    return rp; /* Matched all attribute selectors. */
    }
    return NULL; /* No matching rule found. */
}

/*== ESIS API for the Replacement Backend ============================*/

struct notation_name_ {
    const char *name;
    struct notation_name_ *next;
} *notations = NULL;

bool is_notation(const char *nmtoken, size_t len)
{
    const struct notation_name_ *pn;
    
    if (nmtoken == NULL || len == 0U)
	return false;
	
    for (pn = notations; pn != NULL; pn = pn->next)
	if (strncmp(pn->name, nmtoken, len) == 0)
	    return true;
    return false;
}

void register_notation(const char *nmtoken, size_t len)
{
    struct notation_name_ *pn = malloc(sizeof *pn);
    char *name = malloc(len + 1);
    size_t k;
    
    assert(nmtoken != NULL);
    if (len == NTS) len = strlen(nmtoken);
    assert(len > 0U);
    
    for (k = 0U; k < len; ++k) {
	if (!ISNMCHAR(nmtoken[k]))
	    error("\"%*.s\": Invalid NOTATION name.\n", (int)len,
                                                               nmtoken);
    }
    memcpy(name, nmtoken, len);
    name[len] = NUL;
    pn->name = name;
    pn->next = notations;
    notations = pn;
}

void check_notation(const char *data, size_t len)
{
    static const char mdo[]      = "<!";
    static const size_t mdolen   = sizeof mdo - 1U;
    static const char nword[]    = "NOTATION";
    static const size_t nwordlen = sizeof nword - 1U;
    
    size_t k;
    const char *nmtoken;
    
    if (len < mdolen || strncmp(data, mdo, mdolen) != 0)
	return;
	
    for (k = mdolen; k < len; ++k)
	if (ISSPACE(data[k]))
	    continue;
	else if (k + nwordlen >= len || strncmp(data+k, nword,
	                                                 nwordlen) != 0)
	    return;
	else
	    break;

    if (k + nwordlen >= len)
	return;
	
    for (k += nwordlen; k < len; ++k)
	if (!ISSPACE(data[k]))
	    break;
	    
    if (k >= len)
	return;
	
    nmtoken = data+k;
    for ( ; k < len; ++k)
	if (ISSPACE(data[k]))
	    break;
    
    register_notation(nmtoken, k - (size_t)(nmtoken - data));
}

bool watch_notation = false;

void repl_Attr(ESIS_UserData ud,
                          const char *name, const char *val, size_t len)
{
    push_att(name, val, len);
}

void repl_Start(ESIS_UserData ud, cmark_node_type nt)
{
    const struct repl_ *rp;
    
    close_atts(nt);

/* Check if this is in reality a <!NOTATION markup declaration */
    watch_notation = (nt == NODE_HTML || nt == NODE_INLINE_HTML);

    if (nt != CMARK_NODE_NONE && (rp = select_rule(nt)) != NULL) {
	if (rp->repl[0] != NULL) {
	    put_repl(rp->repl[0]);
	} 
    }
    
    if (nt == NODE_MARKUP && rp == NULL) {
	const char *notation = att_val("notation", 1);
	const char *display  = att_val("display", 1);
        
	assert(notation  != NULL);
	assert(display != NULL);
	fprintf(outfp, "<MARKUP notation=\"%s\" display=\"%s\">",
						  notation, display);
    }
}


void repl_Cdata(ESIS_UserData ud, const char *cdata, size_t len)
{
    static cmark_strbuf houdini = { 0 };
    static int          houdini_init = 0;
    
    cmark_node_type     nt = current_nt();
    const struct repl_ *rp = repl_tab[nt];
    
    size_t      k;
    const char *p;
    
    bool is_cdata;
    
    
    
    if (len == NTS) len = strlen(cdata);
    p = cdata;
    
    is_cdata = rp == NULL || rp->is_cdata;
    if (nt == NODE_TEXT && rp == NULL) is_cdata = false;
    
/*
 * The *first* CDATA line in a CommonMark "HTML" element would contain
 * the MDO right at the start -- and the "HTML" element is just
 * a *notation declaration* in disguise ... 
 */
    switch (nt) case NODE_HTML: case NODE_INLINE_HTML: case NODE_MARKUP:
	if (watch_notation) {
	    check_notation(p, len);
	    watch_notation = false;
	}
    
    if (!is_cdata) {
        if (!houdini_init) {
    	    cmark_strbuf_init(&houdini, 1024);
    	    houdini_init = 1;
        }
        
	houdini_escape_html(&houdini, (uint8_t*)cdata, len);
	p   = cmark_strbuf_cstr(&houdini);
	len = cmark_strbuf_len(&houdini);
	
	for (k = 0U; k < len; ++k)
	    PUTC(p[k]);
    } else if (rp != NULL || nt == NODE_HTML ||
                             nt == NODE_INLINE_HTML) {
	for (k = 0U; k < len; ++k)
	    PUTC(p[k]);
    } else {
	fputs("<![CDATA[", outfp);
	fwrite(p, 1U, len, outfp);
	fputs("]]>", outfp);
	outbol = false;
    }
	
    cmark_strbuf_clear(&houdini);
}

void repl_End(ESIS_UserData ud, cmark_node_type nt)
{
    const struct repl_ *rp = repl_tab[nt];
    
    if (nt != CMARK_NODE_NONE && (rp = select_rule(nt)) != NULL) {
	if (rp->repl[1] != NULL) {
	    put_repl(rp->repl[1]);
	}
    } else if (nt == NODE_MARKUP)
	fprintf(outfp, "</MARKUP>");
	
    pop_atts();
}
 
static const struct ESIS_CB_ repl_CB = {
    repl_Attr,
    repl_Start,
    repl_Cdata,
    repl_End
};

ESIS_Port* generate_repl(FILE *to, unsigned options)
{
    static struct ESIS_Port_ port = { &repl_CB, NULL };
    
    options = 0U; /* NOT USED */
    /*
     * Only one output file at a time.
     */
    assert(port.ud == NULL);
    port.ud = to;
    outfp  = to;
    outbol = true;
    return &port;
}

/*== ESIS API for RAST Output Generator ==============================*/

#define RAST_ALL 1U

struct RAST_Param_ {
    FILE *outfp;
    unsigned options;
} rast_param;



void rast_data(FILE *fp, const char *data, size_t len, char delim)
{
    size_t k;
    int in_special = 1;
    int at_bol = 1;
    
    for (k = 0U; k < len; ++k) {
	int ch = data[k] & 0xFF;
	if (32 <= ch && ch < 128) {
	    if (in_special) {
		if (!at_bol) {
		    fputc(EOL, fp);
		}
		fputc(delim, fp);
		in_special = 0;
		at_bol = 0;
	    }
	    fputc(ch, fp);
	} else {
	    if (!in_special) {
		if (!at_bol) {
		    fputc(delim, fp);
		    fputc('\n', fp);
		}
		in_special = 1;
		at_bol = 1;
	    }
	    if (128 < ch) {
		size_t n = len - k;
		xchar_t c32;
		int i = mbtoxc(&c32, &data[k], n);
		if (i > 0) {
		    fprintf(fp, "#%lu\n", c32);
		    k += i-1;
		} else {
		    unsigned m;
		    n = (unsigned)(-i);
		    if (n == 0) n = 1;
		    
		    for (m = 0; m < n; ++m) {
			fprintf(fp, "#X%02X\n", 0xFFU & data[k+m]);
		    }
		    k = k + m - 1;
		    fprintf(stderr,
		              "Invalid UTF-8 sequence in data line!\n");
		}
	    } else {
		switch (ch) {
		case RS:  fputs("#RS\n", fp);	    break;
		case RE:  fputs("#RE\n", fp);	    break;
		case HT:  fputs("#TAB\n", fp);	    break;
		default:  fprintf(fp, "#%u\n", ch); break;
		}
	    }
	    
	    at_bol = 1;
	}
    }
    if (!in_special) { fputc(delim, fp); at_bol = 0; }
    if (!at_bol) fputc(EOL, fp);
}


void discard_atts(void)
{
    cmark_strbuf_clear(&attr_buf);
    cmark_strbuf_putc(&attr_buf, NUL); /* Index = 0U is unsused.*/
    
    cmark_strbuf_clear(&nameidx_buf);
    cmark_strbuf_clear(&validx_buf);
}

void rast_Attr(ESIS_UserData ud, const char *name, const char *val, size_t len)
{
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    
    if (len == NTS) len = strlen(val);
    
    push_att(name, val, len);
    return;
}

void rast_Start(ESIS_UserData ud, cmark_node_type nt)
{
    struct RAST_Param_* rastp = ud;
    FILE *fp = rastp->outfp;
    size_t nattr = NATTR;
    
    if (nt == 0 && !(rastp->options & RAST_ALL)) {
	discard_atts();
	return;
    }
	
    if (nattr > 0U) {
	size_t k;
	const char *GI = nodename[nt];

	fprintf(fp, "[%s\n", (GI == NULL) ? "#0" : GI);
	for (k = nattr; k > 0U; --k) {
	    nameidx_t nameidx = NAMEIDX[k-1];
	    nameidx_t validx  = VALIDX[k-1];
	    const char *name = attr_buf.ptr + nameidx;
	    const char *val  = attr_buf.ptr + validx;
	    fprintf(fp, "%s=\n", name);
	    rast_data(fp, val, strlen(val), '!');
	}
	fprintf(fp, "]\n");
    } else
	fprintf(fp, "[%s]\n", nodename[nt]);
	
    discard_atts();
    return;
}


void rast_Cdata(ESIS_UserData ud, const char *cdata, size_t len)
{
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    if (len == NTS) len = strlen(cdata);
    
    rast_data(fp, cdata, len, '|');
}

void rast_End(ESIS_UserData ud, cmark_node_type nt)
{
    struct RAST_Param_* rastp = ud;
    FILE *fp = rastp->outfp;
    
    if (nt == 0 && !(rastp->options & RAST_ALL))
	return;
    else {
	const char *GI = nodename[nt];
	fprintf(fp, "[/%s]\n", (GI == NULL) ? "#0" : nodename[nt]);
    }
    return;   
}

const struct ESIS_CB_ rast_CB = {
    rast_Attr,
    rast_Start,
    rast_Cdata,
    rast_End,
};


ESIS_Port* generate_rast(FILE *to, unsigned options)
{
    static ESIS_Port rast_port = { &rast_CB, &rast_param, };
    /*
     * Only one output file at a time.
     */
    assert(rast_param.outfp == NULL);
    rast_param.outfp   = to;
    rast_param.options = options;
    return &rast_port;
}

/*== Generator for OpenSP format ESIS output =========================*/

/*== Parser for OpenSP format ESIS input  ============================*/

/*== CommonMark Document Rendering into an ESIS Port ================*/

/*
 * Rendering a document node into the ESIS callbacks.
 */
 

static int S_render_node_esis(cmark_node *node,
                              cmark_event_type ev_type,
                              ESIS_Port *to)
{
  cmark_delim_type delim;
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  char buffer[100];
  
  const ESIS_CB  *esis_cb = to->cb;
  ESIS_UserData   esis_ud = to->ud;

  if (entering) {
    switch (node->type) {
    case NODE_TEXT:
    case NODE_HTML:
    case NODE_INLINE_HTML:
      if (node->type == NODE_HTML || node->type == NODE_INLINE_HTML) {
	DO_ATTR("type", "HTML", NTS);
	DO_ATTR("display", 
	            node->type == NODE_HTML ? "block" : "inline", NTS);
      }
      DO_START(node->type);
      DO_CDATA(node->as.literal.data, node->as.literal.len);
      DO_END(node->type);
      break;

    case NODE_LIST:
      switch (cmark_node_get_list_type(node)) {
      case ORDERED_LIST:
        DO_ATTR("type", "ordered", NTS); 
        sprintf(buffer, "%d", cmark_node_get_list_start(node));
        DO_ATTR("start", buffer, NTS);
        delim = cmark_node_get_list_delim(node);
        DO_ATTR("delim", (delim == PAREN_DELIM) ?
                              "paren" : "period", NTS);
        break;
      case CMARK_BULLET_LIST:
        DO_ATTR("type", "bullet", NTS);
        break;
      default:
        break;
      }
      DO_ATTR("tight", cmark_node_get_list_tight(node) ?
                                            "true" : "false", NTS);
      DO_START(node->type);
      break;

    case NODE_HEADER:
      sprintf(buffer, "%d", node->as.header.level);
      DO_ATTR("level", buffer, NTS);
      DO_START(node->type);
      break;

    case NODE_CODE:
    case NODE_CODE_BLOCK:
      {
	cmark_node_type nt = node->type;
	const char     *info, *data, *name;
	size_t          ilen,  dlen,  nmlen = 0U;
	const bool      is_inline = (nt == NODE_CODE);
	
	if (is_inline) {
	    data  = node->as.code.info.data;
	    dlen  = node->as.code.info.len;
	    info  = data;
	    ilen  = dlen;
	} else {
	    data  = node->as.code.literal.data;
	    dlen  = node->as.code.literal.len;
	    info  = node->as.code.info.data;
	    ilen  = node->as.code.info.len;
        }
        
	assert(info != NULL || ilen == 0U);
	assert(data != NULL || dlen == 0U);
	assert(dlen < 0xFFFFU);
	assert(ilen < 0xFFFFU);

	if (ilen > 0U) {
	    static const char  stop[] = { NOTA_DELIM_0, 
		CR, LF, SP, HT, NUL };
	    const size_t delimlen = 1U + !!(NOTA_DELIM_1);
	    size_t k;

	    for (k = 0U; k < ilen; ++k) {
		char ch = info[k];
		const char *p = strchr(stop, info[k]);
		if (p != NULL)
		    break;
	    }

	    if (k > 0U && k < ilen + 1) {
		const char *suf    = "";
		size_t      suflen = 0U;
		if (   info[k+0U] == NOTA_DELIM_0
#                     if NOTA_DELIM_1
		    && info[k+1U] == NOTA_DELIM_1
#		      endif
		   ) {
		    nmlen = k;
		    name  = info;
		    if (is_inline) {
			data  += nmlen + delimlen;
			dlen  -= nmlen + delimlen;
		    } else {
		        suf    = info + nmlen + delimlen;
		        suflen = ilen - nmlen - delimlen;
		    }
		} else if (is_inline) {
		    nmlen = 0U;
		    name  = "";
		} else {
		    nmlen = ilen;
		    name  = info;
		}
		
		if (nmlen > 0U && is_notation(name, nmlen)) {
		    const char *display = (nt == NODE_CODE) ?
			"inline" : "block";
		    nt = NODE_MARKUP;
		    DO_ATTR("notation", name,    nmlen);
		    DO_ATTR("display",  display, NTS);
		    info = suf;
		    ilen = suflen;
		} else if (is_inline) {
		    info = name;
		    ilen = nmlen;
		}
	    }
	}
	if (ilen > 0U)
	    DO_ATTR("info", info, ilen);
	DO_START(nt);
	DO_CDATA(data, dlen);
	DO_END(nt);
      }
      break;

    case NODE_LINK:
    case NODE_IMAGE:
      DO_ATTR("destination",
	                 node->as.link.url.data, node->as.link.url.len);
      DO_ATTR("title", node->as.link.title.data,
                                               node->as.link.title.len);
      DO_START(node->type);
      break;

    case NODE_HRULE:
    case NODE_SOFTBREAK:
    case NODE_LINEBREAK:
      DO_START(node->type);
      DO_END(node->type);
      break;

    case NODE_DOCUMENT:
    default:
      DO_START(node->type);
      break;
    } /* entering switch */
  } else if (node->first_child) { /* NOT entering */
    DO_END(node->type);
  }

  return 1;
}

char *cmark_render_esis(cmark_node *root, ESIS_Port *to)
{
  cmark_event_type ev_type;
  cmark_node *cur;
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node_esis(cur, ev_type, to);
  }
  cmark_iter_free(iter);
  return NULL;
}


/*====================================================================*/

/*
 * do_meta_lines -- Set meta-data attributes from pandoc-style header.
 */
 
size_t do_meta_lines(char *buffer, size_t nbuf, const ESIS_Port *to)
{
    size_t ibol, nused;
    unsigned dc_count = 0U;
    static const char *dc_name[] = {
	META_DC_TITLE,
	META_DC_CREATOR,
	META_DC_DATE,
	NULL,
    };
    char version[1024];
    const ESIS_CB *esis_cb = to->cb;
    ESIS_UserData  esis_ud = to->ud;
		
    ibol = 0U;
    nused = 0U;
    
    to->cb->attr(to->ud, META_DC_TITLE,   DEFAULT_DC_TITLE,   NTS);
    to->cb->attr(to->ud, META_DC_CREATOR, DEFAULT_DC_CREATOR, NTS);
    to->cb->attr(to->ud, META_DC_DATE,    DEFAULT_DC_DATE,    NTS);
    
    sprintf(version, "            %s;\n"
		     "            date: %s;\n"
		     "            id: %s\n"
		     "        ",
	    cmark_repourl,
	    __DATE__ ", " __TIME__,
	    cmark_gitident);
    DO_ATTR("CM.doc.v", version, NTS);
    DO_ATTR("CM.ver", CMARK_VERSION_STRING, NTS);
		
    for (ibol = 0U; buffer[ibol] == '%'; ) {
	size_t len;
        char *p;
        size_t ifield;
        
	/*
	 * Field starts after '%', ends before *p = LF.
	 */
        ifield = ibol + 1U;
        if (buffer[ifield] == ' ') ++ifield;
        if (ifield >= nbuf)
            break;
            
        p = memchr(buffer+ifield, '\n', nbuf - ifield);
        if (p == NULL)
            break; /* No EOL ==> fragment buffer too short. */
            
        /*
         * One after '\n'.
         */
        ibol = (p - buffer) + 1U;
        
        /*
         * We copy buffer[ifield .. ibol-2], ie the line content
         * from ifield to just before the '\n', and append a NUL 
         * terminator, of course.
         */
        len = ibol - ifield - 1U;
        if (len > 1U) {
	    if (dc_name[dc_count] != NULL)
		DO_ATTR(dc_name[dc_count++], buffer+ifield, len);
	    else {
		const char *colon, *val;
		char name[NAMELEN+1];
		size_t nname, nval;
		colon = strstr(buffer+ifield, ": ");
		if (colon != NULL && 
		              (nname = colon - (buffer+ifield)) < len) {
		    if (nname > NAMELEN) nname = NAMELEN;
		    strncpy(name, buffer + ifield, nname);
		    name[nname] = NUL;
    		
		    val     = colon + 2;
		    while (val[0] != EOL && ISSPACE(val[0]) &&
		                                          val[1] != EOL)
			++val;
		    nval = 0U;
		    while (val[nval] != EOL)
			++nval;
		    
		    DO_ATTR(name, val, nval);
		} else
		    fprintf(stderr, "Meta line \"%% %.*s\" ignored: "
		                           "No ': ' delimiter found.\n",
		                               (int)len, buffer+ifield);
	    }
        }
    }
    
    nused = ibol;
    return nused;
}

/*== Replacement Definitions Parsing =================================*/

/*
 * During parsing of the "Replacement Definition" file we keep these
 * globals around, mostly to simplify the job of keeping track of
 * the position in the input text file (for diagnostic messages).
 */
 
FILE *replfp         = NULL;
const char *filename = "<no file>";
unsigned lineno      = 0U;  /* Text input position: Line number. */
unsigned colno       = 0U;  /* Text input position: Column number. */

#define LA_SIZE 4

char        la_buf[LA_SIZE], ch0;
unsigned    la_num = 0U;

#define COUNT_EOL(CH) (((CH) == EOL) ? (++lineno, colno = 0U, (CH)) :	\
                                                       (++colno, (CH)) )

#define GETC(CH) ( la_num ? ( (CH) = la_buf[--la_num] ) :		\
                            ( (CH) = getc(replfp), COUNT_EOL(CH) ) )

#define UNGETC(CH) (la_buf[la_num++] = (CH))

#define PEEK()   ( la_buf[la_num++] = ch0 = getc(replfp),		\
                                                        COUNT_EOL(ch0) )

/*--------------------------------------------------------------------*/

/*
 * Error and syntax error diagnostics.
 */
 
void error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(EXIT_FAILURE);
}


void syntax_error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "%s(%u:%u): error: ", filename, lineno, colno);
    vfprintf(stderr, msg, va);
    va_end(va);
}

/*--------------------------------------------------------------------*/

/*
 * Parsing the replacement definition file format.
 */
 

/*
 * S = SPACE | SEPCHAR | RS | RE
 *
 * P_S(ch) accepts ( { S } )
 */
#define P_S(CH)								\
do {									\
    while (ISSPACE(CH))							\
	CH = GETC(CH);							\
} while (0)

/*
 * An _attribute substitution_ in the _replacement text_ gets encoded
 * like this:
 *
 *     attrib subst = "[" , [ prefix ] , nmstart , { nmchar } , "]" ;
 *
 *     encoded form:  SO ,  precode   ,  char   , {  char  } ,  NUL , SI 
 *
 * The (optional) prefix character "." or **Digit** is encoded like
 * this (using SP for "no prefix"):
 *
 *     prefix  precode
 *
 *      ./.      SP
 *      "."     0xFF
 *      "0"     0x01
 *      "1"     0x02
 *      ...      ...
 *
 *      "9"     0x0A
 *
 *   - Thus "precode" can be used as a "depth" argument directly, and
 *     because SI = 13, 
 *
 *   - we can still search for SI starting from the SO
 *     right at the front of this _attribute substitution_ encoding.
 *
 *   - And the _attribute name_ is a NTBS starting at offset 2 after the
 *     initial SO.
 */
 
int P_attr_subst(int ch, cmark_strbuf *pbuf, const char lit)
{
    int code = 0;
       
    assert(ch == '[');
    ch = GETC(ch);
    
    if (ISNMSTART(ch))
	code = SP;
    else if (ISDIGIT(ch) || ch == '.') {
	static const char in_ [] = ".0123456789";
	static const char out_[] = "\xFF\0x01\0x02\0x03\0x04\0x05"
	                               "\0x06\0x07\0x08\0x09\0x0A";
	ptrdiff_t idx = strchr(in_, ch) - in_;
	
	code = out_[idx];
	ch = GETC(ch);
    } else {
	syntax_error("Expected NMSTART or '.' or Digit, got '%c'\n",
	                                                            ch);
	return ch;
    }
    
    /*
     * Code the _**atto**_ delimiter and the "prefix char".
     */
    cmark_strbuf_putc(pbuf, SO);
    cmark_strbuf_putc(pbuf, code);
    if (code == SP) {
        cmark_strbuf_putc(pbuf, ch); /* The NMSTART char of name */
        ch = GETC(ch);
    }
    
    while (ch != EOF && ch != DSC[0]) {
	if (ISNMCHAR(ch)) {
	    cmark_strbuf_putc(pbuf, ch);
	} else if (ch == lit) { /* Hit the string delimiter! */
	    syntax_error("Unclosed attribute reference "
		"(missing '" DSC "').\n");
	    break;
	} else if (ISSPACE(ch)) {
	    syntax_error("SPACE in attribute name discarded.\n");
	} else if (ch == MSSCHAR) {
	    syntax_error("You can't use '%c' in attribute names.\n", ch);
	} else {
	    syntax_error("Expected NMCHAR, got '%c'.\n", ch);
	}
	ch = GETC(ch);
    }
    
    /*
     * Finish the encoded _attribute substitution_.
     */
	
    cmark_strbuf_putc(pbuf, NUL); /* Make the name a NTBS. */
    cmark_strbuf_putc(pbuf, SI);  /* Mark the end of the coded thing. */
    
    return ch;
}

#define P_repl_string(CH, P, L, LCP)  P_string((CH), (P), (L), 1, LCP)
#define P_attr_val_lit(CH, P, L)  P_string((CH), (P), (L), 0, NULL)

int P_string(int ch, cmark_strbuf *pbuf, const char lit, int is_repl,
                                                         char *lastchrp)
{
    char last = NUL;
    
    assert(ch == lit);
    assert(ch == '"' || ch == '\'');
    
    ch = GETC(ch);
    while (ch != lit && (last = ch) != NUL) {
	if (ch == MSSCHAR) {
	
	    switch (ch = GETC(ch)) {
	    case MSSCHAR: ch = MSSCHAR; break;
	    case  'n': ch = '\n';  break;
	    case  'r': ch = '\r';  break;
	    case  's': ch =  SP ;  break;
	    case  't': ch = '\t';  break;
	    case  '[': ch = '[' ;  break;
	    case '\"': ch = '\"' ; break;
	    case '\'': ch = '\'' ; break;
	    default:   cmark_strbuf_putc(pbuf, MSSCHAR);
	    }
	    if (ch != EOF)
		cmark_strbuf_putc(pbuf, ch);
	    last = ch, ch = GETC(ch);
	    
	} else if (ch == DSO[0] && is_repl) {
	
	    ch = P_attr_subst(ch, pbuf, lit);
	    
	} else if (ch == EOL) {
	    cmark_strbuf_putc(pbuf, ch);
	    last = ch, ch = GETC(ch);
	} else {
	    cmark_strbuf_putc(pbuf, ch);
	    last = ch, ch = GETC(ch);
	}
    }    
    if (!is_repl)
	cmark_strbuf_putc(pbuf, NUL);
    
    assert(ch == lit);
    if (lastchrp != NULL) *lastchrp = last;
    ch = GETC(ch);
    return ch;
}


int P_repl_text(int ch, char *repl_text[1], char *lastchrp)
{
    static cmark_strbuf repl = { NULL, 0, 0 };
    unsigned nstrings = 0U;
    
    /* cmark_strbuf_init(&repl, 16); */
    
    P_S(ch);
    
    if (ch == PLUS[0]) {
        cmark_strbuf_putc(&repl, VT);
        ch = GETC(ch);
    }
    
    while (ch != EOF) {

	P_S(ch);
	
	if (ch == LIT[0])
	    ch = P_repl_string(ch, &repl, LIT[0], lastchrp);
	else if (ch == LITA[0])
	    ch = P_repl_string(ch, &repl, LITA[0], lastchrp);
	else
	    break;
	++nstrings;
    }
    
    P_S(ch);
    
    if (ch == PLUS[0]) {
        cmark_strbuf_putc(&repl, VT);
        ch = GETC(ch);
    }
    
    if (nstrings > 0U) {
	char *res;
	cmark_strbuf_putc(&repl, NUL);
	res = cmark_strbuf_dup(&repl);
	repl_text[0] = res;
	return ch;
    } else {
	cmark_strbuf_clear(&repl);
	repl_text[0] = NULL;
	return ch;
    }
}

int P_repl_text_pair(int ch, char *repl_text[2], char *lastchrp)
{
    repl_text[0] = repl_text[1] = NULL;
    
    if (ch == '-') {
	ch = GETC(ch);
    } else if (ch == '/') {
	;
    } else {
	ch = P_repl_text(ch, repl_text+0, lastchrp);
    }
    
    P_S(ch);
    
    if (ch == '/') {
	ch = GETC(ch);
	P_S(ch);
	if (ch == '-') {
	    ch = GETC(ch);
	} else {
	    ch = P_repl_text(ch, repl_text+1, NULL);
	}
    }
    
    return ch;
}


int P_name(int ch, cmark_node_type *pnt, char name[NAMELEN+1], int fold)
{
    char *p = name;
    cmark_node_type nt;
    
    assert(ISNMSTART(ch));
    
    do {
	*p++ = fold ? toupper(ch) : ch;
	ch = GETC(ch);
    } while (p < name + NAMELEN + 1 && ISNMCHAR(ch));
    
    *p = NUL;
    if (p == name + NAMELEN + 1) {
	syntax_error("\"%s\": Name truncated after NAMELEN = %u "
	                                "characters.\n", name, NAMELEN);
    }
    while (ISNMCHAR(ch))
	ch = GETC(ch);
    
    if (pnt == NULL)
	return ch;
    else
	*pnt = 0;
    /*
     * Look up the "GI" for a CommonMark node type.
     */
     
    for (nt = 1; nodename[nt] != NULL; ++nt)
	if (nodename[nt] != NULL && strcmp(nodename[nt], name) == 0) {
	    *pnt = nt;
	    break;
	}
	    
    if (*pnt == 0)
	syntax_error("\"%s\": Not a CommonMark node type.", name);

    return ch;
}


int P_rni_name(int ch, enum rn_ *prn, char name[NAMELEN+1])
{
    char *p = name;
    enum rn_ rn;
 
    assert(ch == RNI[0]);
    
    ch = GETC(ch);
    ch = P_name(ch, NULL, name, 1);
    
    if (prn == NULL)
	return ch;
    else
	*prn = 0;
	
    /*
     * Look up the "reserved name".
     */
    for (rn = 1; rn_name[rn] != NULL; ++rn)
	if (strcmp(rn_name[rn], name) == 0) {
	    *prn = rn;
	    break;
	}
    
    if (*prn == 0)
	syntax_error("\"%s\": Unknown reserved name.\n", name);

    return ch;
}

int P_tag(int ch, struct taginfo_ taginfo[1])
{
    char name[NAMELEN+1];
    cmark_node_type nt;
    const int fold = 1;
    unsigned nattr = 0U;
    int is_etag = 0;
    
    assert(ch == STAGO[0]);
    
    ch = GETC(ch);
    if (ch == '/') {
	is_etag = 1;
	ch = GETC(ch);
    } 
	
    ch = P_name(ch, &nt, name, fold);
    taginfo->nt = nt;
    
    P_S(ch);
    while (ch != TAGC[0]) {
	bufsize_t name_idx = 0, val_idx = 0;
	
	ch = P_name(ch, NULL, name, 0);
	P_S(ch);
	if (ch == VI[0]) {
	    ch = GETC(ch);
	    P_S(ch);
	    if (ch == LIT[0] || ch == LITA[0]) {
		val_idx = text_buf.size;
		ch = P_attr_val_lit(ch, &text_buf, ch);
	    }   
	}
	P_S(ch);
	
	name_idx = text_buf.size;
	cmark_strbuf_puts(&text_buf, name);
	cmark_strbuf_putc(&text_buf, NUL);
	taginfo->atts[2*nattr+0] = name_idx;
	taginfo->atts[2*nattr+1] = val_idx;
	++nattr;
    }
    taginfo->atts[2*nattr+0] = NULLIDX;
    
    if (ch != TAGC[0])
	syntax_error("Expected \'%c\'.\n", TAGC[0]);
    else
	ch = GETC(ch);
    return ch;
}

int P_tag_rule(int ch)
{
    struct taginfo_ taginfo[1];
    char *repl_texts[2];
    char lastch;

    assert(ch == STAGO[0]);

    ch = P_tag(ch, taginfo);
    P_S(ch);
    ch = P_repl_text_pair(ch, repl_texts, &lastch);

    set_repl(taginfo, repl_texts, lastch == '[');

    return ch;
}

int P_rn_rule(int ch)
{
    char name[NAMELEN+1];
    char *repl_text[1];
    enum rn_ rn;
    
    assert(ch == RNI[0]);
    
    ch = P_rni_name(ch, &rn, name);
    
    ch = P_repl_text(ch, repl_text, NULL);
    rn_repl[rn] = repl_text[0];
    return ch;
}

int P_comment(int ch, const char lit)
{
    assert(ch == '%' || ch == '-');
    assert(lit == EOL || lit == '-'); /* TODO "--" vs EOL */
    assert(ch == '%' || ch == lit);
       
    if (ch == '-') {
	ch = GETC(ch);
	assert(ch == '-');
    }
    
    while ((ch = GETC(ch)) != EOF)
	if (ch == lit) {
	    if (lit == '-' && PEEK() == '-') {
		ch = GETC(ch);
		break;
	    } else if (lit == EOL)
		break;
	}

    assert(ch == lit || ch == EOF);
    ch = GETC(ch);
	    
    return ch;
}

int P_repl_defs(int ch)
{
    while (ch != EOF) {
	P_S(ch);
	
	if (ch == EOF)
	    break;

	if (ch == STAGO[0])
	    ch = P_tag_rule(ch);
	else if (ch == RNI[0])
	    ch = P_rn_rule(ch);
	else if (ch == '%')
	    ch = P_comment(ch, '\n');
	else if (ch == '-' && PEEK() == '-')
	    ch = P_comment(ch, '-');
	else {
	    syntax_error("Unexpected character \'%c\'.\n", ch);
	    ch = GETC(ch);
	}
    }
    return ch;
}

/*--------------------------------------------------------------------*/

/*
 * Loading (ie parsing and interpreting) a Replacement Definition file.
 */
 
void load_repl_defs(FILE *fp)
{
    int ch;
    
    if (fp == NULL) return;
    
    replfp = fp;
    
    COUNT_EOL(EOL); /* Move to start of first line */
    
    cmark_strbuf_init(&text_buf, 2048U);
    cmark_strbuf_putc(&text_buf, NUL); /* NULLIDX is unsused.*/
    
    cmark_strbuf_init(&attr_buf, ATTSPLEN);
    cmark_strbuf_putc(&attr_buf, NUL); /* NULLIDX is unsused.*/
    
    cmark_strbuf_init(&nameidx_buf, ATTCNT * sizeof(nameidx_t));
    cmark_strbuf_init(&validx_buf,  ATTCNT * sizeof(validx_t));

    ch = GETC(ch);
    ch = P_repl_defs(ch);
    assert(ch == EOF);
    
    fclose(replfp);
    replfp = NULL;
}


/*--------------------------------------------------------------------*/

/*
 * Find and open a Replacement Definition file.
 *
 * A NULL argument refers to the "default" repl def file.
 */
 
static const char *repl_dir = NULL;
static const char *repl_default = NULL;
    
#ifdef _WIN32
#define DIRSEP "\\"
#else
#define DIRSEP "/"
#endif
static const char dirsep[] = DIRSEP;

bool is_relpath(const char *pathname)
{
    if (pathname[0] == dirsep[0])
	return 0;
#ifdef _WIN32
    /*
     * A DOS-style path starting with a "drive letter" like "C:..."
     * is taken to be "absolute" - although it technically can be 
     * relative (to the cwd for this drive): we would have to
     * _insert_ the path prefix after the ":", not simply prefix it.
     */
    if ( (('A' <= pathname[0] && pathname[0] <= 'Z') || 
          ('a' <= pathname[0] && pathname[0] <= 'z')) && 
         pathname[1] == ':' )
	return 0;
#endif
    return 1;
}

/*
 * Find and open a replacement definition file.
 *
 * If NULL is given as the filename, the "default repl def" is used
 * (specified by environment).
 *
 * The "verbose" argument can name a text stream (ie stdout or stderr)
 * into which report is written on the use of environment variables and
 * which replacement file pathnames were tried etc. (This is used
 * in `usage()`, invoked eg by the `--help` option);
 */
 
FILE *open_repl_file(const char *repl_filename, FILE *verbose)
{
    FILE *fp;
    bool is_rel;
    
    if (repl_dir == NULL)     repl_dir = getenv(REPL_DIR_VAR);
    if (repl_default == NULL) repl_default = getenv(REPL_DEFAULT_VAR);
    
    if (verbose) {
	putc(EOL, verbose);
	fprintf(verbose, "%s =\n\t\"%s\"\n", REPL_DIR_VAR,
		(repl_dir) ? repl_dir : "<not set>");
	fprintf(verbose, "%s =\n\t\"%s\"\n", REPL_DEFAULT_VAR,
		(repl_default) ? repl_default : "<not set>");
	putc(EOL, verbose);
    }
    /*
     * Passing in NULL means: use the default replacement definition.
     */
    if (repl_filename == NULL && (repl_filename = repl_default) == NULL)
	if (verbose) {
	    fprintf(verbose, "No default replacement file!\n");
	    return NULL;
	} else
	    error("No replacement definition file specified, "
	          "nor a default - giving up!\n");
	
    /*
     * First try the given filename literally.
     */
    filename = repl_filename;
    assert(filename != NULL);
    is_rel = is_relpath(filename);
    
    if (verbose) fprintf(verbose, "Trying\t\"%s%s\" ... ",
                         is_rel ? "." DIRSEP : "" , filename);
    fp = fopen(filename, "r");
    if (verbose) fprintf(verbose, "%s.\n", (fp) ? "ok" : "failed");

    
    /*
     * Otherwise, try a *relative* pathname with the REPL_DIR_VAR
     * environment variable.
     */
    if (fp == NULL && repl_dir != NULL) {
	size_t fnlen, rdlen;
	int trailsep;
	char *pathname;

	if (is_relpath(filename)) {
	    fnlen = strlen(filename);
	    rdlen = strlen(repl_dir);
	    trailsep = rdlen > 0U && (repl_dir[rdlen-1U] == dirsep[0]);
	    pathname = malloc(rdlen + !trailsep + fnlen + 16);
	    sprintf(pathname, "%s%s%s",
		repl_dir, dirsep+trailsep, filename);
		
	    if (verbose) fprintf(verbose, "Trying\t\"%s\" ... ",
	                                                      pathname);
	    fp = fopen(pathname, "r");
	    if (verbose) fprintf(verbose, "%s.\n",
	                                (fp == NULL) ? "failed" : "ok");
	                                
	    if (fp != NULL) filename = pathname;
	    else free(pathname);
	}
    }
    
    /*
     * If we **still** have no replacement definition file, it is
     * time to give up.
     */
    if (fp == NULL) {
	if (verbose) fprintf(verbose, "Can't open \"%s\": %s.\n",
	                                     filename, strerror(errno));
	else
	    error("Can't open replacement file \"%s\": %s.", filename,
	                                               strerror(errno));
    }
    return fp;
}

/*====================================================================*/

/*
 * gen_document -- Driver for the replacement backend
 *
 *  1. Start the outermost "universal" pseudo-element.
 *  2. Output the replacement text for #PROLOG, if any.
 *  3. Render the document into the given ESIS API callbacks.
 *  4. Output the replacement text for #EPILOG, if any.
 *  5. End the outermost pseudo-element.
 *  6. [Not needed]: Clean up the attribute stack.
 */
 
static void gen_document(cmark_node *document,
                         cmark_option_t options,
                         ESIS_Port *to)
{
    const ESIS_CB *esis_cb = to->cb;
    ESIS_UserData  esis_ud = to->ud;
    int bol[2];
    const cmark_node_type none = CMARK_NODE_NONE;
    
    DO_START(CMARK_NODE_NONE);
    bol[0] = bol[1] = 0;
    
    if (rn_repl[RN_PROLOG] != NULL) {
	put_repl(rn_repl[RN_PROLOG]);
    }

    cmark_render_esis(document, to);

    if (rn_repl[RN_EPILOG] != NULL) {
	put_repl(rn_repl[RN_EPILOG]);
    }
    DO_END(CMARK_NODE_NONE);
}


/*====================================================================*/

/*
 * Preprocessor.
 */

esc_state *esp;

const char *prep_cb(const char *arg)
{
    static octetbuf chars = { 0 };
    static octetbuf strings = { 0 };
    static char id[128];
    size_t n, k;
    bool isref = strncmp(arg, "ref(", 4) == 0;
    bool isdef = strncmp(arg, "def(", 4) == 0;
    
    if (!isref && !isdef) return NULL;

    if (strlen(arg) >= sizeof id - 1U) return arg;
    
    strcpy(id, arg += 4);
    for (k = 0; id[k] != NUL; ++k)
	if (id[k] == ')')
	    id[k] = NUL;
    
    n = octetbuf_size(&strings)/sizeof(char *);
    for (k = 0; k < n; ++k) {
	const char **pp = octetbuf_elem_at(&strings, k, sizeof *pp);
	const char *p = *pp;
	if (strcmp(p, id) == 0)
	    break;
    }
    if (k == n) {
	octetidx_t i = octetbuf_push_s(&chars, id);
	const char *s = octetbuf_at(&chars, i);
	octetbuf_push_c(&chars, NUL);
	octetbuf_push_back(&strings, &s, sizeof s);
    }
    if (isref)
	snprintf(id, sizeof id, "#ref%002u", (unsigned)k);
    else
	snprintf(id, sizeof id,
	    "<a name=\"ref%03u\" id=\"ref%03u\"></a>",
	                                      (unsigned)k, (unsigned)k);
    return id;
}


int prep_init(const char *dgrfile)
{
    FILE *fp = NULL;
    
    if (dgrfile == NULL)
	dgrfile = getenv("CM2DOC_DIGRAPHS");

    if (dgrfile != NULL && (fp = fopen(dgrfile, "r")) == NULL)
	error("Can't open \"%s\": %s\n.", dgrfile, strerror(errno));
	
    esp = esc_create(fp);
    fclose(fp);
    
    esc_callback(esp, prep_cb);
    esc_set_escape(esp, '\\');
    esc_set_subst(esp,  '$');
    return 0;
}

size_t prep(char *p, size_t n, FILE *fp)
{
    return esc_fsubst(esp, p, n, fp);
}

/*====================================================================*/

int parse_cmark(FILE *from, ESIS_Port *to, cmark_option_t options,
                                                     const char *meta[])
{
    static cmark_parser *parser = NULL;
    
    static bool in_header = true;
    static char buffer[8*BUFSIZ];
    
    size_t bytes;
    
    if (parser == NULL)
	parser = cmark_parser_new(options);
    
    if (from != NULL)
	while ((bytes = prep(buffer, sizeof buffer,from)) > 0) {
	    /*
	    * Read and parse the input file block by block.
	    */
	    size_t hbytes = 0U;

	    if (in_header) {
		int imeta;
		const ESIS_CB *cb = to->cb;
		ESIS_UserData  ud = to->ud;

		hbytes = do_meta_lines(buffer, sizeof buffer, to);

		/*
		* Override meta-data from meta-lines with meta-data
		* given in command-line option arguments, eg `--title`.
		*/
		if (meta != NULL)
		    for (imeta = 0; meta[2*imeta] != NULL; ++imeta)
			cb->attr(ud, meta[2*imeta+0], meta[2*imeta+1],
			                                           NTS);

		in_header = false;
	    }

	    if (hbytes < bytes)
		cmark_parser_feed(parser, buffer + hbytes, 
		                                        bytes - hbytes);

	    if (bytes < sizeof(buffer))
		break;
	}
    else {
	/*
	 * Finished parsing, generate document content into
	 * ESIS port.
	 */
        cmark_node   *document;
        
	document = cmark_parser_finish(parser);
	cmark_parser_free(parser); parser = NULL;

	gen_document(document, options, to);

	cmark_node_free(document);
    }
    
    return 0;
}

/*== Main function ===================================================*/

void usage()
{
    printf("Usage:   cm2doc [FILE*]\n\n");
    printf("Options:\n");
    printf("  -t --title TITLE Set the document title\n");
    printf("  -c --css CSS     Set the document style sheet to CSS\n");
    printf("  -r --repl file   Use replacement definition file\n");
    printf("  --sourcepos      Include source position attribute\n");
    printf("  --hardbreaks     Treat newlines as hard line breaks\n");
    printf("  --safe           Suppress raw HTML and dangerous URLs\n");
    printf("  --smart          Use smart punctuation\n");
    printf("  --normalize      Consolidate adjacent text nodes\n");
    printf("  --rast           Output RAST format "
                                              "(ISO/IEC 13673:2000)\n");
    printf("  --help, -h       Print usage information\n");
    printf("  --version        Print version\n");
    
    printf("\nReplacement files:\n");
    open_repl_file(NULL, stdout);
}


int main(int argc, char *argv[])
{
    const char *username         = "N.N.";
    const char *title_arg        = NULL;
    const char *css_arg          = NULL;
    const char *dgr_arg          = NULL;

    cmark_option_t cmark_options = CMARK_OPT_DEFAULT;
    unsigned       rast_options  = 0U;
    bool doing_rast              = false;
    unsigned repl_file_count     = 0U;
    
    ESIS_Port *port   = NULL;
    FILE      *infp   = stdin;
    FILE      *outfp  = stdout;
    FILE      *replfp = NULL;
    
    const char *meta[42], **pmeta = meta;
    time_t now;
    int argi;
    
    meta[0] = NULL;
    
    if ( (username = getenv("LOGNAME"))   != NULL ||
         (username = getenv("USERNAME"))  != NULL )
	strncpy(default_creator, username, sizeof default_creator -1U);
	
    time(&now);
    strftime(default_date, sizeof default_date, "%Y-%m-%d",
                                                          gmtime(&now));
	    
    for (argi = 1; argi < argc && argv[argi][0] == '-'; ++argi) {
	if (strcmp(argv[argi], "--version") == 0) {
	    printf("cmark %s", CMARK_VERSION_STRING
		" ( %s %s )\n",
		cmark_repourl, cmark_gitident);
	    printf(" - CommonMark converter\n"
	                            "(C) 2014, 2015 John MacFarlane\n");
	    exit(EXIT_SUCCESS);
	} else if ((strcmp(argv[argi], "--repl") == 0) ||
	    (strcmp(argv[argi], "-r") == 0)) {
	    const char *filename = argv[++argi];
	    load_repl_defs(open_repl_file(filename, NULL));
	    ++repl_file_count;
	} else if (strcmp(argv[argi], "--rast") == 0) {
	    doing_rast = true;
	} else if (strcmp(argv[argi], "--rasta") == 0) {
	    doing_rast = true;
	    rast_options |= RAST_ALL;
	} else if ((strcmp(argv[argi], "--title") == 0) ||
	    (strcmp(argv[argi], "-t") == 0)) {
		title_arg = argv[++argi];
	} else if ((strcmp(argv[argi], "--css") == 0) ||
	    (strcmp(argv[argi], "-c") == 0)) {
		css_arg = argv[++argi];
	} else if ((strcmp(argv[argi], "--digr") == 0) ||
	    (strcmp(argv[argi], "-d") == 0)) {
		dgr_arg = argv[++argi];
	} else if (strcmp(argv[argi], "--sourcepos") == 0) {
	    cmark_options |= CMARK_OPT_SOURCEPOS;
	} else if (strcmp(argv[argi], "--hardbreaks") == 0) {
	    cmark_options |= CMARK_OPT_HARDBREAKS;
	} else if (strcmp(argv[argi], "--smart") == 0) {
	    cmark_options |= CMARK_OPT_SMART;
	} else if (strcmp(argv[argi], "--safe") == 0) {
	    cmark_options |= CMARK_OPT_SAFE;
	} else if (strcmp(argv[argi], "--normalize") == 0) {
	    cmark_options |= CMARK_OPT_NORMALIZE;
	} else if (strcmp(argv[argi], "--validate-utf8") == 0) {
	    cmark_options |= CMARK_OPT_VALIDATE_UTF8;
	} else if ((strcmp(argv[argi], "--help") == 0) ||
	    (strcmp(argv[argi], "-h") == 0)) {
		usage();
		exit(EXIT_SUCCESS);
	} else if (argv[argi][1] == NUL) {
	    ++argi;
	    break;
	} else {
	    usage();
	    error("\"%s\": Invalid option.\n", argv[argi]);
	}
    }
    
    prep_init(dgr_arg);
    
    {
	static const char *const notations[] = {
	    "HTML",	"Z",    "EBNF",	    "VDM",
	    "C90",	"C11",	"ASN.1",    "Ada95",
	    NULL
	};
	int i;
	
	for (i = 0; notations[i] != NULL; ++i)
	    register_notation(notations[i], NTS);
    }
    
    /*
     * If no replacement file was mentioned (and processed),
     * try using the default replacement file given in the
     * environment.
     */
    if (doing_rast)
	if (repl_file_count > 0U)
	    error("Can't use RAST with replacement files.\n");
	else
	    /* Do RAST. */
	    port = generate_rast(outfp, rast_options);
    else {
	if (repl_file_count == 0U)
	    /* Succeed or die. */
	    load_repl_defs(open_repl_file(NULL, NULL));
	    
	if (title_arg != NULL) {
	    *pmeta++ = META_DC_TITLE;
	    *pmeta++ = title_arg;
	}
	*pmeta++ = META_CSS;
	*pmeta++ = (css_arg) ? css_arg : DEFAULT_CSS;
	*pmeta++ = "lang";
	*pmeta++ = "en"; /* TODO command-line option "--lang" */
	*pmeta = NULL;
	
	port = generate_repl(outfp, 0U);
    }

    /*
     * Loop through the input files.
     */
    switch (argc - argi) do {
    default:
	if ((outfp = freopen(argv[argi], "r", stdin)) == NULL)
	    error("Can't open \"%s\": %s\n", argv[argi],
	                                               strerror(errno));
    case 0:
	parse_cmark(outfp, port, cmark_options, meta);
    } while (++argi < argc);
    
    parse_cmark(NULL, port, 0U, NULL);


    return EXIT_SUCCESS;
}

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
