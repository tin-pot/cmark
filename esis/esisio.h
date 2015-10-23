/* esisio.h */

#ifndef ESISIO_H_INCLUDED
#define ESISIO_H_INCLUDED

#include <stdio.h> /* FILE */
#include "esisio_external.h"

#ifdef ESIS_XMLPARSE
# include "expat.h" /* Use Expat as a front end for XML. */
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

/*
   The ESIS_Status enum gives the possible return values for several
   API functions (just like XML_Status does in Expat ...).
*/
enum ESIS_Status {
  ESIS_STATUS_ERROR     = 0,
  ESIS_STATUS_OK        = 1,
  ESIS_STATUS_SUSPENDED = 2
};

enum ESIS_Error {
    ESIS_ERROR_NONE,
    ESIS_ERROR_NO_MEMORY,
    /* :TODO: Error codes, but analoguous to Expat's. */
};

typedef struct ESIS_el ESIS_Elem;

struct ESIS_el {
    const ESIS_Char  *elemGI;
    const ESIS_Char **atts;
};


ESISPARSEAPI(ESIS_Parser)
ESIS_ParserCreate(const ESIS_Char *encoding);

#ifdef ESIS_XMLPARSE
ESISPARSEAPI(ESIS_Parser)
ESIS_XmlParserCreate(XML_Parser xmlParser);
#endif

ESISPARSEAPI(ESIS_Bool)
ESIS_ParserReset(ESIS_Parser, const ESIS_Char *encoding);

typedef ESIS_Bool (ESISCALL *ESIS_ElementHandler) (
                        void             *userData,
                        long              elemID,
                        const  ESIS_Elem *elem,
                        const  ESIS_Char *charData,
                        size_t            len);
	
ESISPARSEAPI(void)
ESIS_SetElementHandler(ESIS_Parser         parser,
                       ESIS_ElementHandler handler,
	               const ESIS_Char    *elemGI,
	               long                elemID,
	               void               *userData);
	
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
 
ESISPARSEAPI(size_t)
ESIS_Env(ESIS_Parser, ESIS_Elem outElems[], size_t depth);

/* This structure is filled in by the ESIS_UnknownEncodingHandler to
   provide information to the parser about encodings that are unknown
   to the parser.

   The map[b] member gives information about byte sequences whose
   first byte is b.

   If map[b] is c where c is >= 0, then b by itself encodes the
   Unicode scalar value c.

   If map[b] is -1, then the byte sequence is malformed.

   If map[b] is -n, where n >= 2, then b is the first byte of an
   n-byte sequence that encodes a single Unicode scalar value.

   The data member will be passed as the first argument to the convert
   function.

   The convert function is used to convert multibyte sequences; s will
   point to a n-byte sequence where map[(unsigned char)*s] == -n.  The
   convert function must return the Unicode scalar value represented
   by this byte sequence or -1 if the byte sequence is malformed.

   The convert function may be NULL if the encoding is a single-byte
   encoding, that is if map[b] >= -1 for all bytes b.

   When the parser is finished with the encoding, then if release is
   not NULL, it will call release passing it the data member; once
   release has been called, the convert function will not be called
   again.

   Expat places certain restrictions on the encodings that are supported
   using this mechanism.

   1. Every ASCII character that can appear in a well-formed XML document,
      other than the characters

      $@\^`{}~

      must be represented by a single byte, and that byte must be the
      same byte that represents that character in ASCII.

   2. No character may require more than 4 bytes to encode.

   3. All characters encoded must have Unicode scalar values <=
      0xFFFF, (i.e., characters that would be encoded by surrogates in
      UTF-16 are  not allowed).  Note that this restriction doesn't
      apply to the built-in support for UTF-8 and UTF-16.

   4. No Unicode character may be encoded by more than one distinct
      sequence of bytes.
*/
typedef struct {
  int map[256];
  void *data;
  int (ESISCALL *convert)(void *data, const char *s);
  void (ESISCALL *release)(void *data);
} ESIS_Encoding;

/* This is called for an encoding that is unknown to the parser.

   The encodingHandlerData argument is that which was passed as the
   second argument to ESIS_SetUnknownEncodingHandler.

   The name argument gives the name of the encoding as specified in
   the encoding declaration.

   If the callback can provide information about the encoding, it must
   fill in the ESIS_Encoding structure, and return ESIS_STATUS_OK.
   Otherwise it must return ESIS_STATUS_ERROR.

   If info does not describe a suitable encoding, then the parser will
   return an ESIS_UNKNOWN_ENCODING error.
*/
typedef int (ESISCALL *ESIS_UnknownEncodingHandler) (
                                void            *encodingHandlerData,
                                const ESIS_Char *name,
                                ESIS_Encoding   *info);
                                    
