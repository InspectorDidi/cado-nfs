#ifndef FACUL_H
#define FACUL_H

#include <stdio.h>
#include <stdint.h>
#include <gmp.h>
//{{to define modset_t
#include "modredc_ul.h"
#include "modredc_15ul.h"
#include "modredc_2ul2.h"
#include "mod_mpz.h"
//}}

#define PM1_METHOD 1
#define PP1_27_METHOD 2
#define PP1_65_METHOD 3
#define EC_METHOD 4

#define FACUL_NOT_SMOOTH (-1)

#define STATS_LEN 128

#define NB_MAX_METHODS 200 /* We authorize at most NB_MAX_METHOD
			     differents methods in our strategies */

typedef struct {
  long method; /* Which method to use (P-1, P+1 or ECM) */
  void *plan;  /* Parameters for that method */
  int side; /* To know on which side this method will be apply! */
  int is_the_last; /* To know if this method is the last on its side
		      (used when you chain methods).  */
} facul_method_t;


/* All prime factors in the input number must be > fb. A factor of the 
   input number is assumed to be prime if it is < fb^2.
   The input number is taken to be not smooth if it has a 
   prime factor > 2^lpb. */

typedef struct {
  unsigned long lpb;        /* Large prime bound 2^lpb */
  double assume_prime_thresh; /* The factor base bound squared.
                               We assume that primes <= fbb have already been 
                               removed, thus any factor <= assume_prime_thresh 
                               is assumed prime without further test. */
  double BBB;               /* The factor base bound cubed. */
  facul_method_t *methods;  /* List of methods to try */
} facul_strategy_t;




/*
  This is the same structure than facul_strategy_t but now our
  strategy will be applied on both sides.
*/
typedef struct {
  unsigned long lpb[2];        /* Large prime bound 2^lpb */
  double assume_prime_thresh[2]; /* The factor base bound squared.
                                We assume that primes <= fbb have already been 
				removed, thus any factor <= assume_prime_thresh 
				is  assumed prime without further test. */
  double BBB[2];               /* The factor base bound cubed. */
  facul_method_t *methods;  /* List of methods to try */
} facul_both_strategy_t;


typedef struct {
  int B1;
  int B2;
  int method;
  void *plan;
} precompute_plan_t;


typedef struct {
  facul_both_strategy_t** strategy;
  precompute_plan_t* plan;
  unsigned int mfb[2];
} facul_strategies_t;




typedef struct {
  /* The arith variable tells which modulus type has been initialised for 
     arithmetic. It has a value of CHOOSE_NONE if no modulus currently 
     initialised. */
  enum {
      CHOOSE_NONE,
      CHOOSE_UL,
      //#if     MOD_MAXBITS > MODREDCUL_MAXBITS
      CHOOSE_15UL,
      ////#endif
//#if     MOD_MAXBITS > MODREDC15UL_MAXBITS
      CHOOSE_2UL2,
//#endif
//#if     MOD_MAXBITS > MODREDC2UL2_MAXBITS
      CHOOSE_MPZ,
//#endif
  } arith;

  modulusredcul_t m_ul;
//#if     MOD_MAXBITS > MODREDCUL_MAXBITS
  modulusredc15ul_t m_15ul;
//#endif
//#if     MOD_MAXBITS > MODREDC15UL_MAXBITS
  modulusredc2ul2_t m_2ul2;
//#endif
//#if     MOD_MAXBITS > MODREDC2UL2_MAXBITS
  modulusmpz_t m_mpz;
//#endif
} modset_t;


void
modset_clear (modset_t *);


int nb_curves (unsigned int);
facul_strategy_t * facul_make_strategy (unsigned long, unsigned int, int, int);
void facul_clear_strategy (facul_strategy_t *);
void facul_print_stats (FILE *);
int facul (unsigned long *, const mpz_t, const facul_strategy_t *);

facul_strategies_t* facul_make_strategies (unsigned long, unsigned int,
					   unsigned int, unsigned long,
					   unsigned int, unsigned int,
					   FILE*, const int);

void facul_clear_strategies (facul_strategies_t*);

int
facul_fprint_strategies (FILE*, facul_strategies_t* );


void 
modset_clear (modset_t *modset);

int*
facul_both (unsigned long**, mpz_t* ,
	    const facul_strategies_t *, int*);

#endif /* FACUL_H */
