#include "cado.h"
#include <stdint.h>     /* AIX wants it first (it's a bug) */
#include <stdlib.h>
#include <stdio.h>
#include <gmp.h>
#include "utils.h" /* for getprime() */
#include "pm1.h"
#include "pp1.h"
#include "facul_ecm.h"
#include "bytecode.h"
#include "portability.h"

/* By default we compress the bytecode chain.
 * You can set it to 0 to disabled it.
 */
#define BYTECODE_COMPRESS 1

/* Costs of operations for P+1 (for Lucas chain computed with PRAC algorithm) */
prac_cost_t pp1_opcost = { .dadd = 1., .dbl = 1.};

/* Costs of operations for interpreting PRAC chains on Montgomery curves
 * The cost are the number of modular multiplications and squarings.
 */
prac_cost_t ec_montgomery_opcost = { .dadd = 6., .dbl = 5.};

/* Costs of operations for interpreting double base chains on Twisted Edwards
 * curves with a=-1.
 * The cost are the number of modular multiplications and squarings.
 */
dbchain_cost_t ec_edwards_dbchain_opcost = { .dbl=7., .dbladd=15., .tpl=12.,
                                             .tpladd=21., .extra_final_add=1. };
/* Costs of operations for interpreting precomp chains on Twisted Edwards
 * curves with a=-1.
 * The cost are the number of modular multiplications and squarings.
 */
precomp_cost_t ec_edwards_precomp_opcost = { .add=7., .extra_add_for_add=1.,
                                             .dbl=7., .extra_dbl_for_add=1.,
                                             .tpl=12., .extra_tpl_for_add=2. };
/*
 *  For those curves, we use 3 different models:
 *    projective, extended and (only internally) completed
 *  The costs corresponds to operations on different models:
 *    - dbl corresponds to a doubling projective -> projective
 *    - add corresponds to an addition extended,extended -> projective
 *    - dbladd corresponds to a doubling and an addition
 *        projective, extended -> projective
 *      It is more costly than add + dbl but it is due to the fact that if we
 *      wanted to do the same operation with 1 add and 1 dbl, we would need to
 *      convert the output of the dbl from projective to extended in order to be
 *      able to use the add, which is costly.
 *    - dbl_precomp corresponds to a doubling extended -> extended
 *    - add_precomp corresponds to an addition extended, extended, -> extended
 *  We count 1 for a multiplication and 1 for a squaring on the base field.
 */

mishmash_cost_t ec_mixed_repr_opcost = { .dbchain = &ec_edwards_dbchain_opcost,
                                         .precomp = &ec_edwards_precomp_opcost,
                                         .prac = &ec_montgomery_opcost,
                                         .switch_cost = -4. };

/***************************** P-1 ********************************************/

void
pm1_make_plan (pm1_plan_t *plan, const unsigned int B1, const unsigned int B2,
	       int verbose)
{
  mpz_t E;
  unsigned int p;
  size_t tmp_E_nrwords;

  if (verbose)
    printf("Making plan for P-1 with B1=%u, B2=%u\n", B1, B2);
  /* Generate the exponent for stage 1 */
  plan->exp2 = 0;
  for (p = 1; p <= B1 / 2; p *= 2)
    plan->exp2++;

  plan->B1 = B1;
  mpz_init (E);
  mpz_set_ui (E, 1UL);
  prime_info pi;
  prime_info_init (pi);
  p = (unsigned int) getprime_mt (pi);
  ASSERT (p == 3);
  for ( ; p <= B1; p = (unsigned int) getprime_mt (pi))
    {
      unsigned long q;
      /* Uses p^k s.t. (p-1)p^(k-1) <= B1, except for p=2 because our
         base 2 is a QR for primes == 1 (mod 8) already */
      for (q = 1; q <= B1 / (p - 1); q *= p)
        mpz_mul_ui (E, E, p);
    }
  prime_info_clear (pi);

  if (verbose)
    gmp_printf ("pm1_make_plan: E = %Zd;\n", E);

  plan->E = mpz_export (NULL, &tmp_E_nrwords, -1, sizeof(unsigned long),
                        0, 0, E);
  plan->E_nrwords = (unsigned int) tmp_E_nrwords;
  mpz_clear (E);
  /* Find highest set bit in E. */
  ASSERT (plan->E[plan->E_nrwords - 1] != 0);
  plan->E_mask = ~0UL - (~0UL >> 1); /* Only MSB set */
  while ((plan->E[plan->E_nrwords - 1] & plan->E_mask) == 0UL)
    plan->E_mask >>= 1;

  /* stage2 is done with P+1 code */
  stage2_cost_t stage2_opcost = { .dadd = pp1_opcost.dadd,
                                  .dbl = pp1_opcost.dbl, .is_ecm = 0 };
  stage2_make_plan (&(plan->stage2), B1, B2, &stage2_opcost, verbose);
}


