#include "cado.h"
#include "lingen_qcode_prime.hpp"
#include "bw-common.h"
#include "lingen.hpp"
#include "cxx_mpz.hpp"

/* This destructively cancels the first len coefficients of E, and
 * computes the appropriate matrix pi which achieves this. The
 * elimination is done in accordance with the nominal degrees found in
 * delta.
 *
 * The result is expected to have degree ceil(len*m/b) coefficients, so
 * that E*pi is divisible by X^len.
 */

/*{{{ basecase */

/* {{{ col sorting */
/* We sort only with respect to the global delta[] parameter. As it turns
 * out, we also access the column index in the same aray and sort with
 * respect to it, but this is only cosmetic.
 *
 * Note that unlike what is asserted in other coiped of the code, sorting
 * w.r.t. the local delta[] value is completely useless. Code which
 * relies on this should be fixed.
 */

typedef int (*sortfunc_t) (const void*, const void*);

static int lexcmp2(const int x[2], const int y[2])
{
    for(int i = 0 ; i < 2 ; i++) {
        int d = x[i] - y[i];
        if (d) return d;
    }
    return 0;
}

/* }}} */

std::tuple<unsigned int, unsigned int> get_minmax_delta(std::vector<unsigned int> const & delta)/*{{{*/
{
    unsigned int maxdelta = 0;
    unsigned int mindelta = UINT_MAX;
    for(auto x : delta) {
        if (x > maxdelta) maxdelta = x;
        if (x < mindelta) mindelta = x;
    }
    return std::make_tuple(mindelta, maxdelta);
}/*}}}*/
unsigned int get_min_delta(std::vector<unsigned int> const & delta)/*{{{*/
{
    unsigned int mindelta, maxdelta;
    std::tie(mindelta, maxdelta) = get_minmax_delta(delta);
    return mindelta;
}/*}}}*/
unsigned int get_max_delta(std::vector<unsigned int> const & delta)/*{{{*/
{
    unsigned int mindelta, maxdelta;
    std::tie(mindelta, maxdelta) = get_minmax_delta(delta);
    return maxdelta;
}/*}}}*/
std::tuple<unsigned int, unsigned int> get_minmax_delta_on_solutions(bmstatus & bm, std::vector<unsigned int> const & delta)/*{{{*/
{
    unsigned int maxdelta = 0;
    unsigned int mindelta = UINT_MAX;
    for(unsigned int j = 0 ; j < bm.d.m + bm.d.n ; j++) {
        if (bm.lucky[j] <= 0) continue;
        if (delta[j] > maxdelta) maxdelta = delta[j];
        if (delta[j] < mindelta) mindelta = delta[j];
    }
    return std::make_tuple(mindelta, maxdelta);
}/*}}}*/
unsigned int get_max_delta_on_solutions(bmstatus & bm, std::vector<unsigned int> const & delta)/*{{{*/
{
    unsigned int mindelta, maxdelta;
    std::tie(mindelta, maxdelta) = get_minmax_delta_on_solutions(bm, delta);
    return maxdelta;
}/*}}}*/



unsigned int expected_pi_length(bw_dimensions & d, unsigned int len)/*{{{*/
{
    /* The idea is that we want something which may account for something
     * exceptional, bounded by probability 2^-64. This corresponds to a
     * column in e (matrix of size m*b) to be spontaneously equal to
     * zero. This happens with probability (#K)^-m.
     * The solution to
     * (#K)^(-m*x) > 2^-64
     * is m*x*log_2(#K) < 64
     *
     * We thus need to get an idea of the value of log_2(#K).
     *
     * (Note that we know that #K^abgroupsize(ab) < 2^64, but that bound
     * might be very gross).
     *
     * The same esitmate can be used to appreciate what we mean by
     * ``luck'' in the end. If a column happens to be zero more than
     * expected_pi_length(d,0) times in a row, then the cause must be
     * more than sheer luck, and we use it to detect generating rows.
     */

    unsigned int m = d.m;
    unsigned int n = d.n;
    unsigned int b = m + n;
    abdst_field ab MAYBE_UNUSED = d.ab;
    unsigned int res = 1 + iceildiv(len * m, b);
    cxx_mpz p;
    abfield_characteristic(ab, (mpz_ptr) p);
    unsigned int l;
    if (mpz_cmp_ui(p, 1024) >= 0) {
        l = mpz_sizeinbase(p, 2);
        l *= abfield_degree(ab);    /* roughly log_2(#K) */
    } else {
        mpz_pow_ui(p, p, abfield_degree(ab));
        l = mpz_sizeinbase(p, 2);
    }
    // unsigned int safety = iceildiv(abgroupsize(ab), m * sizeof(abelt));
    unsigned int safety = iceildiv(64, m * l);
    return res + safety;
}/*}}}*/

