#include "cado.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <float.h>

#include "portability.h"
#include "macros.h"

#include "bytecode.h"
#include "getprime.h"

/******************************************************************************/
/************************** encoder utils functions ***************************/
/******************************************************************************/
struct bytecode_encoder_s
{
  bytecode bc;
  unsigned int len;
  unsigned int nalloc;
};

typedef struct bytecode_encoder_s bytecode_encoder_t[1];
typedef struct bytecode_encoder_s * bytecode_encoder_ptr;
typedef const struct bytecode_encoder_s * bytecode_encoder_srcptr;


static inline void
bytecode_encoder_init (bytecode_encoder_ptr e)
{
  e->bc = NULL;
  e->len = 0;
  e->nalloc = 0;
}

static inline void
bytecode_encoder_clear (bytecode_encoder_ptr e)
{
  free (e->bc);
  e->bc = NULL;
  e->len = 0;
  e->nalloc = 0;
}

static inline unsigned int
bytecode_encoder_length (bytecode_encoder_srcptr e)
{
  return e->len;
}

static inline void
bytecode_encoder_add_one (bytecode_encoder_ptr e, bytecode_elt b)
{
  if (e->len >= e->nalloc)
  {
    e->nalloc += 32;
    e->bc = (bytecode) realloc (e->bc, e->nalloc * sizeof (bytecode_elt));
    ASSERT_ALWAYS (e->bc != NULL);
  }

  e->bc[e->len] = b;
  e->len++;
}

static inline void
bytecode_encoder_remove_one (bytecode_encoder_ptr e)
{
  ASSERT_ALWAYS (e->len > 0);
  e->len--;
}

/******************************************************************************/
/***************************** double base chains *****************************/
/******************************************************************************/

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte (if return value is zero) or the byte on which an error occurs (if
 * return value is nonzero)
 */
