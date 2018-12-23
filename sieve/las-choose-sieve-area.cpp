#include "cado.h"
#include <stdarg.h>
#include <stdio.h>
#include <gmp.h>
#include "las-info.hpp"
#include "las-auxiliary-data.hpp"
#include "verbose.h"
#include "utils_cxx.hpp"

int never_discard = 0;      /* only enabled for las_descent */

static bool choose_sieve_area(las_info const & las,
        timetree_t * ptimer MAYBE_UNUSED,
        las_todo_entry const & doing,
        siever_config & conf,
        qlattice_basis & Q,
        uint32_t & J)
{
    sieve_range_adjust Adj;

    int adjust_strategy = las.adjust_strategy;
    {

    /* Our business: find an appropriate siever_config, that is
     * appropriate for this special-q. Different special-q's may lead to
     * different siever_config's, it is allowed.
     *
     * This process completes when we also set conf.logI.
     */
    conf = las.config_pool.get_config_for_q(doing);

    /* The whole business about sieve_range_adjust is to compute the
     * optimal logI and J for this special-q. We have several
     * strategies for that.
     */
    try {
        Adj = sieve_range_adjust(doing, las.cpoly, conf);
    } catch (qlattice_basis::too_skewed const & x) {
        verbose_output_vfprint(0, 1, gmp_vfprintf,
                "# "
                HILIGHT_START
                "Discarding side-%d q=%Zd; rho=%Zd (q-lattice basis does not fit)\n"
                HILIGHT_END,
                doing.side,
                (mpz_srcptr) doing.p,
                (mpz_srcptr) doing.r);
        return false;
    }

#ifndef SUPPORT_LARGE_Q
    if (!Adj.Q.fits_31bits()) { // for fb_root_in_qlattice_31bits
        verbose_output_print(2, 1,
                "# Warning, special-q basis is too skewed,"
                " skipping this special-q."
                " Define SUPPORT_LARGE_Q to proceed anyway.\n");
        return false;
    }
#endif

    }

    /* Must be done before any sort of lognorm computation, even before the
     * adjustment. (only sublat_bound matters, here -- which actual
     * sublattice we will sieve is irrelevant at this point, as what we
     * do will be shared for all sublattices).
     */
    Adj.Q.sublat.m = conf.sublat_bound;

    {

    /* Try strategies for adopting the sieving range */

    int should_discard = !Adj.sieve_info_adjust_IJ();

    if (should_discard) {
        if (never_discard) {
            Adj.set_minimum_J_anyway();
        } else {
            verbose_output_vfprint(0, 1, gmp_vfprintf,
                    "# "
                    HILIGHT_START
                    "Discarding side-%d q=%Zd; rho=%Zd;"
                    HILIGHT_END
                    " a0=%" PRId64
                    "; b0=%" PRId64
                    "; a1=%" PRId64
                    "; b1=%" PRId64
                    "; raw_J=%u;\n", 
                    doing.side,
                    (mpz_srcptr) doing.p,
                    (mpz_srcptr) doing.r,
                    Adj.Q.a0, Adj.Q.b0, Adj.Q.a1, Adj.Q.b1, Adj.J);
            return false;
        }
    }

    /* With adjust_strategy == 2, we want to display the other
     * values, too. Also, strategy 0 wants strategy 1 to run first.
     */
    if (adjust_strategy != 1)
        Adj.sieve_info_update_norm_data_Jmax();

    if (adjust_strategy >= 2)
        Adj.adjust_with_estimated_yield();

    if (adjust_strategy >= 3) {
        /* Let's change that again. We tell the code to keep logI as
         * it is currently. */
        Adj.sieve_info_update_norm_data_Jmax(true);
    }

    /* check whether J is too small after the adjustments */
    if (Adj.J < Adj.get_minimum_J())
    {
        if (never_discard) {
            Adj.set_minimum_J_anyway();
        } else {
            verbose_output_vfprint(0, 1, gmp_vfprintf,
                    "# "
                    HILIGHT_START
                    "Discarding side-%d q=%Zd; rho=%Zd;"
                    HILIGHT_END,
                    doing.side,
                    (mpz_srcptr) doing.p,
                    (mpz_srcptr) doing.r);
            verbose_output_print(0, 1,
                    " a0=%" PRId64
                    "; b0=%" PRId64
                    "; a1=%" PRId64
                    "; b1=%" PRId64
                    "; raw_J=%u;\n",
                    Adj.Q.a0, Adj.Q.b0, Adj.Q.a1, Adj.Q.b1, Adj.J);
            return false;
        }
    }

    /* At this point we've decided on a new configuration for the
     * siever.
     */
    conf.logI = Adj.logI;
    Q = Adj.Q;
    J = Adj.J;

    }

    return true;
}

bool choose_sieve_area(las_info const & las,
        std::shared_ptr<nfs_aux> aux_p,
        las_todo_entry const & doing,
        siever_config & conf,
        qlattice_basis & Q,
        uint32_t & J)
{
    timetree_t & timer(aux_p->timer_special_q);
    return choose_sieve_area(las, &timer, doing, conf, Q, J);
}

bool choose_sieve_area(las_info const & las,
        las_todo_entry const & doing,
        siever_config & conf,
        qlattice_basis & Q,
        uint32_t & J)
{
    return choose_sieve_area(las, nullptr, doing, conf, Q, J);
}

