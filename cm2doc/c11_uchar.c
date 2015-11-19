/*
    c11_uchar.c -
*/

#define USE_UCHAR_STD_NAMES 0
#include "c11_uchar.h" 

#ifndef NDEBUG
#include "utf8.h" /* Compare with cmark utf8 lib functions - for now. */
#endif

#include <assert.h>
#include <errno.h> /* errno, EILSEQ */
#include <string.h> /* memchr() */


#define U(X)                    ((utf32_t)(0x ## X ##UL))
#define BMP(X)                  ((utf32_t)((X) & 0xFFFFUL))

#define UCS_REPLACEMENT         U(FFFD)


typedef unsigned c11_uchar_check_utf32_t[sizeof(utf32_t) == 4];
typedef unsigned c11_uchar_check_utf16_t[sizeof(utf16_t) == 2];

/*== Unicode state keeping ===========================================*/

/*
 * The tail elements are used for the "internal" state of 
 * `u8len()`, `u8toc32()` etc.
 */
#define U8LEN_INT_IDX   255
#define U8TOC32_INT_IDX 254

u8state_t     u8_state[256];
unsigned char u8_map[256];
u8state_t    *u8_state_new(void);

#define U8_STATE(MBS_P_) ( (u8_idx = *(unsigned char *)(MBS_P_)) == 0  	\
                             ? u8_state_new() : u8_state + (u8_idx-1U) )
                                     
u8state_t *u8_state_new(void)
{
    u8state_t     *u8sp;
    unsigned char *p = memchr(u8_map + 1, 0, sizeof u8_map);
    
    assert(p != NULL);
    u8sp = u8_state + (p - (u8_map + 1));
    return u8sp;
}

/*== UTF-8 -> UTF-32 =================================================*/

size_t u8len(const char *s, size_t n, mbstate_t *ps);
size_t u8toc32(utf32_t *pc32, const char *s, size_t n, mbstate_t *ps);

#define U8_TAIL(B)    ( ((B) &  0xC0U) == 0x80U )
#define U8_HEAD(B, L) (  (B) & (0xFFU  >> (L)) )
#define U8_LEN(B)     ( u8_tab[(((B) & 0xFFU) >> 3) & 0x1FU] )

static const size_t u8_tab[32] = {
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,   2, 2, 2, 2, 3, 3, 4, 5,
};

/*
    u8len - Emulate mbrlen (C95, <wchar.h>) for UTF-8.

        #include <wchar.h> // C95
        size_t mbrlen(const char *s, size_t n, mbstate_t *ps);

    Description

    The mbrlen function is equivalent to the call:

        mbrtowc(NULL, s, n, ps != NULL ? ps : &internal)

    where internal is the mbstate_t object for the mbrlen function,
    except that the expression designated by ps is evaluated only once.

    Returns

    The mbrlen function returns a value between zero and n, inclusive,
    (size_t)(-2), or (size_t)(-1).
 */


size_t u8len(const char *s, size_t n, mbstate_t *ps)
{
    mbstate_t int_st = { 0 };
    *(unsigned char*)&int_st = U8LEN_INT_IDX;
    
    return u8toc32(NULL, s, n, ps != NULL ? ps : &int_st);
}


/*
    u8toc32 - Emulate mbtoc32 (C11, <uchar.h>)

        #include <uchar.h> // C11
        size_t mbtoc32(char32_t *pc32, const char *s, size_t n, 
                       mbstate_t *ps);

    Description

    If `s` is a null pointer, the `mbrtoc32` function is equivalent to
    the call:

        mbrtoc32(NULL, "", 1, ps)

    In this case, the values of the parameters `pc32` and `n` are
    ignored.

    If `s` is not a null pointer, the `mbrtoc32` function inspects
    at most `n` bytes beginning with the byte pointed to by `s` to
    determine the number of bytes needed to complete the next multibyte
    character (including any shift sequences). If the function
    determines that the next multibyte character is complete and valid,
    it determines the values of the corresponding wide characters and
    then, if `pc32` is not a null pointer, stores the value of the
    first (or only) such character in the object pointed to by `pc32`.
    Subsequent calls will store successive wide characters without
    consuming any additional input until all the characters have been
    stored. If the corresponding wide character is the null wide
    character, the resulting state described is the initial conversion
    state.

    Returns

    The `mbrtoc32` function returns the first of the following that
    applies (given the current conversion state):

    0
        if the next `n` or fewer bytes complete the multibyte character
        that corresponds to the null wide character (which is the value
        stored).

    Between 1 and `n` inclusive   
        if the next `n` or fewer bytes complete a valid multibyte
        character (which is the value stored); the value returned is the
        number of bytes that complete the multibyte character.

    (size_t)(-3)
        if the next character resulting from a previous call has been
        stored (no bytes from the input have been consumed by this
        call).

    (size_t)(-2)
        if the next `n` bytes contribute to an incomplete (but
        potentially valid) multibyte character, and all `n` bytes have
        been processed (no value is stored). 325)

    (size_t)(-1)
        if an encoding error occurs, in which case the next `n` or
        fewer bytes do not contribute to a complete and valid multibyte
        character (no value is stored); the value of the macro `EILSEQ`
        is stored in `errno`, and the conversion state is unspecified.
 */