int
bytecode_dbchain_check_internal (bytecode_const bc, bytecode_const *endptr,
                                 mpz_t *R, unsigned int R_len, int verbose)
{
  int ret = 0;

  while (1)
  {
    uint8_t op, f, s, n, pow2, pow3;
    bytecode_elt_split_2_1_1_4 (&op, &f, &s, &n, *bc);

    if (n >= R_len)
    {
      printf ("# %s: error, n=%u must be < %u\n", __func__, n, R_len);
      ret = 1;
    }
    else
    {
      if (op == DBCHAIN_OP_DBLADD)
      {
        bc++;
        pow2 = *bc; 
        pow3 = 0;
      }
      else if (op == DBCHAIN_OP_TPLADD)
      {
        bc++;
        pow3 = *bc; 
        pow2 = 0;
      }
      else /* op == DBCHAIN_OP_TPLDBLADD */
      {
        bc++;
        pow3 = *bc; 
        bc++;
        pow2 = *bc;
      }
    
      if (verbose)
      {
        char opchar = s ? '-' : '+';
        printf ("# %s: R[%u] <- 2^%u 3^%u R[0] %c R[%u]\n", __func__, f, pow2,
            pow3, opchar, n);
      }

      for (uint8_t i = 0; i < pow3; i++)
        mpz_mul_ui (R[0], R[0], 3);
      mpz_mul_2exp (R[0], R[0], pow2);
      if (s == 0)
        mpz_add (R[f], R[0], R[n]);
      else
        mpz_sub (R[f], R[0], R[n]);
    }

    if (f || ret) /* is it finished or was there an error ? */
      break;
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;
  return ret;
}

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte.
 */
double
bytecode_dbchain_cost (bytecode_const bc, bytecode_const *endptr,
                       const dbchain_cost_t * opcost)
{
  double cost = 0.;

  while (1)
  {
    uint8_t op, f;
    bytecode_elt_split_2_1_1_4 (&op, &f, NULL, NULL, *bc);

    if (op == DBCHAIN_OP_DBLADD)
    {
      bc++;
      uint8_t pow2 = *bc; 
      cost += (pow2-1) * opcost->dbl + opcost->dbladd;
    }
    else if (op == DBCHAIN_OP_TPLADD)
    {
      bc++;
      uint8_t pow3 = *bc; 
      cost += (pow3-1) * opcost->tpl + opcost->tpladd;
    }
    else /* op == DBCHAIN_OP_TPLDBLADD */
    {
      bc++;
      uint8_t pow3 = *bc; 
      bc++;
      uint8_t pow2 = *bc;
      if (pow2 > 0)
        cost += pow3 * opcost->tpl + (pow2-1) * opcost->dbl + opcost->dbladd;
      else
        cost += (pow3-1) * opcost->tpl + opcost->tpladd;
    }

    if (f) /* is it finished ? */
    {
      cost += opcost->extra_final_add;
      break;
    }
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;

  return cost;
}

/******************************************************************************/
/******************************* precomputation *******************************/
/******************************************************************************/

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte (if return value is zero) or the byte on which an error occurs (if
 * return value is nonzero)
 */
int
bytecode_precomp_check_internal (bytecode_const bc, bytecode_const *endptr,
                                 mpz_t *R, unsigned int R_len, int verbose)
{
  int ret = 0;

  while (*bc != PRECOMP_FINAL)
  {
    uint8_t op, a, s, k;
    bytecode_elt_split_2_1_1_4 (&op, &a, &s, &k, *bc);

    if (k >= R_len)
    {
      printf ("# %s: error, k=%u must be < %u\n", __func__, k, R_len);
      ret = 1;
    }
    else if (op == PRECOMP_OP_ADD)
    {
      uint8_t i,j;
      bc++;
      bytecode_elt_split_4_4 (&i, &j, *bc);

      if (verbose)
      {
        char opchar = s ? '-' : '+';
        printf ("# %s: R[%u] <- R[%u] %c R[%u]\n", __func__, k, i, opchar, j);
      }

      if (s == 0)
        mpz_add (R[k], R[i], R[j]);
      else
        mpz_sub (R[k], R[i], R[j]);
    }
    else
    {
      if (op == PRECOMP_OP_DBL)
      {
        bc++;
        uint8_t pow2 = bytecode_elt_to_uint8 (*bc);
        if (verbose)
          printf ("# %s: R[%u] <- R[0] <- 2^%u R[0]\n", __func__, k, pow2);
        mpz_mul_2exp (R[0], R[0], pow2);
        mpz_set (R[k], R[0]);
      }
      else /* op == PRECOMP_OP_TPL */
      {
        bc++;
        uint8_t pow3 = bytecode_elt_to_uint8 (*bc);
        if (verbose)
          printf ("# %s: R[%u] <- R[0] <- 3^%u R[0]\n", __func__, k, pow3);
        for (uint8_t i = 0; i < pow3; i++)
          mpz_mul_ui (R[0], R[0], 3);
        mpz_set (R[k], R[0]);
      }
    }

    if (ret) /* was there an error ? */
      break;
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;
  return ret;
}

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte.
 */
double
bytecode_precomp_cost (bytecode_const bc, bytecode_const *endptr,
                       const precomp_cost_t * opcost)
{
  double cost = 0.;

  while (*bc != PRECOMP_FINAL)
  {
    uint8_t op, a;
    bytecode_elt_split_2_1_1_4 (&op, &a, NULL, NULL, *bc);

    if (op == PRECOMP_OP_ADD)
    {
      bc++;
      cost += opcost->add;
      if (a)
        cost += opcost->extra_add_for_add;
    }
    else if (op == PRECOMP_OP_DBL)
    {
      bc++;
      uint8_t pow2 = bytecode_elt_to_uint8 (*bc);
      cost += pow2 * opcost->dbl;
      if (a)
        cost += opcost->extra_dbl_for_add;
    }
    else /* op == PRECOMP_OP_TPL */
    {
      bc++;
      uint8_t pow3 = bytecode_elt_to_uint8 (*bc);
      cost += pow3 * opcost->tpl;
      if (a)
        cost += opcost->extra_tpl_for_add;
    }

    bc++; /* go to next byte */
  }
  if (endptr != NULL)
    *endptr = bc;

  return cost;
}

/******************************************************************************/
/************************************ PRAC ************************************/
/******************************************************************************/

/* Table of multipliers for PRAC. prac_mul[i], 1 <= i <= 9, has continued 
   fraction sequence of all ones but with a 2 in the (i+1)-st place, 
   prac_mul[i], 10 <= i <= 17, has continued fraction sequence of all ones 
   but with a 2 in second and in the (i-7)-th place
   and prac_mul[0] is all ones, i.e. the golden ratio. */
#define PRAC_NR_MULTIPLIERS 10
static const double prac_mul[PRAC_NR_MULTIPLIERS] = 
  {1.61803398874989484820 /* 0 */, 1.38196601125010515179 /* 1 */, 
   1.72360679774997896964 /* 2 */, 1.58017872829546410471 /* 3 */, 
   1.63283980608870628543 /* 4 */, 1.61242994950949500192 /* 5 */,
   1.62018198080741576482 /* 6 */, 1.61721461653440386266 /* 7 */, 
   1.61834711965622805798 /* 8 */, 1.61791440652881789386 /* 9 */,
#if PRAC_NR_MULTIPLIERS == 18
   1.41982127170453589529 /* 10 */, 1.36716019391129371457 /* 11 */,
   1.38757005049050499808 /* 12 */, 1.37981801919258423518 /* 13 */,
   1.38278538346559613734 /* 14 */, 1.38165288034377194202 /* 15 */,
   1.38208559347118210614 /* 16 */, 1.38192033153010418805 /* 17 */,
#endif
   };

/***** Cache mechanism *****/

/* Mutual exclusion lock for the global variables for the cache mechanism */
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
struct prac_cache_s
{
  unsigned int k;
  const prac_cost_t *opcost;
  double best_mul;
  double mincost;
};

typedef struct prac_cache_s prac_cache_t;

prac_cache_t *prac_cache = NULL;
unsigned int prac_cache_nb = 0;
unsigned int prac_cache_alloc = 0;

static inline int
prac_cache_is_in (unsigned int k, const prac_cost_t *opcost, double * best_mul,
                  double * mincost)
{
  pthread_mutex_lock (&lock);
  for (unsigned int i = 0; i < prac_cache_nb; i++)
  {
    if (prac_cache[i].k == k && prac_cache[i].opcost == opcost)
    {
      *best_mul = prac_cache[i].best_mul;
      *mincost = prac_cache[i].mincost;
      pthread_mutex_unlock (&lock);
      return 1;
    }
  }
  pthread_mutex_unlock (&lock);
  return 0;
}

static inline void
prac_cache_add_one (unsigned int k, const prac_cost_t *opcost, double best_mul,
                    double mincost)
{
  pthread_mutex_lock (&lock);
  if (prac_cache_nb >= prac_cache_alloc)
  {
    prac_cache_alloc += 16;
    prac_cache = (prac_cache_t *)
                 realloc (prac_cache, prac_cache_alloc * sizeof (prac_cache_t));
    ASSERT_ALWAYS (prac_cache != NULL);
  }

  prac_cache[prac_cache_nb++] = (prac_cache_t) { .k = k, .opcost= opcost,
                                     .best_mul = best_mul, .mincost = mincost };
  pthread_mutex_unlock (&lock);
}

void
bytecode_prac_cache_free ()
{
  pthread_mutex_lock (&lock);
  if (prac_cache)
    free (prac_cache);
  prac_cache = NULL;
  prac_cache_alloc = 0;
  prac_cache_nb = 0;
  pthread_mutex_unlock (&lock);
}

/***** verbose function *****/

void
bytecode_prac_fprintf (FILE *out, bytecode_const bc)
{
  unsigned int i = 0;
  while (1)
  {
    int finished = 0;
    uint8_t b = bytecode_elt_to_uint8 (*bc);
    fprintf (out, "%s 0x%02x", (i == 0) ? "" : ",", b);
    switch (b)
    {
      case PRAC_SWAP: /* [ = 's' ] swap */
        fprintf (out, " [swap]");
        break;
      case PRAC_SUBBLOCK_INIT: /* [ = 'i' ] Start of a sub-block */
        fprintf (out, " [i]");
        break;
      case PRAC_SUBBLOCK_FINAL: /* [ = 'f' ] End of a sub-block */
        fprintf (out, " [f]");
        break;
      case PRAC_BLOCK_FINAL: /* [ = 'F' ] End of the block */
        fprintf (out, " [F]");
        finished = 1;
        break;
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
        break;
      case 10:
        fprintf (out, " [fi]");
        break;
      case 11:
        fprintf (out, " [3s]");
        break;
      case 12:
        fprintf (out, " [3fi]");
        break;
      case 13:
        fprintf (out, " [3s3s]");
        break;
      default:
        fprintf (out, " [unknown]");
        finished = 1;
        break;
    }

    i++;
    if (finished)
      break;
    else
      bc++; /* go to next byte */
  }
  printf (" (length = %u)\n", i);
}

/***** Computing the PRAC chains *****/

/***********************************************************************
   Generating Lucas chains with Montgomery's PRAC algorithm. Code taken 
   from GMP-ECM, mostly written by Paul Zimmermann, and slightly 
   modified here
************************************************************************/


/* Produce a PRAC chain with initial multiplier v. 
 * Returns its arithmetic cost or DBL_MAX if no chain could be computed.
 * The cost of a differential addition is opcost->dadd, the cost a doubling is
 * opcost->dbl.
 */
static double
prac_chain (const unsigned int n, const double v, const prac_cost_t *opcost,
            bytecode_encoder_ptr encoder)
{
  unsigned int d, e, r;
  double cost;

  d = n;
  r = (unsigned int) ((double) d / v + 0.5);
  if (r >= n)
    return DBL_MAX;
  d = n - r;
  e = 2 * r - n;

  /* initial doubling (subchain init) */
  cost = opcost->dbl;
  if (encoder != NULL)
    bytecode_encoder_add_one (encoder, PRAC_SUBBLOCK_INIT); /* 'i' */

  while (d != e)
  {
    if (d < e)
    { /* swap d and e */
      r = d;
      d = e;
      e = r;
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, PRAC_SWAP); /* 's' */
    }
    if (4 * d <= 5 * e && ((d + e) % 3) == 0)
    { /* condition 1 */
      d = (2 * d - e) / 3;
      e = (e - d) / 2;
      cost += 3 * opcost->dadd; /* 3 additions */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 1);
    }
    else if (4 * d <= 5 * e && (d - e) % 6 == 0)
    { /* condition 2 */
      d = (d - e) / 2;
      cost += opcost->dadd + opcost->dbl; /* one addition, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 2);
    }
    else if (d <= 4 * e)
    { /* condition 3 */
      d -= e;
      cost += opcost->dadd; /* one addition */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 3);
    }
    else if ((d + e) % 2 == 0)
    { /* condition 4 */
      d = (d - e) / 2;
      cost += opcost->dadd + opcost->dbl; /* one addition, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 4);
    }
    /* now d+e is odd */
    else if (d % 2 == 0)
    { /* condition 5 */
      d /= 2;
      cost += opcost->dadd + opcost->dbl; /* one addition, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 5);
    }
    /* now d is odd and e even */
    else if (d % 3 == 0)
    { /* condition 6 */
      d = d / 3 - e;
      cost += 3 * opcost->dadd + opcost->dbl; /* three additions, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 6);
    }
    else if ((d + e) % 3 == 0)
    { /* condition 7 */
      d = (d - 2 * e) / 3;
      cost += 3 * opcost->dadd + opcost->dbl; /* three additions, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 7);
    }
    else if ((d - e) % 3 == 0)
    { /* condition 8 */
      d = (d - e) / 3;
      cost += 3 * opcost->dadd + opcost->dbl; /* three additions, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 8);
    }
    else /* necessarily e is even */
    { /* condition 9 */
      e /= 2;
      cost += opcost->dadd + opcost->dbl; /* one addition, one doubling */
      if (encoder != NULL)
        bytecode_encoder_add_one (encoder, (bytecode_elt) 9);
    }
  }

  /* final addition */
  cost += opcost->dadd;
  if (encoder != NULL)
    bytecode_encoder_add_one (encoder, PRAC_SUBBLOCK_FINAL); /* 'f' */

  /* Here d = gcd(n, r). If d != 1, then the sequence cannot be
     reduced below d, i.e., the chain does not start at 1.
     It would be the end of a concatenated chain instead.
     Return DBL_MAX in this case. */
  return (d == 1) ? cost : DBL_MAX;
}
/* Encode bytecode for an addition chain for odd k (repeated val times), and
 * return its cost.
 * k must be odd and prime (not exactly: prac algorithm always succeeds on
 * primes and sometimes fails on composite; it must exist at least one
 * multipliers for which the caller knonws that prac will succeeds; for k prime
 * it is always the case).
 */
static double
bytecode_prac_encode_one (bytecode_encoder_ptr encoder, const unsigned int k,
                          const unsigned int val, const prac_cost_t *opcost,
                          int verbose)
{
  double mincost = DBL_MAX, best_mul = 0.;

  ASSERT_ALWAYS (k % 2 == 1);

  if (k == 3)
  {
    /* There is only one Lucas chain possible in this case so we do not care
     * what multipliers we use.
     */
     best_mul = prac_mul[0];
     mincost = opcost->dbl + opcost->dadd;

    if (verbose)
      printf ("# %s: k=3 best_mul=%f mincost=%f\n", __func__,best_mul, mincost);
  }
  else if (prac_cache_is_in (k, opcost, &best_mul, &mincost))
  {
    if (verbose)
      printf ("# %s: k=%u best_mul=%f mincost=%f [from cache]\n", __func__, k,
              best_mul, mincost);
  }
  else
  {
    /* Find the best multiplier for this k */
    for (unsigned int i = 0 ; i < PRAC_NR_MULTIPLIERS; i++)
    {
      double mul = prac_mul[i];
      if (verbose > 1)
        printf ("# %s: computing Lucas chain for k=%u with mul=%f\n", __func__, 
            k, mul);
      double cost = prac_chain (k, mul, opcost, NULL);

      if (verbose && cost == DBL_MAX)
        printf ("# %s: could not compute Lucas chain for k=%u with mul=%f\n",
            __func__, k, mul);
      if (cost < mincost)
      {
        mincost = cost;
        best_mul = mul;
      }
      if (verbose > 1)
        printf ("# %s: k=%u mul=%f cost=%f\n", __func__, k, mul, cost);
    }

    if (verbose)
      printf ("# %s: k=%u best_mul=%f mincost=%f\n", __func__, k, best_mul,
          mincost);

    /* add it to the cache */
    prac_cache_add_one (k, opcost, best_mul, mincost);
  }

  /* If we have mincost == DBL_MAX, it means that this k is composite and prac
   * cannot make a valid chain for it. We could try to factor k and make a
   * composite chain from the prime factors. For now, we bail out - caller
   * shouldn't give a composite k that doesn't have a prac chain we can find.
   */
  ASSERT_ALWAYS (mincost != DBL_MAX);

  /* Write the best chain (repeated val times) and return the cost of the chain
   * (repeated val times)
   */
  double cost = 0.;
  for (unsigned int i = 0; i < val; i++)
    cost += prac_chain (k, best_mul, opcost, encoder);
  return cost;
}

void
bytecode_prac_encode (bytecode *bc, unsigned int B1, unsigned int pow2_nb,
                      unsigned int pow3_extra, const prac_cost_t *opcost,
                      int compress, int verbose)
{
  double prac_cost = 0.;
  bytecode_encoder_t encoder;

  /* init the encoder */
  bytecode_encoder_init (encoder);

  /* Do all the odd primes */
  prime_info pi;
  prime_info_init (pi);
  unsigned int p = (unsigned int) getprime_mt (pi);
  ASSERT_ALWAYS (p == 3);
  for ( ; p <= B1; p = (unsigned int) getprime_mt (pi))
  {
    unsigned int val = (p == 3) ? pow3_extra : 0;
    for (unsigned int q = 1; q <= B1 / p; q *= p)
      val++;

    prac_cost += bytecode_prac_encode_one (encoder, p, val, opcost, verbose);
  }
  prime_info_clear (pi);

  /* replace last 'f' by a 'F' */
  bytecode_encoder_remove_one (encoder);
  bytecode_encoder_add_one (encoder, PRAC_BLOCK_FINAL);

  /* compress if asked */
  if (compress)
  {
  /* TODO compress if necessary */
  }

  /* Copy into bc */
  unsigned int bc_len = bytecode_encoder_length (encoder);
  *bc = (bytecode) malloc (bc_len * sizeof (bytecode_elt));
  ASSERT_ALWAYS (*bc);
  memcpy (*bc, encoder->bc, bc_len * sizeof (bytecode_elt));

  if (verbose)
  {
    /* The cost of the initial doublings */
    double power2cost = pow2_nb * opcost->dbl;
    /* Print the bytecode */
    printf ("Byte code for stage 1: ");
    bytecode_prac_fprintf (stdout, *bc);
    /* Print the cost */
    printf ("# %s: cost of power of 2: %f\n", __func__, power2cost);
    printf ("# %s: total cost: %f\n", __func__, prac_cost + power2cost);
  }

  bytecode_encoder_clear (encoder);
}

/***** check functions *****/

#define mpz_dadd(r, A, B, C) do { mpz_add (r, A, B); } while (0)

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte (if return value is zero) or the byte on which an error occurs (if
 * return value is nonzero)
 */
int
bytecode_prac_check_internal (bytecode_const bc, bytecode_const *endptr,
                              mpz_t *R, unsigned int R_len, int verbose)
{
  int ret = 0;

  if (R_len < 5)
  {
    printf ("# %s: error, R_len=%u must be >= 5 for PRAC bytecode\n", __func__,
        R_len);
    ret = 1;
  }
  
  if (*bc != PRAC_SUBBLOCK_INIT)
  {
    printf ("# %s: error, PRAC bytecode must start with 'i' = 0x%02x, "
            "not 0x%02x\n", __func__, PRAC_SUBBLOCK_INIT, *bc);
    ret = 1;
  }

  /* We always maintain R[0]-R[1] = R[2] */
  while (!ret)
  {
    int finished = 0;
    switch (*bc)
    {
      case PRAC_SWAP: /* [ = 's' ] Swap R[0], R[1] */
        if (verbose)
          printf ("# %s: swap R[0] <-> R[1]\n", __func__);
        mpz_swap (R[0], R[1]);
        break;
      case PRAC_SUBBLOCK_INIT: /* [ = 'i' ] Start of a sub-block */
        if (verbose)
          printf ("# %s: new sub-block: R[0], R[1], R[2] <- 2 R[0], R[0], R[0]"
                  "\n", __func__);
        mpz_set (R[1], R[0]);
        mpz_set (R[2], R[0]);
        mpz_mul_2exp (R[0], R[0], 1);
        break;
      case PRAC_SUBBLOCK_FINAL: /* [ = 'f' ] End of a sub-block */
        if (verbose)
          printf ("# %s: end of sub-block: R[0] <- R[0] + R[1]\n", __func__);
        mpz_dadd (R[0], R[0], R[1], R[2]);
        break;
      case PRAC_BLOCK_FINAL: /* [ = 'F' ] End of the block */
        if (verbose)
          printf ("# %s: end of PRAC block: R[1] <- R[0] + R[1]\n", __func__);
        mpz_dadd (R[1], R[0], R[1], R[2]);
        finished = 1;
        break;
      case 1: /* Rule 1: R[0], R[1] <- 2*R[0]+R[1], R[1]+2*R[1] */
        if (verbose)
          printf ("# %s: rule 1: R[0], R[1] <- 2 R[0] + R[1], R[0] + 2 R[1]\n",
                  __func__);
        mpz_dadd (R[3], R[0], R[1], R[2]);
        mpz_dadd (R[4], R[3], R[0], R[1]);
        mpz_dadd (R[1], R[1], R[3], R[0]);
        mpz_set (R[0], R[4]);
        break;
      case 2: /* Rule 2: R[0], R[1] <- 2*R[0], R[0]+R[1] */
        if (verbose)
          printf ("# %s: rule 2: R[0], R[1] <- 2 R[0], R[0] + R[1]\n",
                  __func__);
        mpz_dadd (R[1], R[0], R[1], R[2]);
        mpz_mul_2exp (R[0], R[0], 1);
        break;
      case 3: /* Rule 3: R[0], R[1], R[2] <- R[0], R[1]+R[0], R[1] */
        if (verbose)
          printf ("# %s: rule 3: R[0], R[1], R[2] <- R[0], R[0] + R[1], R[1]\n",
                  __func__);
        mpz_dadd (R[2], R[1], R[0], R[2]);
        mpz_swap (R[1], R[2]);
        break;
      case 4: /* R[0], R[1], R[2] <- 2 R[0], R[1]+R[0], R[2] */
        if (verbose)
          printf ("# %s: rule 4: R[0], R[1], R[2] <- 2 R[0], R[0] + R[1], R[1]"
                  "\n", __func__);
        mpz_dadd (R[1], R[1], R[0], R[2]);
        mpz_mul_2exp (R[0], R[0], 1);
        break;
      case 5: /* R[0], R[2] <- 2 R[0], R[2]+R[0] */
        if (verbose)
          printf ("# %s: rule 5: R[0], R[2] <- 2 R[0], R[2] + R[0]\n",__func__);
        mpz_dadd (R[2], R[2], R[0], R[1]);
        mpz_mul_2exp (R[0], R[0], 1);
        break;
      case 6: /* R[0], R[1], R[2] <- 3 R[0], 3 R[0]+R[1], R[1] */
        if (verbose)
          printf ("# %s: rule 6: R[0], R[1], R[2] <- 3 R[0], 3 R[0] + R[1],"
                  "R[1]\n",__func__);
        mpz_mul_2exp (R[3], R[0], 1);
        mpz_dadd (R[4], R[0], R[1], R[2]);
        mpz_dadd (R[0], R[3], R[0], R[0]);
        mpz_dadd (R[2], R[3], R[4], R[2]);
        mpz_swap (R[1], R[2]);
        break;
      case 7: /* R[0], R[1] <- 3 R[0], 2 R[0]+R[1] */
        if (verbose)
          printf ("# %s: rule 7: R[0], R[1] <- 3 R[0], 2 R[0] + R[1]\n",
                  __func__);
        mpz_dadd (R[3], R[0], R[1], R[2]);
        mpz_dadd (R[1], R[3], R[0], R[1]);
        mpz_mul_2exp (R[3], R[0], 1);
        mpz_dadd (R[0], R[0], R[3], R[0]);
        break;
      case 8: /* R[0], R[1], R[2] <- 3 R[0], R[0]+R[1], R[0]+R[2] */
        if (verbose)
          printf ("# %s: rule 8: R[0], R[1], R[2] <- 3 R[0], R[0] + R[1], "
                  "R[0] + R[2]\n", __func__);
        mpz_dadd (R[3], R[0], R[1], R[2]);
        mpz_dadd (R[2], R[2], R[0], R[1]);
        mpz_swap (R[1], R[3]);
        mpz_mul_2exp (R[3], R[0], 1);
        mpz_dadd (R[0], R[0], R[3], R[0]);
        break;
      case 9: /* R[1], R[2] <- 2 R[1], R[2]+R[1] */
        if (verbose)
          printf ("# %s: rule 9: R[1], R[2] <- 2 R[1], R[2] + R[1]\n",
              __func__);
        mpz_dadd (R[2], R[2], R[1], R[0]);
        mpz_mul_2exp (R[1], R[1], 1);
        break;
      case 10: /* Combine end of a sub-block and start of a new sub-block */
        if (verbose)
          printf ("# %s: end of sub-block, then new sub-block: R[0], R[1], R[2]"
                  " <- 2 R[0] + 2 R[1], R[0] + R[1], R[0] + R[1]\n", __func__);
        mpz_dadd (R[1], R[0], R[1], R[2]);
        mpz_set (R[2], R[1]);
        mpz_mul_2exp (R[0], R[1], 1);
        break;
      case 11: /* Combine rule 3 and rule 0 */
        if (verbose)
          printf ("# %s: rule 11 (= rule 3 + rule 0): R[0], R[1], R[2] <- "
                  "R[0] + R[1], R[0], R[1]\n",
                  __func__);
        mpz_dadd (R[2], R[1], R[0], R[2]);
        /* (R[1],R[2],R[0]) := (R[0],R[1],R[2])  */
        mpz_swap (R[1], R[2]);
        mpz_swap (R[0], R[1]);
        break;
      case 12: /* Combine rule 3 and rule 10 (end and start of a sub-block) */
        if (verbose)
          printf ("# %s: rule 3, then end of sub-block, then new sub-block: "
                  "R[0], R[1], R[2] <- 4 R[0] + 2 R[1], 2 R[0] + R[1], "
                  "2 R[0] + R[1]\n", __func__);
        mpz_dadd (R[3], R[1], R[0], R[2]);
        mpz_dadd (R[2], R[0], R[3], R[1]);
        mpz_set (R[1], R[2]);
        mpz_mul_2exp (R[0], R[2], 1);
        break;
      case 13: /* Combine rule 3, rule 0, rule 3 and rule 0 (= rule 11 twice) */
        if (verbose)
          printf ("# %s: rule 13 (= rule 3 + rule 0 + rule 3 + rule 0): "
                  "R[0], R[1], R[2] <- 2 R[0] + R[1], R[0] + R[1], R[0]\n",
                  __func__);
        mpz_set (R[3], R[1]);
        mpz_dadd (R[1], R[1], R[0], R[2]);
        mpz_set (R[2], R[0]);
        mpz_dadd (R[0], R[0], R[1], R[3]);
        break;
      default:
        printf ("# %s: error, unknown bytecode 0x%02x\n", __func__, *bc);
        ret = 1;
        break;
    }

    if (finished || ret) /* is it finished or was there an error ? */
      break;
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;
  return ret;
}

/* Return nonzero if the bytecode does not produce E. Return 0 otherwise. */
int
bytecode_prac_check (bytecode_const bc, mpz_srcptr E, int verbose)
{
  int ret = 0;
  mpz_t *R = NULL;
  unsigned int R_nalloc = 0;

  R_nalloc = 5; /* we need 5 points: 3 for PRAC + 2 temporary points */
  R = (mpz_t *) malloc (R_nalloc * sizeof (mpz_t));
  FATAL_ERROR_CHECK (R == NULL, "could not malloc R");
  for (unsigned int i = 0; i < R_nalloc; i++)
    mpz_init (R[i]);

  /* current point (here starting point) go into R[0] at init */
  mpz_set_ui (R[0], 1);

  ret = bytecode_prac_check_internal (bc, NULL, R, R_nalloc, verbose);

  if (ret == 0 && mpz_cmp (R[1], E) != 0) /* output is in R[1] */
  {
    gmp_printf ("# %s: failure:\n Expected %Zd\n Got %Zd\n", __func__, E, R[1]);
    ret = 1;
  }

  if (ret == 0 && verbose)
    printf ("# %s: success\n", __func__);

  /* clear mpz_t's */
  for (unsigned int i = 0; i < R_nalloc; i++)
    mpz_clear (R[i]);

  /* free array */
  free (R);

  return ret;
}

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte.
 */
double
bytecode_prac_cost (bytecode_const bc, bytecode_const *endptr,
                    const prac_cost_t * opcost)
{
  double cost = 0.;

  while (1)
  {
    int finished = 0;
    switch (*bc)
    {
      case PRAC_SWAP:
        break;
      case PRAC_SUBBLOCK_INIT:
        cost += opcost->dbl;
        break;
      case PRAC_BLOCK_FINAL:
        finished = 1;
        no_break();
      case PRAC_SUBBLOCK_FINAL:
      case 3:
      case 11:
        cost += opcost->dadd; 
        break;
      case 1:
        cost += 3 * opcost->dadd;
        break;
      case 2:
      case 4:
      case 5:
      case 9:
      case 10:
        cost += opcost->dadd + opcost->dbl; 
        break;
      case 6:
      case 7:
      case 8:
        cost += 3 * opcost->dadd + opcost->dbl; 
        break;
      case 12:
        cost += 2 * opcost->dadd + opcost->dbl; 
        break;
      case 13:
        cost += 2 * opcost->dadd; 
        break;
      default:
        printf ("Fatal error in %s at %s:%d -- unknown bytecode 0x%02x\n",
                __func__, __FILE__, __LINE__, *bc);
        abort ();
    }

    if (finished) /* is it finished */
      break;
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;

  return cost;
}

/******************************************************************************/
/********************************** MISHMASH **********************************/
/******************************************************************************/

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte.
 */
double
bytecode_mishmash_cost (bytecode_const bc, bytecode_const *endptr,
                        const mishmash_cost_t * opcost)
{
  double cost = 0.;
  bc++; /* ignore first init byte */

  while (*bc != MISHMASH_FINAL)
  {
    uint8_t t, n;
    bytecode_elt_split_4_4 (&t, &n, *bc);

    if (t == MISHMASH_DBCHAIN_BLOCK)
      cost += bytecode_dbchain_cost (++bc, &bc, opcost->dbchain);
    else if (t == MISHMASH_PRECOMP_BLOCK)
      cost += bytecode_precomp_cost (++bc, &bc, opcost->precomp);
    else if (t == MISHMASH_PRAC_BLOCK)
      cost += bytecode_prac_cost (++bc, &bc, opcost->prac);
    else if (t != MISHMASH_INIT) /* unknown bytecode */
    {
      printf ("Fatal error in %s at %s:%d -- unknown bytecode 0x%02x\n",
              __func__, __FILE__, __LINE__, *bc);
      abort ();
    }

    bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;

  return cost;
}


#include "bytecode_mishmash_B1_data.h"

void
bytecode_mishmash_encode (bytecode *bc, unsigned int B1, unsigned int pow2_nb,
                          unsigned int pow3_extra,
                          const mishmash_cost_t * opcost, int compress,
                          int verbose)
{
  ASSERT_ALWAYS (B1 >= mishmash_B1_data[0].B1);

  double cost = 0.; /* used for verbose output */
  bytecode_encoder_t encoder;

  /* init the encoder */
  bytecode_encoder_init (encoder);

  unsigned int i = 0;
  while (1)
  {
    if (mishmash_B1_data[i].B1 == B1)
      break;
    else if (i+1 == mishmash_B1_data_len || B1 < mishmash_B1_data[i+1].B1) 
      break;
    else
      i++;
  }

  mishmash_B1_data_t *precomp_data = &(mishmash_B1_data[i]);

  if (verbose)
  {
    double cost = bytecode_mishmash_cost (precomp_data->bc, NULL, opcost);
    printf ("# %s: for B1=%u using precomputed data for B1=%u with cost=%f\n",
            __func__, B1, precomp_data->B1, cost);
  }


  if (B1 > precomp_data->B1 || pow3_extra > 0) /* use PRAC for missing primes */
  {
    prime_info pi;
    prime_info_init (pi);
    unsigned int p = (unsigned int) getprime_mt (pi);
    ASSERT_ALWAYS (p == 3);
    for ( ; p <= B1; p = (unsigned int) getprime_mt (pi))
    {
      unsigned int val_already_done = 0, val_needed = 0, v;
      for (unsigned int q = 1; q <= precomp_data->B1 / p; q *= p)
        val_already_done++;
      for (unsigned int q = 1; q <= B1 / p; q *= p)
        val_needed++;
      val_needed += (p == 3) ? pow3_extra : 0;
      ASSERT_ALWAYS (val_needed >= val_already_done);
      v = val_needed - val_already_done;

      if (v > 0)
      {
        if (verbose)
          printf ("# %s: do prime p=%u with PRAC [ %u time(s) ]\n", __func__,
              p, v);
        cost += bytecode_prac_encode_one (encoder, p, v, opcost->prac, verbose);
      }
    }
    prime_info_clear (pi);
  }

  if (bytecode_encoder_length (encoder) > 0)
  {
    /* replace last 'f' by a 'F' */
    bytecode_encoder_remove_one (encoder);
    bytecode_encoder_add_one (encoder, PRAC_BLOCK_FINAL);

    /* compress if asked */
    if (compress)
    {
      /* TODO compress if necessary */
    }

    /* Copy into bc */
    unsigned int len = precomp_data->len + bytecode_encoder_length(encoder) + 1;
    bytecode b;
    b = (bytecode) malloc (len * sizeof (bytecode_elt));
    ASSERT_ALWAYS (b);
    /* copy precomputed data */
    memcpy (b, precomp_data->bc, precomp_data->len * sizeof (bytecode_elt));
    /* overwrite last byte of precomputed data with code for new PRAC block */
    b[precomp_data->len-1] = bytecode_elt_build_4_4 (MISHMASH_PRAC_BLOCK, 1);
    /* copy data for extra primes */
    memcpy (&(b[precomp_data->len]), encoder->bc,
            bytecode_encoder_length (encoder) * sizeof (bytecode_elt));
    /* add final bytecode */
    b[len-1] = MISHMASH_FINAL;
    /* change the first byte (if necessary) */
    uint8_t n;
    bytecode_elt_split_4_4 (NULL, &n, b[0]);
    b[0] = bytecode_elt_build_4_4 (MISHMASH_INIT, (n < 3) ? 3 : n);
    /* set *bc */
    *bc = b;
  }
  else
  {
    /* Copy into bc */
    *bc = (bytecode) malloc (precomp_data->len * sizeof (bytecode_elt));
    ASSERT_ALWAYS (*bc);
    memcpy (*bc, precomp_data->bc, precomp_data->len * sizeof (bytecode_elt));
  }

  if (verbose)
  {
    /* The cost of the initial doublings */
    double power2cost = pow2_nb * opcost->prac->dbl; // TODO use the best DBL
    /* Print the bytecode */
    //printf ("Byte code for stage 1: "); // TODO
    //bytecode_prac_fprintf (stdout, *bc); // TODO
    /* Print the cost */
    printf ("# %s: cost of power of 2: %f\n", __func__, power2cost);
    printf ("# %s: total cost: %f\n", __func__, cost + power2cost);
  }

  /* free memory */
  bytecode_encoder_clear (encoder);
}

/* If not NULL, endptr will contain the adress of a pointer to the last parsed
 * byte (if return value is zero) or the byte on which an error occurs (if
 * return value is nonzero)
 */
int
bytecode_mishmash_check_internal (bytecode_const bc, bytecode_const *endptr,
                                  mpz_t *R, unsigned int R_len, int verbose)
{
  int ret = 0;

  bc++; /* ignore first init byte: R should be already allocated */

  while (*bc != MISHMASH_FINAL)
  {
    uint8_t t, n;
    bytecode_elt_split_4_4 (&t, &n, *bc);

    if (t == MISHMASH_INIT)
    {
      printf ("# %s: error, second init bytecode 0x%02x\n", __func__, *bc);
      ret = 1;
    }
    else /* not the initial byte (nor the final byte) */
    {
      if (n >= R_len)
      {
        printf ("# %s: error, n=%u must be < %u\n", __func__, n, R_len);
        ret = 1;
      }
      else
      {
        if (verbose)
          gmp_printf ("# %s: R[0] <- R[%d] (= %Zd)\n", __func__, n, R[n]);

        mpz_set (R[0], R[n]);

        if (t == MISHMASH_DBCHAIN_BLOCK)
        {
          if (verbose)
            printf ("# %s: entering DBCHAIN block\n", __func__);
          ret = bytecode_dbchain_check_internal (++bc, &bc, R, R_len, verbose);
        }
        else if (t == MISHMASH_PRECOMP_BLOCK)
        {
          if (verbose)
            printf ("# %s: entering PRECOMP block\n", __func__);
          ret = bytecode_precomp_check_internal (++bc, &bc, R, R_len, verbose);
        }
        else if (t == MISHMASH_PRAC_BLOCK)
        {
          if (verbose)
            printf ("# %s: entering PRAC block\n", __func__);
          ret = bytecode_prac_check_internal (++bc, &bc, R, R_len, verbose);
        }
        else /* unknown bytecode */
        {
          printf ("# %s: error, unknown bytecode 0x%02x\n", __func__, *bc);
          ret = 1;
        }
      }
    }

    if (ret) /* was there an error ? */
      break;
    else
      bc++; /* go to next byte */
  }

  if (endptr != NULL)
    *endptr = bc;
  return ret;
}


/* Return nonzero if the bytecode does not produce E. Return 0 otherwise. */
int
bytecode_mishmash_check (bytecode_const bc, mpz_srcptr E, int verbose)
{
  int ret = 0;
  mpz_t *R = NULL;
  unsigned int R_nalloc = 0;

  /* parse first byte to alloc R */
  uint8_t t, n;
  bytecode_elt_split_4_4 (&t, &n, *bc);

  if (t != MISHMASH_INIT)
  {
    printf ("# %s: error, MISHMASH bytecode must start with 0x%1xn, "
            "not 0x%02x\n", __func__, MISHMASH_INIT, *bc);
    ret = 1;
  }
  else
  {
    R_nalloc = n+2;
    R = (mpz_t *) malloc (R_nalloc * sizeof (mpz_t));
    FATAL_ERROR_CHECK (R == NULL, "could not malloc R");
    for (unsigned int i = 0; i < R_nalloc; i++)
      mpz_init (R[i]);

    mpz_set_ui (R[1], 1); /* starting point go into R[1] at init */

    ret = bytecode_mishmash_check_internal (bc, NULL, R, R_nalloc, verbose);
  }

  if (ret == 0 && mpz_cmp (R[1], E) != 0) /* output is in R[1] */
  {
    gmp_printf ("# %s: failure:\n Expected %Zd\n Got %Zd\n", __func__, E, R[1]);
    ret = 1;
  }

  if (ret == 0 && verbose)
    printf ("# %s: success\n", __func__);

  /* clear mpz_t's */
  for (unsigned int i = 0; i < R_nalloc; i++)
    mpz_clear (R[i]);

  /* free array */
  free (R);

  return ret;
}