unsigned int expected_pi_length(bw_dimensions & d, std::vector<unsigned int> const & delta, unsigned int len)/*{{{*/
{
    // see comment above.

    unsigned int mi, ma;
    std::tie(mi, ma) = get_minmax_delta(delta);

    return expected_pi_length(d, len) + ma - mi;
}/*}}}*/

unsigned int expected_pi_length_lowerbound(bw_dimensions & d, unsigned int len)/*{{{*/
{
    /* generically we expect that len*m % (m+n) columns have length
     * 1+\lfloor(len*m/(m+n))\rfloor, and the others have length one more.
     * For one column to have a length less than \lfloor(len*m/(m+n))\rfloor,
     * it takes probability 2^-(m*l) using the notations above. Therefore
     * we can simply count 2^(64-m*l) accidental zero cancellations at
     * most below the bound.
     * In particular, it is sufficient to derive from the code above!
     */
    unsigned int m = d.m;
    unsigned int n = d.n;
    unsigned int b = m + n;
    abdst_field ab MAYBE_UNUSED = d.ab;
    unsigned int res = 1 + (len * m) / b;
    mpz_t p;
    mpz_init(p);
    abfield_characteristic(ab, p);
    unsigned int l;
    if (mpz_cmp_ui(p, 1024) >= 0) {
        l = mpz_sizeinbase(p, 2);
        l *= abfield_degree(ab);    /* roughly log_2(#K) */
    } else {
        mpz_pow_ui(p, p, abfield_degree(ab));
        l = mpz_sizeinbase(p, 2);
    }
    mpz_clear(p);
    unsigned int safety = iceildiv(64, m * l);
    return safety < res ? res - safety : 0;
}/*}}}*/

/*}}}*/

