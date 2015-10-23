/* esisio_external.h */
#ifndef ESISIO_EXTERNAL_H_INCLUDED
#define ESISIO_EXTERNAL_H_INCLUDED

/*
   This file is derived from "expat_external.h", part of the Expat
   XML library:
   
   Copyright (c) 1998, 1999, 2000 Thai Open Source Software Center Ltd
   See the file COPYING for copying permission.
   
   for the use in the ESIS input/output library.
*/


/* External API definitions */

#if defined(_MSC_EXTENSIONS) && !defined(__BEOS__) && !defined(__CYGWIN__)
#define ESIS_USE_MSC_EXTENSIONS 1
#endif

/*
   Expat (and now ESISio too ;-) tries very hard to make the API
   boundary very specifically defined. There are two macros defined to
   control this boundary; each of these can be defined before including
   this header to achieve some different behavior, but doing so it not
   recommended or tested frequently.

   ESISCALL    - The calling convention to use for all calls across the
                "library boundary."  This will default to cdecl, and
                try really hard to tell the compiler that's what we
                want.

   ESISIMPORT  - Whatever magic is needed to note that a function is
                to be imported from a dynamically loaded library
                (.dll, .so, or .sl, depending on your platform).

   The XMLCALL macro was added in Expat 1.95.7.  The only one which is
   expected to be directly useful in client code is XMLCALL.

   Note that on at least some Unix versions, the Expat library must be
   compiled with the cdecl calling convention as the default since
   system headers may assume the cdecl convention.
*/
#ifndef ESISCALL
#if defined(_MSC_VER)
#define ESISCALL __cdecl
#elif defined(__GNUC__) && defined(__i386) && !defined(__INTEL_COMPILER)
#define ESISCALL __attribute__((cdecl))
#else
/* For any platform which uses this definition and supports more than
   one calling convention, we need to extend this definition to
   declare the convention used on that platform, if it's possible to
   do so.

   If this is the case for your platform, please file a bug report
   with information on how to identify your platform via the C
   pre-processor and how to specify the same calling convention as the
   platform's malloc() implementation.
*/
#define ESISCALL
#endif
#endif  /* not defined ESISCALL */


#if !defined(ESIS_STATIC) && !defined(ESISIMPORT)
#ifndef ESIS_BUILDING_APP
/* using Expat from an application */

#ifdef ESIS_USE_MSC_EXTENSIONS
#define ESISIMPORT __declspec(dllimport)
#endif

#endif
#endif  /* not defined ESIS_STATIC */


/* If we didn't define it above, define it away: */
#ifndef ESISIMPORT
#define ESISIMPORT
#endif


#define ESISPARSEAPI(type) ESISIMPORT type ESISCALL
#define ESISWRITEAPI(type) ESISIMPORT type ESISCALL

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESIS_UNICODE_WCHAR_T
#define ESIS_UNICODE
#endif

#ifdef ESIS_UNICODE     /* Information is UTF-16 encoded. */
#ifdef ESIS_UNICODE_WCHAR_T
typedef wchar_t ESIS_Char;
typedef wchar_t ESIS_LChar;
#else
typedef unsigned short ESIS_Char;
typedef char ESIS_LChar;
#endif /* ESIS_UNICODE_WCHAR_T */
#else                  /* Information is UTF-8 encoded. */
typedef char ESIS_Char;
typedef char ESIS_LChar;
#endif /* ESIS_UNICODE */

#ifdef ESIS_LARGE_SIZE  /* Use large integers for file/stream positions. */
#if defined(ESIS_USE_MSC_EXTENSIONS) && _MSC_VER < 1400
typedef __int64 ESIS_Index; 
typedef unsigned __int64 ESIS_Size;
#else
typedef long long ESIS_Index;
typedef unsigned long long ESIS_Size;
#endif
#else
typedef long ESIS_Index;
typedef unsigned long ESIS_Size;
#endif /* ESIS_LARGE_SIZE */

#ifdef __cplusplus
}
#endif

#endif/*ESISIO_EXTERNAL_H_INCLUDED*/
