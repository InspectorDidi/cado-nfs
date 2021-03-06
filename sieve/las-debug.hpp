#ifndef LAS_DEBUG_HPP_
#define LAS_DEBUG_HPP_

#include <limits.h>
#include <stdint.h>
#include <array>

#include "las-config.h"
#include "las-todo-entry.hpp"
#include "las-forwardtypes.hpp"
#include "cxx_mpz.hpp"

/* FIXME: This does not seem to work well */
#ifdef  __GNUC__
#define TYPE_MAYBE_UNUSED     __attribute__((unused));
#else
#define TYPE_MAYBE_UNUSED       /**/
#endif

extern void las_install_sighandlers();

/* {{{ where_am_I (debug) */
struct where_am_I {
#ifdef TRACK_CODE_PATH
    int logI;
    const qlattice_basis * pQ;
    struct side_data {
        const fb_factorbase::slicing * fbs;
    };
    side_data sides[2];
    fbprime_t p;        /* current prime or prime power, when applicable */
    fbroot_t r;         /* current root */
    slice_index_t i;    /* Slice index, if applicable */
    slice_offset_t h;   /* Prime hint, if not decoded yet */
    unsigned int j;     /* row number in bucket */
    unsigned int x;     /* value in bucket */
    unsigned int N;     /* bucket number */
    int side;
    const las_info * plas;
#endif  /* TRACK_CODE_PATH */
} /* TYPE_MAYBE_UNUSED */;

#ifdef TRACK_CODE_PATH
#define WHERE_AM_I_UPDATE(w, field, value) (w).field = (value)
#else
#define WHERE_AM_I_UPDATE(w, field, value) /**/
#endif
/* }}} */

struct trace_Nx_t { unsigned int N; unsigned int x; };
struct trace_ab_t { int64_t a; uint64_t b; };
struct trace_ij_t { int i; unsigned int j; };

extern void init_trace_k(cxx_param_list &);
extern void trace_per_sq_init(nfs_work const & ws);

/* When TRACE_K is defined, we are exposing some non trivial stuff.
 * Otherwise this all collapses to no-ops */

#ifdef TRACE_K

extern int test_divisible(where_am_I& w);
extern struct trace_Nx_t trace_Nx;
extern struct trace_ab_t trace_ab;
extern struct trace_ij_t trace_ij;

extern std::array<cxx_mpz, 2> traced_norms;

static inline int trace_on_spot_N(unsigned int N) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N;
}

static inline int trace_on_spot_Nx(unsigned int N, unsigned int x) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N && x == trace_Nx.x;
}

static inline int trace_on_range_Nx(unsigned int N, unsigned int x0, unsigned int x1) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N && x0 <= trace_Nx.x && trace_Nx.x < x1;
}

static inline int trace_on_spot_x(uint64_t x) {
    return x == (((uint64_t)trace_Nx.N) << LOG_BUCKET_REGION)
        + (uint64_t)trace_Nx.x;
}

static inline int trace_on_spot_ab(int64_t a, uint64_t b) {
    return a == trace_ab.a && b == trace_ab.b;
}

static inline int trace_on_spot_ij(int i, unsigned int j) {
    return i == trace_ij.i && j == trace_ij.j;
}

void sieve_increase_logging(unsigned char *S, const unsigned char logp, where_am_I& w);
void sieve_increase(unsigned char *S, const unsigned char logp, where_am_I& w);

#else
static inline int test_divisible(where_am_I& w MAYBE_UNUSED) { return 1; }
static inline int trace_on_spot_N(unsigned int N MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_Nx(unsigned int N MAYBE_UNUSED, unsigned int x MAYBE_UNUSED) { return 0; }
static inline int trace_on_range_Nx(unsigned int N MAYBE_UNUSED, unsigned int x0 MAYBE_UNUSED, unsigned int x1 MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_x(unsigned int x MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_ab(int64_t a MAYBE_UNUSED, uint64_t b MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_ij(int i MAYBE_UNUSED, unsigned int j MAYBE_UNUSED) { return 0; }

#ifdef CHECK_UNDERFLOW
void sieve_increase_underflow_trap(unsigned char *S, const unsigned char logp, where_am_I& w);
#endif  /* CHECK_UNDERFLOW */

static inline void sieve_increase(unsigned char *S, const unsigned char logp, where_am_I& w MAYBE_UNUSED)
{
#ifdef CHECK_UNDERFLOW
  if (*S > UCHAR_MAX - logp)
    sieve_increase_underflow_trap(S, logp, w);
#endif  /* CHECK_UNDERFLOW */
    *S += logp;
}

#endif  /* TRACE_K */

/* If -dumpregion is given, dump sieve region to a file to be able to
   compare new sieving code with a known good reference. Beware of
   resulting large files. */
class dumpfile_t {
    FILE *f = NULL;
public:
    ~dumpfile_t();
    void close();
    void open(const char *filename_stem, las_todo_entry const & doing, int side);
    size_t write(const unsigned char *, size_t) const;
};


#endif	/* LAS_DEBUG_HPP_ */
