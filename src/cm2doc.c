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

#include <assert.h>
#include <ctype.h> /* toupper() */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> /* strftime(), gmtime(), time() */

/* CommonMark cmark header */
#include "config.h"
#include "cmark.h"
#include "cmark_ctype.h"
#include "node.h"
#include "buffer.h"

/*== ESIS API ========================================================*/

/*
 * Callback function types for document traversal.
 */

typedef void *ESIS_UserData;

typedef void (ESIS_Attr)(ESIS_UserData,
                             const char *name, const char *val, size_t);
typedef void (ESIS_Start)(ESIS_UserData,               cmark_node_type);
typedef void (ESIS_Empty)(ESIS_UserData,               cmark_node_type);
typedef void (ESIS_Cdata)(ESIS_UserData,          const char *, size_t);
typedef void (ESIS_End)(ESIS_UserData,                 cmark_node_type);

typedef struct ESIS_API_ {
    ESIS_Attr	 *attr;
    ESIS_Start	 *start;
    ESIS_Empty	 *empty;
    ESIS_Cdata	 *cdata;
    ESIS_End	 *end;
    ESIS_UserData ud;
} ESIS_API;

#define NUL  0


/*== Unicode UTF-8 handling ==========================================*/

void error(const char *msg, ...);

typedef long ucs_t;

#define U(X)                    ((ucs_t)(0x ## X ##L))
#define UCS(CH)                 ((ucs_t)((CH)&0xFFU))

#define IS_BYTE(c)              (!((c) & ~0xFF))
#define BYTE(c)                 ((c) & 0xFF)
#define HIGH(w)                 (BYTE((w) >> 8))
#define LOW(w)                  (BYTE(w))
#define POINT(hi,lo)            ((BYTE(hi) << 8) | BYTE(lo))

/*-- UCS code points -------------------------------------------------*/

#define UEOF                    -1L
#define UCS_NUL                  0UL

#define UCS_SURR_FIRST          U(D800)
#define UCS_HI_FIRST            U(D800)
#define UCS_HI_LAST             U(DBFF)
#define UCS_LO_FIRST            U(DC00)
#define UCS_LO_LAST             U(DFFF)
#define UCS_SURR_LAST           U(DFFF)

#define UCS_NONCH_FIRST         U(FDD0) /* Not assigned, but allowed */
#define UCS_NONCH_LAST          U(FDEF) /* in XML. */

#define UCS_BOM                 U(FEFF)
#define UCS_NONCH_HIGH          U(FFFF)

#define UCS_REPLACEMENT         U(FFFD)

#define UCS_MAX                 U(10FFFF)

/*
 * UCS_ISXML() - Check XML code point
 *
 * The XML "Character Range" is defined in W3C XML 1.0 5th Edition
 * as:
 *
 *     Char ::= #x9 | #xA | #xD
 *            | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]
 *
 * The SGML declaration for XML in ISO 8879:1986/Cor.2:1999 (in the
 * the informative "Annex L: Added Requirements for XML" does also
 * exclude 
 * 
 *  1. the U+007F DELETE character (which has the Unicode General
 *     Category Cc [Other, Control], but is *not* a control character
 *     by ISO 646:1991), and
 *
 *  2. all the C1 control characters (range U+0080 .. U+009F).
 *
 * We go with W3C XML here.
 */
 
#define UCS_ISXML(CP) (							\
( (CP) >= 32 || (CP) == 9 || (CP) == 10 || (CP) == 13 ) &&		\
  ( (CP) < U(D800) || U(DFFF) < (CP) ) &&				\
  (CP) != U(FFFE) && (CP) != U(FFFF) &&					\
  (CP) <= UCS_MAX )

/*-- UTF-8 lengths ---------------------------------------------------*/

/*
 * The first byte determines how many "contiunuation" bytes follow.
 */

#define UTF_START2_MASK 0x1F   /* Mask payload bits in start 2 byte   */
#define UTF_START3_MASK 0x0F   /* Mask payload bits in start 3 byte   */
#define UTF_START4_MASK 0x07   /* Mask payload bits in start 3 byte   */
#define UTF_CONTIN_MASK 0x3F   /* Mask payload bits in contin byte    */
#define UTF_CONTIN_BITS 6      /* There are 6 payload bits in that    */

/*
 * We only want to process well-formed sequences.
 *
 * See Table 3-7 "Well formed UTF-8 byte sequences" in
 * <http://www.unicode.org/versions/Unicode6.0.0/ch03.pdf>
 * for details.
 */

#define UTF_IS_START2(c) ('\xC2' <= (c) && (c) <= '\xDF')   /* C2..DF */
#define UTF_IS_START3(c) (((c) & '\xF0') == '\xE0')         /* E0..EF */
#define UTF_IS_START4(c) (((c) & '\xF8') == '\xF0')         /* F0..F7 */

#define UTF_IS_CONTIN(c) (((c) & '\xC0') == '\x80')         /* 80..BF */

