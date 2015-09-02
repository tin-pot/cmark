/*
 * stdbool.h
 * 
 * Define bool, true, false, __bool_true_false_are_defined
 * according to ISO 9899:1999.
 */

/*
 * The _Bool type must:
 * - promote to int,
 * - have 1 byte size (compatibility to C++),
 * - allow positive 1 in 1-bit bitfields.
 */

#ifndef _STDBOOL_INCLUDED
#define _STDBOOL_INCLUDED

#if defined(bool)
#define HAS_BOOL_TYPE
#elif defined(__cplusplus) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 6))
#define HAS_BOOL_TYPE
#elif defined(__cplusplus) && _MSC_VER >= 1100 || __BORLANDC__ >= 0x500
#define HAS_BOOL_TYPE
#elif defined(_CC_COMEAU_)
#define HAS_BOOL_TYPE
#elif defined(sgi) && ( (_COMPILER_VERSION >= 710) || defined(_BOOL) )
#define HAS_BOOL_TYPE
#elif defined(__DECCXX) && (__DECCXX_VER >= 60060005)
#define HAS_BOOL_TYPE
#endif

#if !defined(HAS_BOOL_TYPE)
typedef unsigned char _Bool;
#define bool _Bool
#define false 0
#define true 1
#define __bool_true_false_are_defined 1
#endif

#endif /* _STDBOOL_INCLUDED */
