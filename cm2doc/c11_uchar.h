/* c11_uchar.h */

#ifndef C11_UCHAR_H_INCLUDED
#define C11_UCHAR_H_INCLUDED

#include <stddef.h> /* size_t */
#include <wchar.h> /* mbstate_t */

typedef unsigned long  utf32_t;
typedef unsigned short utf16_t;

typedef struct u8state_ { 
    utf32_t  ucp;	    /* Accum. code point so far. */
    unsigned acc, req;      /* Number octets accumulated, number of
                               octets required to complete UCS char. */
} u8state_t;

size_t u8len(const char *, size_t, mbstate_t *);
size_t u8toc32(utf32_t *, const char *, size_t, mbstate_t *);

#if USE_UCHAR_STD_NAMES

#define char32_t utf32_t
#define char16_t utf16_t

#define mbtoc32(PC_, S_, N_, PS_)      u8toc32((PC_), (S_), (N_), (PS_))
#define mbrlen(S_, N_, PS_)            u8len((S_), (N_), (PS_))

#endif /* USE_UCHAR_STD_NAMES */

#endif/*C11_UCHAR_H_INCLUDED*/
