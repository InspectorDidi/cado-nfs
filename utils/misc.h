#ifndef CADO_UTILS_MISC_H_
#define CADO_UTILS_MISC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <gmp.h>
#include "macros.h"
#include "portability.h"

/* we require GMP 5 at least */
#if !GMP_VERSION_ATLEAST(5,0,0)
#error "GNU MP 5 (at least) is required to compile CADO-NFS"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define UMAX(A) (0xffffffffffffffffULL >>((8-sizeof(A))<<3))
#define SMAX(A) (0x7fffffffffffffffLL  >>((8-sizeof(A))<<3))
#define UMIN(A) (0)
#define SMIN(A) (1ULL<< ((sizeof(A)<<3)-1))

/* uintmax_t is guaranteed to be larger or equal to uint64_t */
#define strtouint64(nptr,endptr,base) (uint64_t) strtoumax(nptr,endptr,base)

static inline void* pointer_arith(void * a, ptrdiff_t q) {
    return (void*)(((char*)a)+q);
}
static inline const void* pointer_arith_const(const void * a, ptrdiff_t q) {
    return (const void*)(((const char*)a)+q);
}

/* MinGW's string.h does not declare a prototype for strdup if __STRICT_ANSI__
   is defined */
#if !defined(HAVE_STRDUP) || (defined(__MINGW32__) && defined(__STRICT_ANSI__))
char * strdup(const char *s);
#endif
#ifndef HAVE_STRNDUP
char * strndup(const char * a, size_t n);
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size) ATTRIBUTE((__warn_unused_result__));
#endif
#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size) ATTRIBUTE((__warn_unused_result__));
#endif
void strlcat_check(char *dst, const char *src, size_t size);

extern char * derived_filename(const char * prefix, const char * what, const char * ext);
extern int has_suffix(const char * path, const char * sfx);

extern char ** filelist_from_file(const char * basepath, const char * filename,
                                  int typ);
extern void filelist_clear(char ** filelist);

extern void * malloc_check(const size_t x);
extern void * physical_malloc(const size_t x, const int affect);

extern long pagesize (void);
long get_arg_max(void);
extern void * malloc_aligned(size_t size, size_t alignment);
extern void free_aligned(void * ptr, size_t size, size_t alignment);

extern void * malloc_pagealigned(size_t sz);
extern void free_pagealigned(void * ptr, size_t sz);

extern int mkdir_with_parents(const char * dir, int fatal);

extern char * path_resolve(const char * progname, char * resolved);

/* k must be a power of 2. Returns the smallest multiple of k greater
 * than or equal to n
 */
static inline unsigned long
next_multiple_of_powerof2(unsigned long n, unsigned long k)
{
    ASSERT((k & (k-1)) == 0);
    return ((n-1)|(k-1)) + 1;
}

/* those builtins seem to have appeared in 3.4 (April 2004) */
#ifndef HAVE_clzl
#if GNUC_VERSION_ATLEAST(3,4,0)
#define clzll(x)        __builtin_clzll(x)
#define clzl(x)         __builtin_clzl(x)
#define clz(x)          __builtin_clz(x)
#define HAVE_clzl
#else
/* provide slow fallbacks */
static inline int clzll(unsigned long long x)
{
        /* We assume exactly 64 bits in a long long for now */
        static const int t[4] = { 2, 1, 0, 0 };
        int a = 0;
        int res;
        if (x >> 32) { a += 32; x >>= 32; }
        if (x >> 16) { a += 16; x >>= 16; }
        if (x >>  8) { a +=  8; x >>=  8; }
        if (x >>  4) { a +=  4; x >>=  4; }
        if (x >>  2) { a +=  2; x >>=  2; }
        res = 64 - 2 - a + t[x];
        return res;
}

static inline int clzl(unsigned long x)
{
        static const int t[4] = { 2, 1, 0, 0 };
        int a = 0;
        int res;
#if (GMP_LIMB_BITS == 64)
        if (x >> 32) { a += 32; x >>= 32; }
#endif
        if (x >> 16) { a += 16; x >>= 16; }
        if (x >>  8) { a +=  8; x >>=  8; }
        if (x >>  4) { a +=  4; x >>=  4; }
        if (x >>  2) { a +=  2; x >>=  2; }
        res = GMP_LIMB_BITS - 2 - a + t[x];
        return res;
}

