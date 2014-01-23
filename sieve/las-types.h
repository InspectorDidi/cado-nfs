#ifndef LAS_TYPES_H_
#define LAS_TYPES_H_

#include <stdint.h>
#include "fb.h"
#include "trialdiv.h"
#include "las-config.h"
#include "cado_poly.h"
#include "ecm/facul.h"
#include "relation.h"

/* These must be forward-declared, because the header files below use
 * them */

struct sieve_info_s;
typedef struct sieve_info_s * sieve_info_ptr;
typedef const struct sieve_info_s * sieve_info_srcptr;
struct las_info_s;
typedef struct las_info_s * las_info_ptr;
typedef const struct las_info_s * las_info_srcptr;
struct where_am_I_s;
typedef struct where_am_I_s * where_am_I_ptr;
typedef const struct where_am_I_s * where_am_I_srcptr;

#include "las-unsieve.h"
#include "las-smallsieve.h"

/* {{{ siever_config */
/* The following structure lists the fields with an impact on the siever.
 * Different values for these fields will correspond to different siever
 * structures.
 */
struct siever_config_s {
    /* The bit size of the special-q. Counting in bits is no necessity,
     * we could imagine being more accurate */
    unsigned int bitsize;  /* bitsize == 0 indicates end of table */
    int side;
    int logI;
    struct {
        unsigned long lim; /* factor base bound */
        int lpb;           /* large prime bound is 2^lpbr */
        int mfb;           /* bound for residuals is 2^mfbr */
        double lambda;     /* lambda sieve parameter */
    } sides[2][1];
};
typedef struct siever_config_s siever_config[1];
typedef struct siever_config_s * siever_config_ptr;
typedef const struct siever_config_s * siever_config_srcptr;
/* }}} */

/* {{{ descent_hint
 *
 * This is used for the descent. For each factor size, we provide a
 * reasonable siever_config value
 *
 * We also provide, based on experience, info relative to how long it
 * takes to finish the smoothing process for a prime factor of this size.
 */
struct descent_hint_s {
    siever_config conf;
    double expected_time;
    double expected_success;
};
typedef struct descent_hint_s descent_hint[1];
typedef struct descent_hint_s * descent_hint_ptr;
typedef const struct descent_hint_s * descent_hint_srcptr;

/* }}} */

/* {{{ las_todo */
struct las_todo_s;
struct las_todo_s {
    mpz_t p;
    /* even when side == RATIONAL_SIDE, the field below is used, since
     * it is needed for the initialization of the q-lattice. All callers
     * of las_todo_push must therefore make sure that a proper argument
     * is provided.
     */
    mpz_t r;
    int side;
    int depth;  /* used for the descent only. The normal value is zero. */
    struct las_todo_s * next;
};
typedef struct las_todo_s las_todo[1];
typedef struct las_todo_s * las_todo_ptr;
typedef const struct las_todo_s * las_todo_srcptr;

void las_todo_push(las_todo_ptr * d, mpz_srcptr p, mpz_srcptr r, int side);
void las_todo_pop(las_todo_ptr * d);
/* }}} */

/* {{{ sieve_side_info */
struct sieve_side_info_s {
    unsigned char bound; /* A sieve array entry is a sieve survivor if it is
                            at most "bound" on each side */
    fbprime_t *trialdiv_primes;
    trialdiv_divisor_t *trialdiv_data;
    struct {
        factorbase_degn_t * pow2[2];
        factorbase_degn_t * pow3[2];
        factorbase_degn_t * td[2];
        factorbase_degn_t * rs[2];
        factorbase_degn_t * rest[2];
    } fb_parts[1];
    struct {
        int pow2[2];
        int pow3[2];
        int td[2];
        int rs[2];
        int rest[2];
    } fb_parts_x[1];
    /* The reading, mapping or generating the factor base all create the
     * factor base in several pieces: small primes, and large primes split
     * into one piece for each thread.
     */
    factorbase_degn_t * fb;
    factorbase_degn_t ** fb_bucket_threads;
    /* fb_is_mmapped is 1 if the factor memory is created by mmap(), and 0
       if it is created by malloc() */
    int fb_is_mmapped;
    /* log_steps[i] contains the largest integer x such that 
       fb_log(x, scale, 0.) == i, i.e., the integer after which the 
       rounded logarithm increases, i.o.w., x = floor(scale^(i+0.5)),
       for 0 <= i <= log_steps_max, where log_steps_max = fb_log(fbb, scale)
       For i > log_steps_max, log_steps[i] is undefined.
    */
    fbprime_t log_steps[256];
    unsigned char log_steps_max;
    /* When threads pick up this sieve_info structure, they should check
     * their bucket allocation */
    double max_bucket_fill_ratio;


    /* These fields are used for the norm initialization essentially.
     * Only the scale is also relevant to part of the rest, since it
     * determines the logp contributions for factor base primes */
    double scale;      /* scale used for logarithms for fb and norm */
    double cexp2[257]; /* for 2^X * scale + GUARD */
    double logmax;     /* norms on the alg-> side are < 2^alg->logmax */