size_t u8toc32(utf32_t *pc32, const char *s, size_t n, mbstate_t *ps_)
{
    mbstate_t int_st = { 0 };
    int st;
    size_t u8_req, u8_use;
    unsigned char u8_idx;
    const char *p = s;
    u8state_t *ps;
    utf32_t c32;
    
#ifndef NDEBUG /* Compare our result with `cmark_utf8proc_iterate()` */
    utf32_t c32cmark;
#endif
    *(unsigned char*)&int_st = U8LEN_INT_IDX;
    
    ps = (ps_ != NULL) ? U8_STATE(ps_) : U8_STATE(&int_st);

    u8_idx = (unsigned)(ps - u8_state) + 1U;
    assert(u8_idx == (unsigned)(ps - u8_state) + 1U);
    

    /*
     * No octets provided? - we need as many as indicated in `*ps`.
     */
    if (s == NULL) return ps->req;
    
    /*
       Tail octets provided, but not expected?
       Or head octet provided, but tail octets required?
       
       In other words: exactly one of u8_req and ps->req must be
       zero.
     */
    if (((u8_req = U8_LEN(*s)) == 0) != (ps->req > 0U))
	goto ilseq;
	
    /*
       Because only one of the two `u8_req` and `ps->req` is non-zero,
       we get the number of actually required octets here: either
       `u8_req` or `ps->req`.
     */
    u8_req += ps->req;
    
    /*
       We can only use what is provided to us (`n`). If this is zero
       octets, we return the number of octets still required to complete
       the accumulated code point in `ps->acc`.
     */
    if ((u8_use = (u8_req > n) ? n : u8_req) == 0)
	return n;
    
    /*
     * At least one octet is provided, we get the *value* in it first.
     * This either starts a new code point (if `ps->acc` == 0U), or
     * adds 6 bits from a U8_TAIL byte to the accumulated code point
     * `ps->ucp`.
     */
    if (ps->acc == 0U)
        c32 = U8_HEAD(*p++, (u8_req > 1 ? u8_req + 1 : u8_req));
    else
        c32 = (ps->ucp << 6) | U8_TAIL(*p++);
    
    switch (u8_use) {
    case 5:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 4:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 3:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 2:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 1:
	break;
    case 0: ilseq:
	errno = EILSEQ;
	return (size_t)-1;
    }
    
    /*
     * If the code point is still incomplete, store it in the state,
     * return the indicator `(size_t)-2` for this situation.
     */
    if (u8_use < u8_req) {
        ps->ucp = c32;
        ps->acc += u8_use;
        ps->req  = u8_req - u8_use;
        (*(unsigned char*)ps_) = u8_idx;
        return (size_t)-2;
    }

    /*
     * We have a complete code point - reset the state ...
     */
    ps->ucp = U(0000);
    ps->acc = ps->req = 0U;
    
    if (c32 == U(0000)) {
	/* NUL decoded: Release the state, we're done. */
        u8_map[u8_idx] = 0;
        (*(unsigned char*)ps) = 0;
        if (pc32 != NULL) *pc32 = c32;
        return 0U;
    }

    /*
     * ... and check the code point.
     */
    if (U(D800) <= c32 && c32 < U(E000) || U(10FFFF) < c32) {
	errno = EILSEQ;
	return (size_t)-1;
    }

#if !defined(NDEBUG)
    /* Compare our result with that of _cmark_'s UTF-8 component. */
    st = cmark_utf8proc_iterate((uint8_t*)s, n, &c32cmark);
    assert(u8_use > 0U);
    assert(st == u8_use || st < 0);
    assert(c32cmark == c32 || st < 0);
#endif

    if (pc32 != NULL) *pc32 = c32;
    return u8_use;
}


size_t c32tou8(char *s, utf32_t c32, mbstate_t *ps_)
{
    mbstate_t int_st = { 0 };
    unsigned char u8_idx;
    char *p = s;
    u8state_t *ps;
    
    *(unsigned char*)&int_st = U8LEN_INT_IDX;
    
    ps = (ps_ != NULL) ? U8_STATE(ps_) : U8_STATE(&int_st);

    u8_idx = (unsigned)(ps - u8_state) + 1U;
    assert(u8_idx == (unsigned)(ps - u8_state) + 1U);
    
    if (s == NULL) {
	memset(ps, 0, sizeof *ps);
	return 0U;
    }

    if (U(D800) <= c32 && c32 < U(E000))
	return errno = EILSEQ, (size_t)-1; /* Surrogate, not a valid scalar. */
    if (U(10FFFF) < c32)
	return errno = EILSEQ, (size_t)-1; /* Out of range. */
	
    if (c32 <= U(007F))
	*p++ = (char)c32;
    else if (c32 <= U(07FF)) {
	unsigned oct_1 = ((c32 >>  6) & U(001F)) | U(00C0);
	unsigned oct_2 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
    } else if (c32 < U(FFFF)) {
	unsigned oct_1 = ((c32 >> 12) & U(000F)) | U(00E0);
	unsigned oct_2 = ((c32 >>  6) & U(003F)) | U(0080);
	unsigned oct_3 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
	*p++ = (char)oct_3;
    } else if (c32 < U(110000)) {
	unsigned oct_1 = ((c32 >> 18) & U(0007)) | U(00F0);
	unsigned oct_2 = ((c32 >> 12) & U(003F)) | U(0080);
	unsigned oct_3 = ((c32 >>  6) & U(003F)) | U(0080);
	unsigned oct_4 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
	*p++ = (char)oct_3;
	*p++ = (char)oct_4;
    } else
	return errno = EILSEQ, (size_t)-1;

    return (size_t)(p - s);
}