/* Parses some input. Returns ESIS_STATUS_ERROR if a fatal error is
   detected.  The last call to ESIS_Parse must have isFinal true; len
   may be zero for this call (or any other).

   Though the return values for these functions has always been
   described as a Boolean value, the implementation, at least for the
   1.95.x series, has always returned exactly one of the ESIS_Status
   values.
*/

ESISPARSEAPI(enum ESIS_Status)
ESIS_Parse(ESIS_Parser parser, const char *s, size_t len, int isFinal);

ESISPARSEAPI(void *)
ESIS_GetBuffer(ESIS_Parser parser, size_t len);

ESISPARSEAPI(enum ESIS_Status)
ESIS_ParseBuffer(ESIS_Parser parser, size_t len, int isFinal);

/*
   Stops parsing, causing ESIS_Parse() or ESIS_ParseBuffer() to return.
   Must be called from within a call-back handler, except when aborting
   (resumable = 0) an already suspended parser. Some call-backs may
   still follow because they would otherwise get lost. Examples:
   - endElementHandler() for empty elements when stopped in
     startElementHandler(), 
   - endNameSpaceDeclHandler() when stopped in endElementHandler(), 
   and possibly others.

   Can be called from most handlers, including DTD related call-backs,
   except when parsing an external parameter entity and resumable != 0.
   Returns ESIS_STATUS_OK when successful, ESIS_STATUS_ERROR otherwise.
   Possible error codes: 
   - ESIS_ERROR_SUSPENDED: when suspending an already suspended parser.
   - ESIS_ERROR_FINISHED: when the parser has already finished.
   - ESIS_ERROR_SUSPEND_PE: when suspending while parsing an external PE.

   When resumable != 0 (true) then parsing is suspended, that is, 
   ESIS_Parse() and ESIS_ParseBuffer() return ESIS_STATUS_SUSPENDED. 
   Otherwise, parsing is aborted, that is, ESIS_Parse() and ESIS_ParseBuffer()
   return ESIS_STATUS_ERROR with error code ESIS_ERROR_ABORTED.

   *Note*:
   This will be applied to the current parser instance only, that is, if
   there is a parent parser then it will continue parsing when the
   externalEntityRefHandler() returns. It is up to the implementation of
   the externalEntityRefHandler() to call ESIS_StopParser() on the parent
   parser (recursively), if one wants to stop parsing altogether.

   When suspended, parsing can be resumed by calling ESIS_ResumeParser(). 
*/

ESISPARSEAPI(enum ESIS_Status)
ESIS_StopParser(ESIS_Parser parser, ESIS_Bool resumable);

/* Resumes parsing after it has been suspended with ESIS_StopParser().
   Must not be called from within a handler call-back. Returns same
   status codes as ESIS_Parse() or ESIS_ParseBuffer().
   Additional error code ESIS_ERROR_NOT_SUSPENDED possible.   

   *Note*:
   This must be called on the most deeply nested child parser instance
   first, and on its parent parser only after the child parser has finished,
   to be applied recursively until the document entity's parser is restarted.
   That is, the parent parser will not resume by itself and it is up to the
   application to call ESIS_ResumeParser() on it at the appropriate moment.
*/
ESISPARSEAPI(enum ESIS_Status)
ESIS_ResumeParser(ESIS_Parser parser);


enum ESIS_Parsing {
  ESIS_INITIALIZED,
  ESIS_PARSING,
  ESIS_FINISHED,
  ESIS_SUSPENDED
};

typedef struct {
  enum ESIS_Parsing parsing;
  ESIS_Bool finalBuffer;
} ESIS_ParsingStatus;

/* Returns status of parser with respect to being initialized, parsing,
   finished, or suspended and processing the final buffer.
   XXX ESIS_Parse() and ESIS_ParseBuffer() should return ESIS_ParsingStatus,
   XXX with ESIS_FINISHED_OK or ESIS_FINISHED_ERROR replacing ESIS_FINISHED
*/
ESISPARSEAPI(void)
ESIS_GetParsingStatus(ESIS_Parser parser, ESIS_ParsingStatus *status);


/*
 * Creates an ESIS_Parser object that can parse a given FILE stream.
 *
 * Expat provides a XML_ExternalEntityParserCreate() function with
 * a similar purpose. To use the ESIS API for parsing an XML input
 * file (or entity, or buffer), the best contraption without too
 * much duplication would probably a function
 *
 *    ESIS_XmlParserCreate(XML_Parser xmlParser)
 *
 * so that the xmlParser can be created and initialized in any way
 * the application wants.
 */
 