/*
 * utf8len -
 *	Determine length and check validity of a supposed 
 *	UTF-8 sequence.
 *
 * Return:
 * 
 *   m > 0 : A valid m-byte long seqence is in buf[0..m-1].
 *
 *   m < 0 : Either the (-m)-byte long sequence is invalid,
 *           or the size allowed by parameter len is too low.
 */
 
int utf8tab[32] = {
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,   2, 2, 2, 2, 3, 3, 4, 5,
};

int utf8len(const char buf[4], size_t len)
{
    unsigned byte = buf[0] & 0xFFU;
    unsigned idx = (byte >> 3) & 0x1FU;
    int u8len = utf8tab[idx];
    
    switch (u8len) {
    case 0: return 0; /* UTF-8 trailing octet */
    case 1: return ( u8len <= (int)len ) ? u8len : -u8len;
    case 2: return ( u8len <= (int)len && UTF_IS_CONTIN(buf[1]) ) ?
                                                         u8len : -u8len;
    case 3: return ( u8len <= (int)len && UTF_IS_CONTIN(buf[1])
                                       && UTF_IS_CONTIN(buf[2]) ) ?
                                                         u8len : -u8len;
    case 4: return ( u8len <= (int)len && UTF_IS_CONTIN(buf[1])
                                       && UTF_IS_CONTIN(buf[2])
                                       && UTF_IS_CONTIN(buf[3]) ) ?
                                                         u8len : -u8len;
    default:
	    return -u8len;
    }
}

/*-- UTF-8 decoding --------------------------------------------------*/