    mpz_poly_t fij;   /* coefficients of F(a0*i+a1*j, b0*i+b1*j) (divided by 
                         q on the special-q side) */
    double *fijd;     /* coefficients of F_q (divided by q on the special q side) */

    /* This updated by applying the special-q lattice transform to the
     * factor base. */
    small_sieve_data_t ssd[1];
    /* And this is just created as an extraction of the above */
    small_sieve_data_t rsd[1];

    facul_strategy_t *strategy;
};
typedef struct sieve_side_info_s * sieve_side_info_ptr;
typedef const struct sieve_side_info_s * sieve_side_info_srcptr;
typedef struct sieve_side_info_s sieve_side_info[1];
/* }}} */

/* {{{ sieve_info
 *
 * General information about the siever, based on some input-dependent
 * configuration data with an impact on the output (as opposed to e.g.
 * file names, or verbosity flags, which do not affect the output).
 */
struct sieve_info_s {
    cado_poly_ptr cpoly; /* The polynomial pair */

    /* This conditions the validity of the sieve_info_side members
     * sides[0,1], as well as some other members */
    siever_config conf;

    las_todo doing;     /* not a todo list, but just one item => next==null */

    // sieving area
    uint32_t J;
    uint32_t I; // *conf has logI.

    // description of the q-lattice. The values here should remain
    // compatible with those in ->conf (this concerns notably the bit
    // size as well as the special-q side).
    int64_t a0, b0, a1, b1;

    // parameters for bucket sieving
    unsigned int td_thresh;
    int bucket_thresh;    // bucket sieve primes >= bucket_thresh
    uint32_t nb_buckets;

    sieve_side_info sides[2];

    /* Data for unsieving locations where gcd(i,j) > 1 */
    unsieve_aux_data us;
    /* Data for divisibility tests p|i in lines where p|j */
    j_div_ptr j_div;

    /* used in check_leftover_norm */
    mpz_t BB[2], BBB[2], BBBB[2];
};
typedef struct sieve_info_s sieve_info[1];

/* }}} */

/* {{{ las_info
 *
 * las_info holds general data, mostly unrelated to what is actually
 * computed within a sieve. las_info also contains outer data, which
 * lives outside the choice of one particular way to configure the siever
 * versus another.
 */
struct las_info_s {
    // general operational flags
    int nb_threads;
    FILE *output;
    const char * outputname; /* keep track of whether it's gzipped or not */
    int verbose;
    int bench;
    int suppress_duplicates;

    /* It's not ``general operational'', but global enough to be here */
    cado_poly cpoly;

    siever_config default_config;

    /* There may be several configured sievers. This is used mostly for
     * the descent.
     * TODO: For now these different sievers share nothing of their
     * factor bases, which is a shame. We should examine ways to get
     * around this limitation, but it is hard. One could imagine some,
     * though. Among them, given the fact that only _one_ siever is
     * active at a time, it might be possible to sort the factor base
     * again each time a new siever is used (but maybe it's too
     * expensive). Another way could be to work only on sharing
     * bucket-sieved primes.
     */
    sieve_info_ptr sievers;

    /* Descent lower steps */
    descent_hint * hint_table;
    unsigned int max_hint_bitsize[2];
    int * hint_lookups[2]; /* quick access indices into hint_table */

    /* This is an opaque pointer to C++ code. */
    void * descent_helper;

    las_todo_ptr todo;
    /* These are used for reading the todo list */
    mpz_t todo_q0;
    mpz_t todo_q1;
    FILE * todo_list_fd;
};

typedef struct las_info_s las_info[1];
/* }}} */

/* FIXME: This does not seem to work well */
#ifdef  __GNUC__
#define TYPE_MAYBE_UNUSED     __attribute__((unused));
#else
#define TYPE_MAYBE_UNUSED       /**/
#endif

/* {{{ where_am_I (debug) */
struct where_am_I_s {
#ifdef TRACK_CODE_PATH
    fbprime_t p;        /* current prime or prime power, when applicable */
    fbprime_t r;        /* current root */
    int fb_idx;         /* index into the factor base si->sides[side]->fb
                           or into th->sides[side]->fb_bucket */
    unsigned int j;     /* row number in bucket */
    unsigned int x;     /* value in bucket */
    unsigned int N;     /* bucket number */
    int side;
    las_info_srcptr las;
    sieve_info_srcptr si;
#endif  /* TRACK_CODE_PATH */
} TYPE_MAYBE_UNUSED;

typedef struct where_am_I_s where_am_I[1];


#ifdef TRACK_CODE_PATH
#define WHERE_AM_I_UPDATE(w, field, value) (w)->field = (value)
#else
#define WHERE_AM_I_UPDATE(w, field, value) /**/
#endif
/* }}} */

#endif	/* LAS_TYPES_H_ */