void
pm1_clear_plan (pm1_plan_t *plan)
{
  stage2_clear_plan (&(plan->stage2));

  free (plan->E);
  plan->E = NULL;
  plan->E_nrwords = 0;
  plan->B1 = 0;
}

/***************************** P+1 ********************************************/

/* Make byte code for addition chain for stage 1, and the parameters for
   stage 2 */

void
pp1_make_plan (pp1_plan_t *plan, const unsigned int B1, const unsigned int B2,
	       int verbose)
{
  if (verbose)
    printf("Making plan for P+1 with B1=%u, B2=%u\n", B1, B2);

  /* Make bytecode for stage 1 */
  plan->exp2 = 0;
  for (unsigned int p = 1; p <= B1 / 2; p *= 2)
    plan->exp2++;

  plan->B1 = B1;

  /* Allocates plan->bc, fills it and returns bc_len */
  bytecode_prac_encode (&(plan->bc), B1, plan->exp2, 0, &pp1_opcost,
                        BYTECODE_COMPRESS, verbose);

  /* Make stage 2 plan */
  stage2_cost_t stage2_opcost = { .dadd = pp1_opcost.dadd,
                                  .dbl = pp1_opcost.dbl, .is_ecm = 0 };
  stage2_make_plan (&(plan->stage2), B1, B2, &stage2_opcost, verbose);
}

void
pp1_clear_plan (pp1_plan_t *plan)
{
  stage2_clear_plan (&(plan->stage2));
  free (plan->bc);
  plan->bc = NULL;
  plan->B1 = 0;

  /* Clear the cache for the chains computed by prac algorithm.
   * The first call to this function will free the memory, the other calls will
   * be nop.
   */
  bytecode_prac_cache_free ();
}

/***************************** ECM ********************************************/

/* Generate bytecode for stage 1, and the parameters for stage 2.
   The curve used in ECM will be computed using the given 'parameterization'
   with the given 'parameter' value.
   "extra_primes" controls whether some primes should be added (or left out!)
   on top of the primes and prime powers <= B1, for example to take into
   account the known factors in the group order. */

void
ecm_make_plan (ecm_plan_t *plan, const unsigned int B1, const unsigned int B2,
               const ec_parameterization_t parameterization,
               const unsigned long parameter, const int extra_primes,
               const int verbose)
{
  unsigned int pow3_extra;

  if (verbose)
    printf("Making plan for ECM with B1=%u, B2=%u, parameterization = %d, "
           "parameter = %lu, extra primes = %d\n", B1, B2, parameterization,
           parameter, extra_primes);

  /* If group order is divisible by 12 or 16, add two or four 2s to stage 1 */
  if (extra_primes)
  {
    if (parameterization & ECM_TORSION16)
      plan->exp2 = 4;
    else if (parameterization & ECM_TORSION12)
      plan->exp2 = 2;
  }
  else
    plan->exp2 = 0;
  for (unsigned int q = 1; q <= B1 / 2; q *= 2)
    plan->exp2++;
  if (verbose)
    printf ("Exponent of 2 in stage 1 primes: %u\n", plan->exp2);

  /* Make bytecode for stage 1 */
  plan->B1 = B1;
  plan->parameterization = parameterization;
  plan->parameter = parameter;

  /* If group order is divisible by 12, add another 3 to stage 1 primes */
  pow3_extra = (extra_primes && (parameterization & ECM_TORSION12)) ? 1 : 0 ;
  if (verbose && pow3_extra)
    printf ("Add another 3 to stage 1 primes\n");

  /* Allocates plan->bc, fills it and returns bc_len.
   * For Montgomery curves: we use Lucas chains computed with PRAC algorithm.
   */
  if (parameterization & FULLMONTY)
  {
    bytecode_prac_encode (&(plan->bc), B1, plan->exp2, pow3_extra,
        &ec_montgomery_opcost, BYTECODE_COMPRESS, verbose);
  }
  else if (parameterization & FULLMONTYTWED)
  {
    bytecode_mishmash_encode (&(plan->bc), B1, plan->exp2, pow3_extra,
                            &ec_mixed_repr_opcost, BYTECODE_COMPRESS, verbose);
  }
  else
    FATAL_ERROR_CHECK (1, "Unknown parameterization");

  /* Make stage 2 plan */
  stage2_cost_t stage2_opcost = { .dadd = ec_montgomery_opcost.dadd,
                                 .dbl = ec_montgomery_opcost.dbl, .is_ecm = 1 };
  stage2_make_plan (&(plan->stage2), B1, B2, &stage2_opcost, verbose);
}


void
ecm_clear_plan (ecm_plan_t *plan)
{
  stage2_clear_plan (&(plan->stage2));
  plan->B1 = 0;

  if (plan->bc != NULL)
  {
    free (plan->bc);
    plan->bc = NULL;
  }

  /* Clear the cache for the chains computed by prac algorithm.
   * The first call to this function will free the memory, the other calls will
   * be nop.
   */
  bytecode_prac_cache_free ();
}