int
bw_lingen_basecase_raw(bmstatus & bm, matpoly & pi, matpoly const & E, std::vector<unsigned int> & delta) /*{{{*/
{
    int generator_found = 0;

    bw_dimensions & d = bm.d;
    unsigned int m = d.m;
    unsigned int n = d.n;
    unsigned int b = m + n;
    abdst_field ab = d.ab;
    ASSERT(E.m == m);
    ASSERT(E.n == b);

    ASSERT(pi.m == 0);
    ASSERT(pi.n == 0);
    ASSERT(pi.alloc == 0);

    /* Allocate something large enough for the result. This will be
     * soon freed anyway. Set it to identity. */
    unsigned int mi, ma;
    std::tie(mi, ma) = get_minmax_delta(delta);

    unsigned int pi_room_base = expected_pi_length(d, delta, E.size);

    pi = matpoly(ab, b, b, pi_room_base);
    pi.size = pi_room_base;

    /* Also keep track of the
     * number of coefficients for the columns of pi. Set pi to Id */

    std::vector<unsigned int> pi_lengths(b, 1);
    std::vector<unsigned int> pi_real_lengths(b, 1);
    pi.set_constant_ui(1);

    for(unsigned int i = 0 ; i < b ; i++) {
        pi_lengths[i] = 1;
        /* Fix for check 21744-bis. Our column priority will allow adding
         * to a row if its delta is above the average. But then this
         * might surprise us, because in truth we dont start with
         * something of large degree here. So we're bumping pi_length a
         * little bit to accomodate for this situation.
         */
        pi_lengths[i] += delta[i] - mi;
    }

    /* Keep a list of columns which have been used as pivots at the
     * previous iteration */
    std::vector<unsigned int> pivots(m, 0);
    std::vector<int> is_pivot(b, 0);

    matpoly e(ab, m, b, 1);
    e.size = 1;

    matpoly T(ab, b, b, 1);
    int (*ctable)[2] = new int[b][2];

    for (unsigned int t = 0; t < E.size ; t++, bm.t++) {

        /* {{{ Update the columns of e for degree t. Save computation
         * time by not recomputing those which can easily be derived from
         * previous iteration. Notice that the columns of e are exactly
         * at the physical positions of the corresponding columns of pi.
         */

        std::vector<unsigned int> todo;
        for(unsigned int j = 0 ; j < b ; j++) {
            if (is_pivot[j]) continue;
            /* We should never have to recompute from pi using discarded
             * columns. Discarded columns should always correspond to
             * pivots */
            ASSERT_ALWAYS(bm.lucky[j] >= 0);
            todo.push_back(j);
        }
        /* icc openmp doesn't grok todo.size() as being a constant
         * loop bound */
        unsigned int nj = todo.size();

#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
        {
            abelt_ur tmp_ur;
            abelt_ur_init(ab, &tmp_ur);

            abelt_ur e_ur;
            abelt_ur_init(ab, &e_ur);
            
#ifdef HAVE_OPENMP
#pragma omp for collapse(2)
#endif
            for(unsigned int jl = 0 ; jl < nj ; ++jl) {
                for(unsigned int i = 0 ; i < m ; ++i) {
                    unsigned int j = todo[jl];
                    unsigned int lj = MIN(pi_real_lengths[j], t + 1);
                    abelt_ur_set_zero(ab, e_ur);
                    for(unsigned int k = 0 ; k < b ; ++k) {
                        for(unsigned int s = 0 ; s < lj ; s++) {
                            abmul_ur(ab, tmp_ur,
                                    E.coeff(i, k, t - s),
                                    pi.coeff(k, j, s));
                            abelt_ur_add(ab, e_ur, e_ur, tmp_ur);
                        }
                    }
                    abreduce(ab, e.coeff(i, j, 0), e_ur);
                }
            }

            abelt_ur_clear(ab, &tmp_ur);
            abelt_ur_clear(ab, &e_ur);
        }
        /* }}} */

        /* {{{ check for cancellations */

        unsigned int newluck = 0;
        for(unsigned int jl = 0 ; jl < todo.size() ; ++jl) {
            unsigned int j = todo[jl];
            unsigned int nz = 0;
            for(unsigned int i = 0 ; i < m ; i++) {
                nz += abcmp_ui(ab, e.coeff(i, j, 0), 0) == 0;
            }
            if (nz == m) {
                newluck++, bm.lucky[j]++;
            } else if (bm.lucky[j] > 0) {
                bm.lucky[j] = 0;
            }
        }


        if (newluck) {
            /* If newluck == n, then we probably have a generator. We add an
             * extra guarantee. newluck==n, for a total of k iterations in a
             * row, means m*n*k coefficients cancelling magically. We would
             * like this to be impossible by mere chance. Thus we want n*k >
             * luck_mini, which can easily be checked */

            int luck_mini = expected_pi_length(d);
            unsigned int luck_sure = 0;

            printf("t=%d, canceled columns:", bm.t);
            for(unsigned int j = 0 ; j < b ; j++) {
                if (bm.lucky[j] > 0) {
                    printf(" %u", j);
                    luck_sure += bm.lucky[j] >= luck_mini;
                }
            }

            if (newluck == n && luck_sure == n) {
                if (!generator_found) {
                    printf(", complete generator found, for sure");
                }
                generator_found = 1;
            }
            printf(".\n");
        }
        /* }}} */

        if (generator_found) break;

        /* {{{ Now see in which order I may look at the columns of pi, so
         * as to keep the nominal degrees correct. In contrast with what
         * we used to do before, we no longer apply the permutation to
         * delta. So the delta[] array keeps referring to physical
         * indices, and we'll tune this in the end. */
        for(unsigned int j = 0; j < b; j++) {
            ctable[j][0] = delta[j];
            ctable[j][1] = j;
        }
        qsort(ctable, b, 2 * sizeof(int), (sortfunc_t) & lexcmp2);
        /* }}} */

        /* {{{ Now do Gaussian elimination */

        /*
         * The matrix T is *not* used for actually storing the product of
         * the transvections, just the *list* of transvections. Then,
         * instead of applying them row-major, we apply them column-major
         * (abiding by the ordering of pivots), so that we get a better
         * opportunity to do lazy reductions.
         */

        T.set_constant_ui(1);

        is_pivot.assign(b, 0);
        unsigned int r = 0;

        std::vector<unsigned int> pivot_columns;
        /* Loop through logical indices */
        for(unsigned int jl = 0; jl < b; jl++) {
            unsigned int j = ctable[jl][1];
            unsigned int u = 0;
            /* {{{ Find the pivot */
            for( ; u < m ; u++) {
                if (abcmp_ui(ab, e.coeff(u, j, 0), 0) != 0)
                    break;
            }
            if (u == m) continue;
            assert(r < m);
            /* }}} */
            pivots[r++] = j;
            is_pivot[j] = 1;
            pivot_columns.push_back(j);
            /* {{{ Cancel this coeff in all other columns. */
            abelt inv;
            abinit(ab, &inv);
            int rc = abinv(ab, inv, e.coeff(u, j, 0));
            if (!rc) {
                fprintf(stderr, "Error, found a factor of the modulus: ");
                abfprint(ab, stderr, inv);
                fprintf(stderr, "\n");
                exit(EXIT_FAILURE);
            }
            abneg(ab, inv, inv);
            for (unsigned int kl = jl + 1; kl < b ; kl++) {
                unsigned int k = ctable[kl][1];
                if (abcmp_ui(ab, e.coeff(u, k, 0), 0) == 0)
                    continue;
                // add lambda = e[u,k]*-e[u,j]^-1 times col j to col k.
                abelt lambda;
                abinit(ab, &lambda);
                abmul(ab, lambda, inv, e.coeff(u, k, 0));
                assert(delta[j] <= delta[k]);
                /* {{{ Apply on both e and pi */
                abelt tmp;
                abinit(ab, &tmp);
                for(unsigned int i = 0 ; i < m ; i++) {
                    /* TODO: Would be better if mpfq had an addmul */
                    abmul(ab, tmp, lambda, e.coeff(i, j, 0));
                    abadd(ab,
                            e.coeff(i, k, 0),
                            e.coeff(i, k, 0),
                            tmp);
                }
                if (bm.lucky[k] < 0) {
                    /* This column is already discarded, don't bother */
                    continue;
                }
                if (bm.lucky[j] < 0) {
                    /* This column is discarded. This is going to
                     * invalidate another column of pi. Not a problem,
                     * unless it's been marked as lucky previously ! */
                    ASSERT_ALWAYS(bm.lucky[k] <= 0);
                    printf("Column %u discarded from now on (through addition from column %u)\n", k, j);
                    bm.lucky[k] = -1;
                    continue;
                }
                /* We do *NOT* really update T. T is only used as
                 * storage!
                 */
                abset(ab, T.coeff(j, k, 0), lambda);
                abclear(ab, &tmp);
                /* }}} */
                abclear(ab, &lambda);
            }
            abclear(ab, &inv); /* }}} */
        }
        /* }}} */

        /* {{{ apply the transformations, using the transvection
         * reordering trick */

        /* non-pivot columns are only added to and never read, so it does
         * not really matter where we put their computation, provided
         * that the columns that we do read are done at this point.
         */
        for(unsigned int jl = 0; jl < b; jl++) {
            unsigned int j = ctable[jl][1];
            if (!is_pivot[j])
                pivot_columns.push_back(j);
        }

#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
        {
            abelt_ur tmp_pi;
            abelt_ur tmp;
            abelt_ur_init(ab, &tmp);
            abelt_ur_init(ab, &tmp_pi);

            for(unsigned int jl = 0 ; jl < b ; ++jl) {
                unsigned int j = pivot_columns[jl];
                /* compute column j completely. We may put this interface in
                 * matpoly, but it's really special-purposed, to the point
                 * that it really makes little sense IMO
                 *
                 * Beware: operations on the different columns are *not*
                 * independent, here ! Operations on the different degrees,
                 * on the other hand, are. As well of course as the
                 * operations on the different entries in each column.
                 */

#ifndef NDEBUG
#ifdef HAVE_OPENMP
#pragma omp critical
#endif
                {
                    for(unsigned int kl = m ; kl < b ; kl++) {
                        unsigned int k = pivot_columns[kl];
                        absrc_elt Tkj = T.coeff(k, j, 0);
                        ASSERT_ALWAYS(abcmp_ui(ab, Tkj, k==j) == 0);
                    }
                    for(unsigned int kl = 0 ; kl < MIN(m,jl) ; kl++) {
                        unsigned int k = pivot_columns[kl];
                        absrc_elt Tkj = T.coeff(k, j, 0);
                        if (abcmp_ui(ab, Tkj, 0) == 0) continue;
                        ASSERT_ALWAYS(pi_lengths[k] <= pi_lengths[j]);
                        pi_real_lengths[j] = std::max(pi_real_lengths[k], pi_real_lengths[j]);
                    }
                }
#endif

#ifdef HAVE_OPENMP
#pragma omp for collapse(2)
#endif
                for(unsigned int i = 0 ; i < b ; i++) {
                    for(unsigned int s = 0 ; s < pi_real_lengths[j] ; s++) {
                        abdst_elt piijs = pi.coeff(i, j, s);

                        abelt_ur_set_elt(ab, tmp_pi, piijs);

                        for(unsigned int kl = 0 ; kl < MIN(m,jl) ; kl++) {
                            unsigned int k = pivot_columns[kl];
                            /* TODO: if column k was already a pivot on previous
                             * turn (which could happen, depending on m and n),
                             * then the corresponding entry is probably zero
                             * (exact condition needs to be written more
                             * accurately).
                             */

                            absrc_elt Tkj = T.coeff(k, j, 0);
                            if (abcmp_ui(ab, Tkj, 0) == 0) continue;
                            /* pi[i,k] has length pi_lengths[k]. Multiply
                             * that by T[k,j], which is a constant. Add
                             * to the unreduced thing. We don't have an
                             * mpfq api call for that operation.
                             */
                            absrc_elt piiks = pi.coeff(i, k, s);
                            abmul_ur(ab, tmp, piiks, Tkj);
                            abelt_ur_add(ab, tmp_pi, tmp_pi, tmp);
                        }
                        abreduce(ab, piijs, tmp_pi);
                    }
                }
            }
            abelt_ur_clear(ab, &tmp);
            abelt_ur_clear(ab, &tmp_pi);
        }
        /* }}} */

        ASSERT_ALWAYS(r == m);

        /* {{{ Now for all pivots, multiply column in pi by x */
        for (unsigned int j = 0; j < b ; j++) {
            if (!is_pivot[j]) continue;
            if (pi_real_lengths[j] >= pi.alloc) {
                if (!generator_found) {
                    pi.realloc(pi.alloc + MAX(pi.alloc / (m+n), 1));
                    printf("t=%u, expanding allocation for pi (now %zu%%) ; lengths: ",
                            bm.t,
                            100 * pi.alloc / pi_room_base);
                    for(unsigned int j = 0; j < b; j++)
                        printf(" %u", pi_real_lengths[j]);
                    printf("\n");
                } else {
                    ASSERT_ALWAYS(bm.lucky[j] <= 0);
                    if (bm.lucky[j] == 0)
                        printf("t=%u, column %u discarded from now on\n",
                                bm.t, j);
                    bm.lucky[j] = -1;
                    pi_lengths[j]++;
                    pi_real_lengths[j]++;
                    delta[j]++;
                    continue;
                }
            }
            pi.multiply_column_by_x(j, pi_real_lengths[j]);
            pi_real_lengths[j]++;
            pi_lengths[j]++;
            delta[j]++;
        }
        /* }}} */
    }

    pi.size = 0;
    for(unsigned int j = 0; j < b; j++) {
        if (pi_real_lengths[j] > pi.size)
            pi.size = pi_real_lengths[j];
    }
    /* Given the structure of the computation, there's no reason for the
     * initial estimate to go wrong.
     */
    ASSERT_ALWAYS(pi.size <= pi.alloc);
    for(unsigned int j = 0; j < b; j++) {
        for(unsigned int k = pi_real_lengths[j] ; k < pi.size ; k++) {
            for(unsigned int i = 0 ; i < b ; i++) {
                ASSERT_ALWAYS(abis_zero(ab, pi.coeff(i, j, k)));
            }
        }
    }
    pi.size = MIN(pi.size, pi.alloc);

    return generator_found;
}
/* }}} */