ESISPARSEAPI(ESIS_Parser)
ESIS_FileParserCreate(FILE *fp,
                      const ESIS_Char *encoding);


/* If ESIS_Parse or ESIS_ParseBuffer have returned ESIS_STATUS_ERROR, then
   ESIS_GetErrorCode returns information about the error.
*/
ESISPARSEAPI(enum ESIS_Error)
ESIS_GetErrorCode(ESIS_Parser parser);

/*####################################################################*/

/*
 * Output support: writing ESIS, XML, and SGML files.
 *
 * :TODO: Separate header file "esisout.h" instead of "esisio.h" ?
 */

struct ESIS_WriterStruct;

typedef struct ESIS_WriterStruct *ESIS_Writer;

ESIS_Writer ESIS_WriterCreate(const ESIS_Char *encoding);
ESIS_Writer ESIS_XmlWriterCreate(const ESIS_Char *encoding);
ESIS_Writer ESIS_SgmlWriterCreate(const ESIS_Char *encoding);

/*
 * If the number of characters passed in a ESIS_Char * argument
 * is specified as ESIS_NTS, a NUL-terminated string is assumed,
 * and the length is determined by the called function.
 */

#define ESIS_NTS (~(size_t)0U)

void ESIS_Attr(ESIS_Writer, const ESIS_Char  *name,
                            const ESIS_Char  *val, size_t len);

void ESIS_Atts(ESIS_Writer, const ESIS_Char **atts);


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

void ESIS_Start(ESIS_Writer,     const ESIS_Char  *elemGI,
                                 const ESIS_Char **atts);
void ESIS_StartElem(ESIS_Writer, const ESIS_Elem  *elem);

void ESIS_Empty(ESIS_Writer,     const ESIS_Char  *elemGI,
                                 const ESIS_Char **atts);
void ESIS_EmptyElem(ESIS_Writer, const ESIS_Elem  *elem);

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

void ESIS_PCdata(ESIS_Writer,  const ESIS_Char *cd, size_t len);
void ESIS_Cdata(ESIS_Writer,   const ESIS_Char *cd, size_t len);


/*
 * The ESIS_End* functions output an end tag rsp a "`)`" line.
 */

void ESIS_End(ESIS_Writer,     const ESIS_Char *elemGI);
void ESIS_EndElem(ESIS_Writer, const ESIS_Elem *elem);

/*
 * Useful Expat functions to emulat:
 */
ESISPARSEAPI(void)
ESIS_SetUnknownEncodingHandler(ESIS_Parser parser,
                               ESIS_UnknownEncodingHandler handler,
                               void *encodingHandlerData);

/* This can be called within a handler for a start element, end
   element, processing instruction or character data.  It causes the
   corresponding markup to be passed to the default handler.
*/
ESISPARSEAPI(void)
ESIS_DefaultCurrent(ESIS_Parser parser);
 
 
 
/* This value is passed as the userData argument to callbacks. */
ESISPARSEAPI(void)
ESIS_SetUserData(ESIS_Parser parser, void *userData);

/* Returns the last value set by ESIS_SetUserData or NULL. */
#define ESIS_GetUserData(parser) (*(void **)(parser))

/* This is equivalent to supplying an encoding argument to
   ESIS_ParserCreate. On success ESIS_SetEncoding returns non-zero,
   zero otherwise.
   Note: Calling ESIS_SetEncoding after ESIS_Parse or ESIS_ParseBuffer
     has no effect and returns ESIS_STATUS_ERROR.
*/
ESISPARSEAPI(enum ESIS_Status)
ESIS_SetEncoding(ESIS_Parser parser, const ESIS_Char *encoding);/* If this function is called, then the parser will be passed as the
   first argument to callbacks instead of userData.  The userData will
   still be accessible using ESIS_GetUserData.
*/
ESISPARSEAPI(void)
ESIS_UseParserAsHandlerArg(ESIS_Parser parser);

/* Returns the number of the attribute/value pairs passed in last call
   to the ESIS_StartElementHandler that were specified in the start-tag
   rather than defaulted. Each attribute/value pair counts as 2; thus
   this correspondds to an index into the atts array passed to the
   ESIS_StartElementHandler.
*/
ESISPARSEAPI(int)
ESIS_GetSpecifiedAttributeCount(ESIS_Parser parser);

/* Returns the index of the ID attribute passed in the last call to
   ESIS_StartElementHandler, or -1 if there is no ID attribute.  Each
   attribute/value pair counts as 2; thus this correspondds to an
   index into the atts array passed to the ESIS_StartElementHandler.
*/
ESISPARSEAPI(int)
ESIS_GetIdAttributeIndex(ESIS_Parser parser);




#endif/*ESISIO_H_INCLUDED*/
