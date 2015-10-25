#ifndef ESISIO_H_INCLUDED
#define ESISIO_H_INCLUDED

/*
   esisio --
   
       Reading and writing texts streams in the `nsgmls` output format,
       representing the Element Structure Information Set (ESIS) of an
       XML or SGML document.

       The API defined herein does heavily borrow from the one provided
       by the Expat 1.2 XML Parser Toolkit by James Clarke, see:

           http://www.jclark.com/xml/expat.html

       The primary purpose of this library is to support tools that
       transform or consume such ESIS representation streams.

       Functions to write output in XML or SGML format are also
       provided: Together with an XML parser, this library can be used
       to implement conversion from the marked-up form of XML and
       SGML documents to the ESIS representation (either by stand-
       alone parser like `nsgmls`, producing the ESIS representation,
       or by using an XML parser library like `Expat` to read the ESIS
       representation directly from the linked-in parser.
       
       Like Expat 1.2, this library is published under the MIT license:
       
           https://opensource.org/licenses/MIT

       reproduced in the file `copying.txt`.
       
 */
   
#include <stdio.h> /* FILE */
#include "esisio_external.h"

#ifdef ESIS_XMLPARSE   /* NOT IMPLEMENTED */
# include <xmlparse.h> /* Use Expat as a front end for XML input. */
#endif