/* wrap this up */
int
bw_lingen_basecase(bmstatus & bm, matpoly & pi, matpoly & E, std::vector<unsigned int> & delta)
{
    lingen_call_companion const & C = bm.companion(bm.depth(), E.size);
    tree_stats::sentinel dummy(bm.stats, __func__, E.size, C.total_ncalls, true);
    bm.stats.plan_smallstep("basecase", C.ttb);
    bm.stats.begin_smallstep("basecase");
    int done = bw_lingen_basecase_raw(bm, pi, E, delta);
    bm.stats.end_smallstep();
    E = matpoly();
    return done;
}

void test_basecase(abdst_field ab, unsigned int m, unsigned int n, size_t L, gmp_randstate_t rstate)/*{{{*/
{
    /* used by testing code */
    bmstatus bm(m,n);
    mpz_t p;
    mpz_init(p);
    abfield_characteristic(ab, p);
    abfield_specify(bm.d.ab, MPFQ_PRIME_MPZ, p);
    unsigned int t0 = bm.t = iceildiv(m,n);
    std::vector<unsigned int> delta(m+n, t0);
    matpoly E(ab, m, m+n, L);
    E.fill_random(L, rstate);
    matpoly pi;
    bw_lingen_basecase_raw(bm, pi, E, delta);
    mpz_clear(p);
}/*}}}*/