ucs_t utf8_decode2(const char buf[2])
{
    ucs_t val;

    val  = (buf[0] & UTF_START2_MASK);               /* Upper 5 bits  */

    if (!UTF_IS_CONTIN(buf[1]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[1] & UTF_CONTIN_MASK);               /* Lower 6 bits  */

    return val;
}

ucs_t utf8_decode3(const char buf[3])
{
    ucs_t val;

    val  = (buf[0] & UTF_START3_MASK);               /* Upper 4 bits  */

    if (!UTF_IS_CONTIN(buf[1]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[1] & UTF_CONTIN_MASK) ;              /* Middle 6 bits */

    if (!UTF_IS_CONTIN(buf[2]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[2] & UTF_CONTIN_MASK) ;              /* Lower 6 bits  */

    return val;
}

ucs_t utf8_decode4(const char buf[4])
{
    ucs_t val;

    val  = (buf[0] & UTF_START4_MASK);               /* Upper 3 bits  */

    if (!UTF_IS_CONTIN(buf[1]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[1] & UTF_CONTIN_MASK) ;              /* Upper 6 bits  */

    if (!UTF_IS_CONTIN(buf[2]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[2] & UTF_CONTIN_MASK) ;              /* Middle 6 bits */

    if (!UTF_IS_CONTIN(buf[3]))
	return UCS_REPLACEMENT;
    val <<= UTF_CONTIN_BITS;
    val |= (buf[3] & UTF_CONTIN_MASK) ;              /* Lower 6 bits  */

    return val;
}

/*
 * utf8decode -- Convert UTF-8 to UCS
 *
 * Return UCS_REPLACEMENT if 
 *
 *   - buf[] is not a valid UTF-8 sequence, or
 *
 *   - len is too short.
 */
ucs_t utf8decode(const char buf[4], size_t len)
{
    int n = utf8len(buf, len);
    
    if (n > 0) switch (n) {
	case 1: return UCS(buf[0]);
	case 2: return utf8_decode2(buf);
	case 3: return utf8_decode3(buf);
	case 4: return utf8_decode4(buf);
    }
    return UCS_REPLACEMENT;
}


/*-- UTF-8 encoding --------------------------------------------------*/

/*
 * utf8encode - Convert UCS to UTF-8, storing up to 4 octets 
 *              and a terminating NUL character in buf[].
 *
 * Return: Number of UTF-8 octets generated (not including the NUL).
 */
size_t utf8encode(char buf[5], ucs_t codepoint)
{
    buf[0] = NUL;
    
    if (UCS_MAX < codepoint) {
        error("Invalid UCS output code point discarded", codepoint);
        return 0;
    } else if (UCS_SURR_FIRST <= codepoint && 
                                           codepoint <= UCS_SURR_LAST) {
        error("Invalid UCS output surrogate discarded", codepoint);
	return 0;
    }
           
    if (codepoint <= U(007F)) {
	buf[0] = (char)codepoint;
	buf[1] = NUL;
	return 1;
    } else if (codepoint <= U(07FF)) {
        unsigned const bits = (unsigned)codepoint;
        unsigned first, second;
        
        first  = ((bits >>  6) & 0x1FU) | 0xC0U,
	second = ((bits      ) & 0x3FU) | 0x80U;

	buf[0] = (char)first;
	buf[1] = (char)second;
	buf[2] = NUL;
	return 2;
    } else if (codepoint <= U(FFFF)) {
        unsigned const bits = (unsigned)codepoint;
        unsigned first, second, third;
        first  = ((bits >> 12) & 0x0FU) | 0xE0U,
	second = ((bits >>  6) & 0x3FU) | 0x80U,
	third  = ((bits      ) & 0x3FU) | 0x80U;

	buf[0] = (char)first;
	buf[1] = (char)second;
	buf[2] = (char)third;
	buf[3] = NUL;
	return 3;
    } else /* codepoint <= U(10FFFF) */ {
        unsigned first, second, third, fourth;

        first  = (unsigned)((codepoint >> 18) & 0x07L) | 0xF0U;
        second = (unsigned)((codepoint >> 12) & 0x3FL) | 0x80U;
        third  = (unsigned)((codepoint >>  6) & 0x3FL) | 0x80U;
        fourth = (unsigned)((codepoint      ) & 0x3FL) | 0x80U;
        
	buf[0] = (char)first;
	buf[1] = (char)second;
	buf[2] = (char)third;
	buf[3] = (char)fourth;
	buf[4] = NUL;
	return 4;
    }
    
    return 0;
}


/*--------------------------------------------------------------------*/

/*
 * Reading and writing UCS characters from/to text streams.
 *
 *             **UNTESTED, NOT YET IN USE**
 */
 
ucs_t getuc(FILE *);
long putuc(ucs_t, FILE *);
long ungetuc(ucs_t, FILE *);
#define UNGETUC_MAX 4

char       uc_buf[5];
ucs_t      uc_ungot[UNGETUC_MAX];
unsigned   uc_nungot = 0U;

ucs_t getuc(FILE *fp)
{
    int ch;
    int len, i;
    ucs_t ucs;
    
    if (uc_nungot > 0U)
	return uc_ungot[--uc_nungot];

    if ((ch = getc(fp)) == EOF)
	return UEOF;

    i = 1;
    uc_buf[0] = ch;
    uc_buf[i] = NUL;
    len = utf8len(uc_buf, 5U);
    if (len < -1)
	while (++len < 0) {
	    if ((ch = getc(fp)) == EOF) return UEOF;
	    uc_buf[i++] = ch;
	}
    else if (len > 1)
	while (--len > 0) {
	    if ((ch = getc(fp)) == EOF) return UEOF;
	    uc_buf[i++] = ch;
	}
    ucs = utf8decode(uc_buf, 5U);
    return ucs;
}

long putuc(ucs_t ucs, FILE *fp)
{
    char buf[5];
    int len, i;
    int st;
    
    len = utf8encode(buf, ucs);
    if (len > 0) {
	for (i = 0; i < len; ++i)
	    if ((st = putc(buf[i], fp)) == EOF)
		return UEOF;
	return ucs;
    } else {
	return UEOF;
    }
}

long ungetuc(ucs_t ucs, FILE *fp)
{
    if (uc_nungot + 1 < UNGETUC_MAX)
	return uc_ungot[uc_nungot++] = ucs;
    else
	return UEOF;
}

/*====================================================================*/

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

/*--------------------------------------------------------------------*/

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

/*--------------------------------------------------------------------*/

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

/*====================================================================*/

/* How many node types there are, and what the name length limit is. */
#define NODE_NUM       (CMARK_NODE_LAST_INLINE+1)
#define NODENAME_LEN   NAMELEN

static const char* const nodename[NODE_NUM] = {
     NULL,	/* The "none" type (enum const 0) is invalid! */
   /*12345678*/
    "CM.DOC",
    "CM.QUO-B",
    "CM.LIST",
    "CM.LITM",
    "CM.COD-B",
    "CM.FRG-B", /* Block HTML/SGML/XHTML/XML fragment: literal output. */
    "CM.PAR",
    "CM.HDR",
    "CM.HRULE",
    "CM.TXT",
    "CM.SO-BR",
    "CM.LN-BR",
    "CM.COD",
    "CM.FRG",   /* Inline HTML/SGML/XHTML/XML fragment: literal output. */
    "CM.EMPH",
    "CM.STRN",
    "CM.LNK",
    "CM.IMG",
};


/*--------------------------------------------------------------------*/

/*
 * "Reserved Names" to bind special "replacement texts" to:
 * The output document's prolog (and, if needed, epilog).
 */
enum rn_ {
    RN_PROLOG,
    RN_EPILOG,
    RN_NUM,	/* Number of defined "reserved names". */
};

static const char *const rn_name[] = {
    "PROLOG",
    "EPILOG",
    NULL
};
    
static const char *rn_repl[RN_NUM]; /* Replacement texts for RNs. */

/*--------------------------------------------------------------------*/

/*
 * Character classification.
 */
 
#define SOH  1
#define STX  2
#define ETX  3
#define EOT  4

/*
 * The C0 control characters allowed in SGML/XML; all other C0 are
 * **not** usable in a document.
 */
#define HT   9	/* SGML SEPCHAR */
#define LF  10	/* SGML RS */
#define CR  13	/* SGML RE */
#define SP  32	/* SGML SPACE */

#define EOL          LF	    /* Per ISO C90 text stream. */

 
/*--------------------------------------------------------------------*/

/*
 * SGML function roles.
 */
#define RE           LF
#define RS           CR
#define SPACE        SP
#define ISSEPCHAR(C) ((C) == HT)

#define MSSCHAR      '\\'   /* Suppresses recognition of next char. */

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

/*--------------------------------------------------------------------*/

/*
 * The output stream (right now, this is always stdout).
 */
FILE *outfp;
int outbol = 1;

#define PUTC(ch)							\
do {									\
    putc(ch, outfp);							\
    outbol = (ch == EOL);						\
} while (0)

/*
 * During parsing of the "Replacement Definition" file we keep these
 * globals around, mostly to simplify the job of keeping track of
 * the position in the input text file (for diagnostic messages).
 */
FILE *replfp         = NULL;
const char *filename = "<no file>";
unsigned lineno      = 0U;  /* Text input position: Line number. */
unsigned colno       = 0U;  /* Text input position: Column number. */

/*
 * A convenience mechanism: In an API using arguments like
 *
 *     ..., char *data, size_t len, ...
 *
 * placing the value NTS in the actual parameter `len` indicates that
 * `data` is a NUL-terminated (byte) string aka NTBS, so the *callee*
 * can determine the correct value for `len` from `data`.
 */
#define NTS (~0U)

/*--------------------------------------------------------------------*/

void error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(EXIT_FAILURE);
}

/*====================================================================*/

/*
 * Translation definitions for a node type are hold in a 
 * `struct trans_`
 */
 
#define STAG_REPL       001
#define ETAG_REPL       002
#define STAG_BOL_START  010
#define STAG_BOL_END    020
#define ETAG_BOL_START  040
#define ETAG_BOL_END    100

struct trans_ {
    const char *stag_repl;
    const char *etag_repl;
    unsigned    defined; /* STAG_REPL, ETAG_REPL, STAG_BOL, ETAG_BOL. */
};

/*
 * Translation definitions: one array member per node type,
 * plus the (currently unused) member at index 0 == CMARK_NODE_NONE.
 */
 
static struct trans_ trans[NODE_NUM];

/*--------------------------------------------------------------------*/

typedef size_t nameidx_t; /* Index into attr_buf. */
typedef size_t validx_t;  /* Index into attr_buf. */

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
#define close_atts()   ( PUT_NAMEIDX(NULLIDX), PUT_VALIDX(NULLIDX) )
#define pop_atts()       POP_ATTS()

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


/*--------------------------------------------------------------------*/

/*
 * Set the replacement text for a node type.
 */
void set_repl(cmark_node_type nt,
	      unsigned tag_bit,
              const char *repl)
{
    assert(0 <= nt);
    assert(nt < NODE_NUM);
    
    switch (tag_bit & (STAG_REPL|ETAG_REPL)) {
    case STAG_REPL:
	if (trans[nt].defined & STAG_REPL)
	    free((void*)trans[nt].stag_repl);
	trans[nt].stag_repl = repl;
	break;
    case ETAG_REPL:
	if (trans[nt].defined & ETAG_REPL)
	    free((void*)trans[nt].stag_repl);
	trans[nt].etag_repl = repl;
	break;
    default:
	assert(!"Invalid tag_bit");
    }
    
    trans[nt].defined |= tag_bit;
}

/*--------------------------------------------------------------------*/


void repl_Attr(ESIS_UserData ud,
                          const char *name, const char *val, size_t len)
{
    push_att(name, val, len);
}

const char *put_subst(const char *p, int output);

const char *put_text(const char *p, int output)
{
    int ch;
    
    assert(p[-1] == STX);
    while ((ch = *p++) != ETX) {
	if (ch == SOH)
	    put_subst(p, output);
	else if (output)
	    PUTC(ch);
    }
    return p;
}

const char *put_subst(const char *p, int output)
{
    const char *name = p;
    const char *val = NULL;
    const char *cmpval = NULL;
    int pred;
    unsigned depth = 1U;
    
    assert(p[-1] == SOH);
    
    p = name + strlen(name)+1U;

    if (name[0] == '.')
	depth = ~0U, ++name;
    else if (ISDIGIT(name[0]))
	depth = name[0] - '0', ++name;

    if (output)
	val = att_val(name, depth);
    
    if (p[0] != STX && p[0] != EOT) {
	cmpval = p;
	p = cmpval + strlen(cmpval)+1U;
    }
    pred = (val != NULL && cmpval != NULL) ? strcmp(val, cmpval) == 0 :
           (val != NULL);
    if (p[0] == STX) {
	p = put_text(p+1, output && pred);
	if (p[0] == STX) 
	    p = put_text(p+1, output && !pred);
    } else if (output) {
	if (val != NULL) {
	    size_t k;
	    for (k = 0U; val[k] != NUL; ++k)
		PUTC(val[k]);
	}
    } 
    assert(p[0] == EOT);
    return ++p;
}

void put_repl(const char *repl, int bol[2])
{
    const char *p = repl;
    char ch;
    
    if (bol[0] && !outbol)
	PUTC(EOL);
    
    while ((ch = *p++) != NUL) {
	if (ch != SOH)
	    PUTC(ch);
	else 
	    p = put_subst(p, 1);
    }
    
    if (bol[1] && !outbol)
	PUTC(EOL);
}

void repl_Start(ESIS_UserData ud, cmark_node_type nt)
{
    const struct trans_ *tr = &trans[nt];
    
    close_atts();
    
    if (tr->defined & STAG_REPL) {
	int bol[2];
	bol[0] = (tr->defined & STAG_BOL_START) != 0;
	bol[1] = (tr->defined & STAG_BOL_END)   != 0;
	put_repl(tr->stag_repl, bol);
    }
}

void repl_Empty(ESIS_UserData ud, cmark_node_type nt)
{
    repl_Start(ud, nt);
    
    pop_atts();
}

void repl_Cdata(ESIS_UserData ud, const char *cdata, size_t len)
{
    size_t k;
    
    if (len == NTS) len = strlen(cdata);
    for (k = 0U; k < len; ++k)
	PUTC(cdata[k]);
}

void repl_End(ESIS_UserData ud, cmark_node_type nt)
{
    const struct trans_ *tr = &trans[nt];
    
    if (tr->defined & ETAG_REPL) {
	int bol[2];
	bol[0] = (tr->defined & ETAG_BOL_START) != 0;
	bol[1] = (tr->defined & ETAG_BOL_END)   != 0;
	put_repl(tr->etag_repl, bol);
    }
	
    pop_atts();
}
 
static const struct ESIS_API_ repl_API = {
    repl_Attr,
    repl_Start,
    repl_Empty,
    repl_Cdata,
    repl_End
};
 
/*====================================================================*/

/*
 * RAST
 *
 */
 
struct RAST_Param_ {
    FILE *outfp;
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
		int i = utf8len(&data[k], n);
		if (i > 0) {
		    ucs_t ucs = utf8decode(&data[k], n);
		    fprintf(fp, "#%lu\n", ucs);
		    k += i-1;
		} else {
		    unsigned m;
		    n = (unsigned)(-i);
		    if (n == 0) n = 1;
		    
		    for (m = 0; m < n; ++m) {
			fprintf(fp, "#X%02X\n", 0xFFU & data[k+m]);
		    }
		    k = k + m - 1;
		    fprintf(stderr, "Invalid UTF-8 sequence in data line!\n");
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
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    size_t nattr = NATTR;
    
    if (nattr > 0U) {
	size_t k;

	fprintf(fp, "[%s\n", nodename[nt]);
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

void rast_Empty(ESIS_UserData ud, cmark_node_type nt)
{
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    rast_Start(ud, nt);
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
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    fprintf(fp, "[/%s]\n", nodename[nt]);
    return;   
}

const struct ESIS_API_ rast_API = {
    rast_Attr,
    rast_Start,
    rast_Empty,
    rast_Cdata,
    rast_End,
    &rast_param
};

/*====================================================================*/

/*
 * Rendering into the translator.
 */
 
#define do_Attr(N, V, L)   api->attr(api->ud, N, V, L)
#define do_Start(NT)       api->start(api->ud, NT)
#define do_Empty(NT)       api->empty(api->ud, NT)
#define do_Cdata(D, L)     api->cdata(api->ud, D, L)
#define do_End(NT)         api->end(api->ud, NT)

static int S_render_node_esis(cmark_node *node,
                              cmark_event_type ev_type,
                              const ESIS_API *api)
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
      do_Attr("destination",
	                 node->as.link.url.data, node->as.link.url.len);
      do_Attr("title", node->as.link.title.data,
                                               node->as.link.title.len);
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

char *cmark_render_esis(cmark_node *root, const ESIS_API *api)
{
  cmark_event_type ev_type;
  cmark_node *cur;
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node_esis(cur, ev_type, api);
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
                         const ESIS_API *api)
{
    int bol[2];
    
    close_atts();
    bol[0] = bol[1] = 0;
    
    if (rn_repl[RN_PROLOG] != NULL) {
	put_repl(rn_repl[RN_PROLOG], bol);
    }

    cmark_render_esis(document, api);

    if (rn_repl[RN_EPILOG] != NULL) {
	put_repl(rn_repl[RN_EPILOG], bol);
    }
}

/*====================================================================*/

size_t do_prolog(char *buffer, size_t nbuf, const ESIS_API *api)
{
    size_t ibol, nused;
    unsigned dc_count = 0U;
    static const char *dc_name[] = {
	META_DC_TITLE,
	META_DC_CREATOR,
	META_DC_DATE,
	NULL,
    };

    ibol = 0U;
    nused = 0U;
    
    do_Attr(META_DC_TITLE,   DEFAULT_DC_TITLE,   NTS);
    do_Attr(META_DC_CREATOR, DEFAULT_DC_CREATOR, NTS);
    do_Attr(META_DC_DATE,    DEFAULT_DC_DATE,    NTS);
    
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
		do_Attr(dc_name[dc_count++], buffer+ifield, len);
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
		    
		    do_Attr(name, val, nval);
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

/*====================================================================*/

/*
 * setup -- Parse the replacement definition file.
 */
 

#define COUNT_EOL() (++lineno, colno = 0U)

#define GETC()     ( ch = getc(replfp),					\
                     ((ch == EOL) ? COUNT_EOL() : ++colno),		\
                     ch )
                   
#define UNGETC(ch) ( ungetc(ch, replfp),				\
                     assert(colno > 0U),				\
                     --colno,						\
                     ch )

/*
 * S = SPACE | SEPCHAR | RS | RE
 *
 * accept ( { S } )
 *
 * Uses char ch.
 */
#define S()								\
do {									\
    while (ISSPACE(ch))							\
	ch = GETC();							\
} while (0)

/*
 * Where DELIM = ( CHAR , [ CHAR ] )
 *
 * accept? ( DELIM )
 *
 * Uses `char ch`.
 */
#define DELIM(STR) delim_(&ch, STR)

int delim_(int *pch, const char STR[2])
{
    int ch = *pch;
    int ch0 = ch;
    
    if (ch == STR[0]) {
	if (STR[1] == NUL) {
	    *pch = GETC();
	    return 1;
	} else {
	    ch = GETC();
	    if (ch == STR[1]) {
	        *pch = GETC();
		return 1;
	    } else {
		UNGETC(ch);
		*pch = ch0;
		return 0;
	    }
	}
    } else
	return 0;
}

void syntax_error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "%s(%u:%u): error: ", filename, lineno, colno);
    vfprintf(stderr, msg, va);
    va_end(va);
}

int comment(const char lit[2])
{
    int ch;

    while ((ch = GETC()) != EOF)
	if (DELIM(lit))
	    return ch;
	    
    return ch;
}

char *get_repl(int, int[2]);

int rni_repl(int ch)
{
    char name[NAMELEN+1], *p = name;
    enum rn_ rn;
    const char *repl;
    int bol[2];
    
    if (ISNMSTART(ch))
	*p++ = toupper(ch);
    else
	syntax_error("'%c': Not a NMSTART.\n", ch);
    
    while ((ch = GETC()) != EOF)
	if (ISNMCHAR(ch))
	    *p++ = toupper(ch);
	else
	    break;
    *p = NUL;

    /*
     * Look up the "reserved name".
     */
    for (rn = 0; rn_name[rn] != NULL; ++rn)
	if (!strcmp(rn_name[rn], name))
	    break;
    
    if (rn_name[rn] == NULL) {
	syntax_error("\"%s\": Unknown name.\n", name);
	return ch;
    }
	
    repl = get_repl(ch, bol);
    
    if (repl != NULL) {
	free((void*)rn_repl[rn]);
	rn_repl[rn] = repl;
    } 
    return GETC();
}

/*
 * subst = "[" , name , [ "?" , { elem } , [ ":"  , { elem } ] ] , "]" ;
 *
 *         SOH , str , NUL , [ STX , subst ,  [ ETB  , subst ] ] , EOT
 *
 * elem = restricted char | escape sequence | subst ;
 * restricted char = char - ( LIT | ":" | "]" ) ;
 */
int substitution(cmark_strbuf *pbuf, int ch, const char lit[2])
{
    enum { IN_ATTR_NAME , IN_CMP_VAL, IN_TEXT_1 , IN_TEXT_2 } state;
    
    cmark_strbuf_putc(pbuf, SOH);
    
    state = IN_ATTR_NAME;
    while (ch != EOF && !DELIM(DSC)) {
	if (DELIM(lit)) {
	    syntax_error("Unclosed attribute reference "
		"(missing '" DSC "').\n");
	    if (state == IN_ATTR_NAME)
		cmark_strbuf_putc(pbuf, NUL);
	    else
		cmark_strbuf_putc(pbuf, ETX);
	    break;
	}
	
	switch (state) {
	case IN_ATTR_NAME:
	    if (ISSPACE(ch))
		syntax_error("Space in attribute name.\n");
	    else if (ch == '=') { 
		cmark_strbuf_putc(pbuf, NUL);
		state = IN_CMP_VAL;
	    } else if (ch == '?') {
		cmark_strbuf_putc(pbuf, NUL);
		cmark_strbuf_putc(pbuf, STX);
		state = IN_TEXT_1;
	    } else
		cmark_strbuf_putc(pbuf, ch);
	    ch = GETC();
	    break;
	case IN_CMP_VAL:
	    if (ch == '?') {
		cmark_strbuf_putc(pbuf, NUL);
		cmark_strbuf_putc(pbuf, STX);
		state = IN_TEXT_1;
	    } else 
		cmark_strbuf_putc(pbuf, ch);
	    ch = GETC();
	    break;
	case IN_TEXT_1:
	case IN_TEXT_2:
	    if (ch == ':') {
		if (state == IN_TEXT_1) {
		    cmark_strbuf_putc(pbuf, ETX);
		    cmark_strbuf_putc(pbuf, STX);
		    state = IN_TEXT_2;
		} else {
		    syntax_error("Already had ':'\n");
		}
		ch = GETC();
	    } else if (DELIM(DSO)) {
		ch = substitution(pbuf, ch, lit);
	    } else {
		cmark_strbuf_putc(pbuf, ch);
		ch = GETC();
	    }
	}
    }
    
    if (state == IN_ATTR_NAME)
	cmark_strbuf_putc(pbuf, NUL);
    else
	cmark_strbuf_putc(pbuf, ETX);
    cmark_strbuf_putc(pbuf, EOT);
    
    return ch;
}

int get_string(cmark_strbuf *pbuf, int ch, const char lit[2])
{
    while (!DELIM(lit)) {
	if (ch == MSSCHAR) {
	
	    switch (ch = GETC()) {
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
	    ch = GETC();
	    
	} else if (DELIM(DSO)) {
	
	    ch = substitution(pbuf, ch, lit);
	    
	} else {
	    cmark_strbuf_putc(pbuf, ch);
	    ch = GETC();
	}
    }    
    return ch;
}

char *get_repl(int ch, int bol[2])
{
    cmark_strbuf repl;
    unsigned nstrings = 0U;
    
    bol[0] = bol[1] = 0;
    cmark_strbuf_init(&repl, 16);
    
    S();
    
    if (DELIM(PLUS)) 
	bol[0] = 1;
    
    while (ch != EOF) {

	S();
	
	if (DELIM(LIT))
	    ch = get_string(&repl, ch, LIT);
	else if (DELIM(LITA))
	    ch = get_string(&repl, ch, LITA);
	else
	    break;
	++nstrings;
    }
    
    S();
    
    if (DELIM(PLUS)) 
	bol[1] = 1;
    else
	UNGETC(ch);
    
    if (nstrings > 0U) {
	cmark_strbuf_putc(&repl, NUL);
	return (char*)cmark_strbuf_detach(&repl);
    } else
	return NULL;
}


int name_repl(int ch, unsigned bits)
{
    char name[NODENAME_LEN+1];
    char *p = name;
    cmark_node_type nt;
    const char *repl;
    int bol[2];
    
    *p++ = toupper(ch);
    
    while ((ch = GETC()) != EOF) {
	if (ISNMCHAR(ch))
	    *p++ = toupper(ch);
	else if (DELIM(TAGC))
	    break;
	else
	    syntax_error("'%c': Not a NMCHAR.\n", ch);    
    }
    *p = NUL;

    /*
     * Look up the "GI" for a CommonMark node type.
     */
    for (nt = 1; nodename[nt] != NULL; ++nt)
	if (!strcmp(nodename[nt], name))
	    break;
    if (nodename[nt] == NULL) {
	syntax_error("\"%s\": Not a CommonMark node type.", name);
	return ch;
    }

    repl = get_repl(ch, bol);
    
    switch (bits & (STAG_REPL|ETAG_REPL)) {
    case STAG_REPL:
	if (bol[0]) bits |= STAG_BOL_START;
	if (bol[1]) bits |= STAG_BOL_END;
	break;
    case ETAG_REPL:
	if (bol[0]) bits |= ETAG_BOL_START;
	if (bol[1]) bits |= ETAG_BOL_END;
	break;
    }
    
    set_repl(nt, bits, repl);
    return GETC();
}


static unsigned    repl_filecount = 0U;
static const char *repl_dir = NULL;
static const char *repl_default = NULL;
    
#ifdef _WIN32
#define DIRSEP "\\"
#else
#define DIRSEP "/"
#endif
static const char dirsep[] = DIRSEP;

int is_relpath(const char *pathname)
{
    if (pathname[0] == dirsep[0])
	return 0;
#ifdef _WIN32
    if ( (('A' <= pathname[0] && pathname[0] <= 'Z') || 
          ('a' <= pathname[0] && pathname[0] <= 'z')) && 
         pathname[1] == ':' )
	return 0;
#endif
    return 1;
}

void setup(const char *repl_filename)
{
    int ch;
    
    if (repl_dir == NULL)     repl_dir = getenv(REPL_DIR_VAR);
    if (repl_default == NULL) repl_default = getenv(REPL_DEFAULT_VAR);
    
    /*
     * Passing in NULL means: use the default replacement definition.
     */
    if (repl_filename == NULL) {
	repl_filename = repl_default;
	if (repl_filename == NULL)
	    return;
    }
	
    /*
     * First try the given filename literally.
     */
    filename = repl_filename;
    replfp = fopen(filename, "r");
    
    /*
     * Otherwise, try a *relative* pathname with the REPL_DIR_VAR
     * environment variable.
     */
    if (replfp == NULL && repl_dir != NULL) {
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
	    replfp = fopen(pathname, "r");
	    if (replfp != NULL) filename = pathname;
	    else free(pathname);
	}
    }
    
    /*
     * If we **still** have no replacement definition file, it is
     * time to give up.
     */
    if (replfp == NULL) {
	error("Can't open replacement file \"%s\": %s.", filename,
	                                               strerror(errno));
    } else
	++repl_filecount;
    
    COUNT_EOL();
    
    cmark_strbuf_init(&attr_buf, ATTSPLEN);
    cmark_strbuf_putc(&attr_buf, NUL); /* Index = 0U is unsused.*/
    
    cmark_strbuf_init(&nameidx_buf, ATTCNT * sizeof(nameidx_t));
    cmark_strbuf_init(&validx_buf,  ATTCNT * sizeof(validx_t));

    ch = GETC();
    while (ch != EOF) {
	S();
	
	if (ch == EOF)
	    break;

	if (DELIM(ETAGO)) {
	    if (ISNMSTART(ch))
		ch = name_repl(ch, ETAG_REPL);
	    else
		syntax_error("\'%c\' after '" ETAGO "'", ch);
	} else if (DELIM(STAGO)) {
	    if (ISNMSTART(ch))
    		ch = name_repl(ch, STAG_REPL);
	    else
		syntax_error("\'%c\' after '" STAGO "': Not a NMSTART.\n", ch);
	} else if (DELIM(RNI))
	    ch = rni_repl(ch);
	else if (DELIM("%"))
	    ch = comment("\n");
	else if (DELIM(COM))
	    ch = comment(COM);
	else
	    syntax_error("Unexpected character \'%c\'.\n", ch);
    }
		
    fclose(replfp);
    replfp = NULL;
}

/*====================================================================*/

void usage()
{
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


int main(int argc, char *argv[])
{
    FILE *fp;
    bool in_header;

    int argi;
    char buffer[16*BUFSIZ];
    size_t bytes;
    time_t now;
    const char *username;
    const char *title_arg = NULL, *css_arg = NULL;
    
    cmark_parser *parser;
    cmark_node *document;
    cmark_option_t options = CMARK_OPT_DEFAULT | CMARK_OPT_ISO;

    const ESIS_API *api = &repl_API;
    int doing_rast = 0;

    rast_param.outfp = stdout;
    
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
	    if (doing_rast) {
		fprintf(stderr, "--rast can't use --repl!\n");
		exit(EXIT_FAILURE);
	    }
	    setup(filename);
	} else if (strcmp(argv[argi], "--rast") == 0) {
	    api = &rast_API;
	    doing_rast = 1;
	} else if ((strcmp(argv[argi], "--title") == 0) ||
	    (strcmp(argv[argi], "-t") == 0)) {
		title_arg = argv[++argi];
	} else if ((strcmp(argv[argi], "--css") == 0) ||
	    (strcmp(argv[argi], "-c") == 0)) {
		css_arg = argv[++argi];
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
    
    /*
     * If no replacement file was mentioned (and processed),
     * try using the default replacement file given in the
     * environment.
     */
    if (repl_filecount == 0U && !doing_rast)
	setup(NULL);

    parser = cmark_parser_new(options);

    fp        = stdin;
    outfp     = stdout;
    in_header = true;

    switch (argc - argi) do {
    default:
	if ((fp = freopen(argv[argi], "r", stdin)) == NULL)
	    error("Can't open \"%s\": %s\n", argv[argi],
	                                               strerror(errno));
	    
    case 0:
	while ((bytes = fread(buffer, 1U, sizeof buffer, fp)) > 0) {
	    size_t hbytes = 0U;

	    if (in_header) {
		char version[1024];
		
		if (doing_rast) {
		}
		hbytes = do_prolog(buffer, sizeof buffer, api);
		
		if (title_arg != NULL)
		    do_Attr(META_DC_TITLE, title_arg, NTS);
		    
		do_Attr(META_CSS, css_arg ? css_arg : DEFAULT_CSS, NTS);
		
		sprintf(version, "            %s;\n"
		                 "            date: %s;\n"
		                 "            id: %s\n"
		                 "        ",
			cmark_repourl,
			__DATE__ ", " __TIME__,
			cmark_gitident);
		do_Attr("cm2doc.version", version, NTS);
		do_Attr("cmark.version", CMARK_VERSION_STRING, NTS);
		
		in_header = false;
	    }

	    if (hbytes < bytes)
		cmark_parser_feed(parser, 
		                       buffer + hbytes, bytes - hbytes);

	    if (bytes < sizeof(buffer)) {
		break;
	    }
	}
    } while (++argi < argc);

    document = cmark_parser_finish(parser);
    cmark_parser_free(parser);

    gen_document(document, options, api);

    cmark_node_free(document);

    return EXIT_SUCCESS;
}

/*== EOF =============================================================*/