#ifdef __cplusplus 
extern "C" {
#endif

#ifndef __cplusplus
#ifdef ESIS_STDBOOL
# include <stdbool.h>
  typedef bool ESIS_Bool;
# define ESIS_TRUE   true
# define ESIS_FALSE  false
#else
  typedef unsigned char ESIS_Bool;
# define ESIS_TRUE   ((ESIS_Bool) 1)
# define ESIS_FALSE  ((ESIS_Bool) 0)
#endif
#endif /* __cplusplus */

struct ESIS_ParserStruct;
typedef struct ESIS_ParserStruct *ESIS_Parser;

typedef enum ESIS_Error {
    ESIS_ERROR_NONE,
    ESIS_ERROR_NO_MEMORY,
    ESIS_ERROR_FILE_READ,
    ESIS_ERROR_FILE_WRITE,
    ESIS_ERROR_SYNTAX,
    ESIS_ERROR_INTERNAL
} ESIS_Error;

enum {
    ESIS_CANONICAL  = 010000,       /* Output Canonical XML. */
};

typedef enum {
    ESIS_START,
    ESIS_CDATA,
    ESIS_END
} ESIS_ElemEvent;

typedef struct ESIS_el ESIS_Elem;

struct ESIS_el {
    const ESIS_Char  *elemGI;
    const ESIS_Char **atts;
};


ESIS_Parser ESISAPI
ESIS_ParserCreate(const ESIS_Char *encoding);

#ifdef ESIS_XMLPARSE /* NOT IMPLEMENTED */
ESIS_Parser ESISAPI
ESIS_XmlParserCreate(XML_Parser xmlParser);
#endif

ESIS_Bool ESISAPI
ESIS_ParserReset(ESIS_Parser, const ESIS_Char *encoding);


typedef ESIS_Bool (*ESIS_ElementHandler) (
					void               *userData,
					ESIS_ElemEvent      elemEvent,
					long                elemID,
					const  ESIS_Elem   *elem,
					const  ESIS_Char   *charData,
					size_t              len);

void ESISAPI ESIS_SetElementHandler(
					ESIS_Parser         parser,
					ESIS_ElementHandler handler,
					const ESIS_Char    *elemGI,
					long                elemID,
					void               *userData);

/*
 * The following three handler types and handler setting functions are
 * for compatibility with Expat 1.2 -- implement if needed.
 */
 
typedef void (*ESIS_StartElementHandler) (
					void               *userData,
					const  ESIS_Char   *name,
					const  ESIS_Char  **atts);

typedef void (*ESIS_EndElementHandler) (
					void               *userData,
					const  ESIS_Char   *name);

typedef void (*ESIS_CharacterDataHandler) (
					void               *userData,
					const  ESIS_Char   *charData,
					int                 len);

void ESISAPI ESIS_SetExpUserData(  ESIS_Parser               parser,
                                   void                     *userData);

void ESISAPI ESIS_SetExpElementHandler(
			           ESIS_Parser               parser,
				   ESIS_StartElementHandler  start,
				   ESIS_EndElementHandler    end);

void ESISAPI ESIS_SetExpCharacterDataHandler(
				   ESIS_Parser               parser,
				   ESIS_CharacterDataHandler data);

/*
 * An element E is "seen" as open through the ESIS_Env() function
 *
 *   - from the time just before a start tag callback for a
 *     *nested* element F is or would be called, 
 *
 *   - until just after the end tag callback for the nested F element
 *     is called
 *
 * and so on for each nested element: Here is which element would
 * ESIS_Env() present as the inner-most open element over time:
 *
 *     <o>...<E>...<F>...</F>...<G>...</G>...</E>...</o>
 *           ooooooEEEEEEEEEEoooEEEEEEEEEEooooooo
 *
 * As a consequence, the callback handler for an element E (in it's
 * start tag/char data/end tag invocations) *never* sees E open, but can
 * use
 *
 *     ESIS_Elem openElements[1];
 *
 *     if (ESIS_Env(parser, 1, openElements))
 *       printf("Inside an <%s> element.\n", openElements[0].elemGI);
 *     else
 *       printf("At top level.\n");
 *
 * to find out about the element O in which E occurs (if any, otherwise
 * E is (one of several or the only) top-level element in the ESIS
 * stream, and ESIS_Env() returns 0U).
 *
 * The outElems array must have room for at least depth elements.
 * For each nesting level, from inside to outside, one element
 * in the array is set up to hold the open element's GI and
 * attributes.
 *
 * If outElems == NULL, the current nesting level is returned,
 * ie the number of currently un-closed elements, which is the
 * maximum number of outElems records this function would write.
 *
 * Either the number of array elements written is returned,
 * or the number of currently open elements.
 */
 
size_t ESISAPI
ESIS_Env(ESIS_Parser, ESIS_Elem outElems[], size_t depth);


/*
   This structure is filled in by the ESIS_UnknownEncodingHandler to
   provide information to the parser about encodings that are unknown to
   the parser (specified in the `?xml` PI at the start of an ESIS
   stream).

   The map[b] member gives information about byte sequences whose first
   byte is b.

   If map[b] is c where c is >= 0, then b by itself encodes the Unicode
   scalar value c.

   If map[b] is -1, then the byte sequence is malformed.

   If map[b] is -n, where n >= 2, then b is the first byte of an n-byte
   sequence that encodes a single Unicode scalar value. The data member
   will be passed as the first argument to the convert function.

   The convert function is used to convert multibyte sequences; s will
   point to a n-byte sequence where map[(unsigned char)*s] == -n. The
   convert function must return the Unicode scalar value represented
   by this byte sequence or -1 if the byte sequence is malformed.
   The convert function may be null if the encoding is a single-byte
   encoding, that is if map[b] >= -1 for all bytes b.

   When the parser is finished with the encoding, then if release is not
   null, it will call release passing it the data member; once release
   has been called, the convert function will not be called again.

   This library places certain restrictions on the encodings that are
   supported using this mechanism.

    1. Every ASCII character that can appear in a well-formed XML
       document, other than the characters

           $@\^`{}~

       must be represented by a single byte, and that byte must be the
       same byte that represents that character in ASCII.

    2. No character may require more than 4 bytes to encode.

    3. All characters encoded must have Unicode scalar values <= 0xFFFF,
       (ie characters that would be encoded by surrogates in UTF-16 are
       not allowed). Note that this restriction doesn't apply to the
       built-in support for UTF-8 and UTF-16.

    4. No Unicode character may be encoded by more than one distinct
       sequence of bytes.
 */

typedef struct {
	int              map[256];
	void            *data;
	int  (ESISCALL  *convert)(void *data, const char *s);
	void (ESISCALL  *release)(void *data);
} ESIS_Encoding;


/*
   This is called for an encoding that is unknown to the parser. The
   encodingHandlerData argument is that which was passed as the second
   argument to ESIS_SetUnknownEncodingHandler.

   The name argument gives the name of the encoding as specified in the
   encoding declaration.

   If the callback can provide information about the encoding, it must
   fill in the ESIS_Encoding structure, and return 1.

   Otherwise it must return 0.

   If info does not describe a suitable encoding, then the parser will
   return an ESIS_UNKNOWN_ENCODING error.
 */

typedef int (ESISCALL *ESIS_UnknownEncodingHandler) (
				void            *encodingHandlerData,
				const ESIS_Char *name,
				ESIS_Encoding   *info);


/*
   Parses some input. Returns 0 if a fatal error is detected.

   The last call to ESIS_Parse must have isFinal true; len may be zero
   for this call (or any other).
 */

int ESISAPI
ESIS_ParseFile(ESIS_Parser parser, FILE *inputFile);

int ESISAPI
ESIS_FilterFile(ESIS_Parser parser, FILE *inputFile, FILE *outputFile);

int ESISAPI
ESIS_Parse(ESIS_Parser parser, const char *s, size_t len, int isFinal);

void * ESISAPI
ESIS_GetBuffer(ESIS_Parser parser, size_t len);

int ESISAPI
ESIS_ParseBuffer(ESIS_Parser parser, size_t len, int isFinal);

/*
   Frees memory used by the parser.
 */

void ESISAPI ESIS_ParserFree(ESIS_Parser parser);

/*
   Returns a string describing the error.
 */

const char ESISAPI *ESIS_ErrorString(int code);

/*
If ESIS_Parse or ESIS_ParseBuffer have returned 0, then ESIS_GetParserError
returns information about the error.
*/

enum ESIS_Error ESISAPI ESIS_GetParserError(ESIS_Parser parser);

/**********************************************************************/

/*
 * Output support: writing ESIS, XML, and SGML files.
 */

struct ESIS_WriterStruct;

typedef struct ESIS_WriterStruct *ESIS_Writer;


ESIS_Writer ESISAPI
ESIS_WriterCreate(FILE *, unsigned options);

ESIS_Writer ESISAPI
ESIS_XmlWriterCreate(FILE *, unsigned options);

ESIS_Writer ESISAPI
ESIS_SgmlWriterCreate(FILE *, const ESIS_Char *encoding);


/*
   If the number of characters passed in a ESIS_Char * argument is
   specified as ESIS_NTS, a NUL-terminated string is assumed, and the
   length is determined by the called function.
 */

#define ESIS_NTS (~(size_t)0U)

void ESISAPI
ESIS_Attr(ESIS_Writer, const ESIS_Char  *name,
                       const ESIS_Char  *val, size_t len);

void ESISAPI
ESIS_Atts(ESIS_Writer, const ESIS_Char **atts);


/*
   The functions for creating start tags, ESIS_Start* and ESIS_Empty*,
   use

    1. the attributes passed in through preceding calls to ESIS_Attr
       and/or ESIS_Atts, and also

    2. the attributes passed in through their own atts argument,
       if any

   for the new element about to start.

   IMPORTANT: Between the ESIS_Attr* calls to set up attributes
   and the ESIS_Start* or ESIS_Empty* calls to use them,
   *no other ESIS function* should be called on this parser!

   After using the passed-in attributes in creating the element start 
   output, the parser immediately "forgets" them again: it is
   the application's job to remember what they were, if needed.

   If the application "knows" that the given element has an empty
   content model in the document type in use (as the application
   should!), the correct function to call is ESIS_Empty*, because it
   will output eg "`<HR>`" into SGML, but "`<HR />`" into XML. 
   
   [NOTE: For ESIS output, there is no difference between ESIS_Start*
   and ESIS_Empty*, they both produce an "`(`" line and a following "`)`"
   line each time they are called.]
   
   [NOTE: For XML/SGML output, the attribute values are transformed
   for output like character data is by the ESIS_PCdata function.]

   [NOTE: An element that just happens to be empty, like "`<P></P>`" in
   HTML, should **NOT** be written through ESIS_Empty* -- use this
   function **ONLY** for elements of a type that by definition (in the
   DTD etc) will *never* have any content.]
   
 */

void ESISAPI
ESIS_Start(ESIS_Writer,     const ESIS_Char  *elemGI,
                            const ESIS_Char **atts);

void ESISAPI
ESIS_StartElem(ESIS_Writer, const ESIS_Elem  *elem);

void ESISAPI
ESIS_Empty(ESIS_Writer,     const ESIS_Char  *elemGI,
                            const ESIS_Char **atts);

void ESISAPI
ESIS_EmptyElem(ESIS_Writer, const ESIS_Elem  *elem);


/*
   The difference between the two character data output functions
   ESIS_PCdata and ESIS_Cdata is the same as between CDATA and #PCDATA
   in element content models:

     - in #PCDATA content, each character "active" in mark-up, like
       "`<`", must be represented by a corresponding entity (either a
       named or a numeric character reference) like "`&lt;`".

     - in CDATA content, no such substitution is required (the end of
       the CDATA is usually found by "special means", like the end of a
       CDATA section at a "`]]>`" sequence.

   The character data passed into ESIS_PCdata and ESIS_Cdata is
   transformed in different ways for XML/SGML output, and for ESIS
   output:

    - When writing XML or SGML output,

       1. ESIS_PCdata *does* substitute "active" characters by named
          entity references (but only "`<`", "`&`", and "`"`" will be
          represented in this way as "`&lt;`", "`&amp;`", and "`&quot;`"
          in the output), while

       2. ESIS_Cdata *does not* do this substitution; and

       3. characters outside the output encoding's character set will be
          represented by numeric character references "`&#ddd;`" with
          decimal digits by both functions.

    - when writing ESIS output, the substitution by entity references
      will *never* be applied, and for this form of output, both
      functions are equivalent. Instead,

       1. the EOL character "`\n`" is transformed to a RE/RS pair
          "`\n\012`",

       2. other control characters (in the range U+0000..U+0019) will be
          represented in the output by a "`\nnn`" sequence using three
          octal digits, and

       3. characters outside the output encoding's character set will be
          represented by a "`\#n;`" sequence using decimal digits.
 */

void ESISAPI
ESIS_PCdata(ESIS_Writer,  const ESIS_Char *cd, size_t len);
void ESISAPI
ESIS_Cdata(ESIS_Writer,   const ESIS_Char *cd, size_t len);

/*
 * This one (with an `int` length parameter) is for Expat 1.2
 * compatibility.
 */
 
void ESISAPI
ESIS_CdataExp(ESIS_Writer, const ESIS_Char *cd, int len);

/*
 * The ESIS_End* functions output an end tag rsp a "`)`" line.
 */

void ESISAPI
ESIS_End(ESIS_Writer,     const ESIS_Char *elemGI);
void ESISAPI
ESIS_EndElem(ESIS_Writer, const ESIS_Elem *elem);

enum ESIS_Error ESISAPI ESIS_GetWriterError(ESIS_Writer writer);

void ESISAPI ESIS_WriterFree(ESIS_Writer);

/*
 * Functions analogous to the Expat API
 *
 * ************ THESE ARE ALL NOT IMPLEMENTED YET *********************
 */
 
void ESISAPI
ESIS_SetUnknownEncodingHandler(
				ESIS_Parser parser,
				ESIS_UnknownEncodingHandler handler,
				void *encodingHandlerData);


/*
   This can be called within a handler for a start element, end element,
   processing instruction or character data. It causes the corresponding
   markup to be passed to the default handler.
 */

void ESISAPI
ESIS_DefaultCurrent(ESIS_Parser parser);
 
 
/*
   This value is passed as the userData argument to callbacks.
 */

void ESISAPI
ESIS_SetUserData(ESIS_Parser parser, long elemID, void *userData);


/*
   Returns the last value set by ESIS_SetUserData or NULL.
 */

void * ESISAPI
ESIS_GetUserData(ESIS_Parser, long elemID);


/*
   This is equivalent to supplying an encoding argument to
   ESIS_ParserCreate. It must not be called after ESIS_Parse or
   ESIS_ParseBuffer.
 */

int ESISAPI
ESIS_SetEncoding(ESIS_Parser parser, const ESIS_Char *encoding);


/*
   If this function is called, then the parser will be passed as the
   first argument to callbacks instead of userData. The userData will
   still be accessible using ESIS_GetUserData.
 */

void ESISAPI
ESIS_UseParserAsHandlerArg(ESIS_Parser parser);


/*
   Returns the number of the attribute/value pairs passed in last call
   to the ESIS_ElementHandler that were specified in the start-
   tag rather than defaulted. Each attribute/value pair counts as 2;
   thus this correspondds to an index into the atts array passed to the
   ESIS_ElementHandler.
 */

int ESISAPI
ESIS_GetSpecifiedAttributeCount(ESIS_Parser parser);


/*
   Returns the index of the ID attribute passed in the last call to
   ESIS_ElementHandler, or -1 if there is no ID attribute. Each
   attribute/value pair counts as 2; thus this correspondds to an index
   into the atts array passed to the ESIS_ElementHandler.
 */

int ESISAPI
ESIS_GetIdAttributeIndex(ESIS_Parser parser);

#ifdef __cplusplus
}
#endif

#endif/*ESISIO_H_INCLUDED*/