static inline int clz(unsigned int x)
{
        static const int t[4] = { 2, 1, 0, 0 };
        int a = 0;
        int res;
        if (x >> 16) { a += 16; x >>= 16; }
        if (x >>  8) { a +=  8; x >>=  8; }
        if (x >>  4) { a +=  4; x >>=  4; }
        if (x >>  2) { a +=  2; x >>=  2; }
        res = 30 - a + t[x];
        return res;
}
#define HAVE_clzl
#define HAVE_clzl_fallback
#endif
#endif  /* HAVE_clzl */

/* Best X86 medium memcpy with pointers & size length already cache
   lines aligned on modern X86, so dst/src/lg & 0x3F = 0 */
static inline void aligned_medium_memcpy(void *dst, void *src, size_t lg) {
#ifdef __x86_64
  size_t lg_8bytes = lg >> 3;
  __asm__ __volatile__ ("cld\nrep movsq\n":"+D"(dst),"+S"(src),"+c"(lg_8bytes));
#else
  memcpy(dst,src,lg);
#endif
}

#ifndef HAVE_ctzl
#if GNUC_VERSION_ATLEAST(3,4,0)
#define ctzll(x)        __builtin_ctzll(x)
#define ctzl(x)         __builtin_ctzl(x)
#define ctz(x)          __builtin_ctz(x)
#define HAVE_ctzl
#else
/* the following code is correct because if x = 0...0abc10...0, then
   -x = ~x + 1, where ~x = 1...1(1-a)(1-b)(1-c)01...1, thus
   -x = 1...1(1-a)(1-b)(1-c)10...0, and x & (-x) = 0...000010...0 */
static inline int ctzll(unsigned long long x)
{
  return (64 - 1) - clzll(x & - x);
}
static inline int ctzl(unsigned long x)
{
  ASSERT(GMP_LIMB_BITS == sizeof(unsigned long) * CHAR_BIT);
  return (GMP_LIMB_BITS - 1) - clzl(x & - x);
}
static inline int ctz(unsigned int x)
{
  ASSERT(GMP_LIMB_BITS == sizeof(unsigned int) * CHAR_BIT);
  return (GMP_LIMB_BITS - 1) - clzl(x & - x);
}
#define HAVE_ctzl
#define HAVE_ctzl_fallback
#endif
#endif  /* HAVE_ctzl */


#ifdef __cplusplus
}
#endif

#if 0
#ifdef __cplusplus
// declare c++ containers as vector<T,pagealigned_allocator<T>>
template < typename _Tp > class pagealigned_allocator {
      public:
        typedef size_t size_type;
        typedef ptrdiff_t difference_type;
        typedef _Tp *pointer;
        typedef const _Tp *const_pointer;
        typedef _Tp & reference;
        typedef const _Tp & const_reference;
        typedef _Tp value_type;

        template < typename _Tp1 > struct rebind {
                typedef pagealigned_allocator < _Tp1 > other;
        };

        pagealigned_allocator()throw() { }
        pagealigned_allocator(const pagealigned_allocator &) throw() { }
        ~pagealigned_allocator()throw() { }

        template < typename _Tp1 >
        pagealigned_allocator(const pagealigned_allocator < _Tp1 > &) throw() { }

        pointer address(reference __x) const { return &__x; }
        const_pointer address(const_reference __x) const { return &__x; }
        // NB: __n is permitted to be 0.  The C++ standard says nothing
        // about what the return value is when __n == 0.
        pointer allocate(size_type __n, const void * = 0) {
                return static_cast <
                    _Tp * >(malloc_pagealigned(__n * sizeof(_Tp)));
        }

        // __p is not permitted to be a null pointer.
        void deallocate(pointer __p, size_type __n) {
                free_pagealigned(__p, __n * sizeof(_Tp));
        }

        size_type max_size()const throw() {
                return size_t(-1) / sizeof(_Tp);
        }

        void construct(pointer __p, const _Tp & __val) {
                ::new(__p) _Tp(__val);
        }

        void destroy(pointer __p) { __p->~_Tp(); }
};
#endif
#endif

#endif	/* CADO_UTILS_MISC_H_ */
