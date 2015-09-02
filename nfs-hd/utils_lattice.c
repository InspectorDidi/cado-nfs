#include "cado.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include "utils_lattice.h"
#include "ideal.h"
#include "utils_mpz.h"
#include "sieving_bound.h"
#include "mat_double.h"
#include "double_vector.h"
#include "list_int64_vector.h"
#include "list_double_vector.h"
#include "list_int64_vector_index.h"
#include "gcd.h"
#include "utils_int64.h"

/*
 * Compute angle between two vectors.
 *
 * v0: first vector.
 * v1: second vector.
 */
static double angle_2_coordinate(int64_vector_srcptr v0, int64_vector_srcptr v1)
{
  ASSERT(v0->dim == v1->dim);
  ASSERT(v0->dim >= 2);

#ifndef NDEBUG
  for (unsigned int i = 2; i < v0->dim; i++) {
    ASSERT(v0->c[i] == v1->c[i]);
  } 
#endif // NDEBUG

  double dot = 0;
  for (unsigned int i = 0; i < 2; i++) {
    dot += (double)(v0->c[i] * v1->c[i]);
  }
  dot = dot / (sqrt((double)(v0->c[0] * v0->c[0] + v0->c[1] * v0->c[1])));
  dot = dot / (sqrt((double)(v1->c[0] * v1->c[0] + v1->c[1] * v1->c[1])));
  return acos(dot);
}

/*
 * Compute the determinant of a 2 * 2 matrix.
 */
MAYBE_UNUSED static int determinant2(mat_int64_srcptr matrix)
{
  return (int) matrix->coeff[1][1] * matrix->coeff[2][2] - matrix->coeff[2][1] *
    matrix->coeff[1][2];
}

int gauss_reduction(int64_vector_ptr v0, int64_vector_ptr v1,
    mat_int64_ptr U, int64_vector_srcptr v0_root, int64_vector_srcptr v1_root)
{
  //Verify that the vector are in the same plane.
  ASSERT(v0_root->dim == v1_root->dim);
  ASSERT(v0_root->dim >= 2);
#ifndef NDEBUG
  for (unsigned int i = 3; i < v1->dim; i++) {
    ASSERT(v0_root->c[i] == v1_root->c[i]);
  }

  if (U != NULL) {
    ASSERT(U->NumRows == U->NumCols);
    ASSERT(U->NumRows == 2);
  }

  int64_vector_t v0_tmp;
  int64_vector_init(v0_tmp, v0->dim);
  int64_vector_set(v0_tmp, v0);

  int64_vector_t v1_tmp;
  int64_vector_init(v1_tmp, v1->dim);
  int64_vector_set(v1_tmp, v1);
#endif // NDEBUG
  ASSERT(v0->dim == v1->dim);
  ASSERT(v0_root->dim >= v1->dim);

  int64_vector_set(v0, v0_root);
  int64_vector_set(v1, v1_root);

  //determinant of U, 0 if U is NULL.
  int det = 0;
  
  double B0 = int64_vector_norml2sqr(v0);
  double m = (double)int64_vector_dot_product(v0, v1) / B0;
  for (unsigned int i = 0; i < 2; i++) {
    v1->c[i] = v1->c[i] - (int64_t)round(m) * v0->c[i];
  }

  if (U != NULL) {
    U->coeff[1][1] = 1;
    U->coeff[1][2] = -(int64_t)round(m);
    U->coeff[2][1] = 0;
    U->coeff[2][2] = 1;
    det = 1;
  }

  double B1 = int64_vector_norml2sqr(v1);

  while (B1 < B0) {
    int64_vector_swap(v0, v1);
    B0 = B1;
    m = (double)int64_vector_dot_product(v0, v1) / B0;
    for (unsigned int i = 0; i < 2; i++) {
      v1->c[i] = v1->c[i] - (int64_t)round(m) * v0->c[i];
    }

    //Update U by U = U * transformation_matrix.
    if (U != NULL) {
      int64_t tmp = U->coeff[1][2];
      U->coeff[1][2] = U->coeff[1][1] - (int64_t)round(m) * U->coeff[1][2];
      U->coeff[1][1] = tmp;
      tmp = U->coeff[2][2];
      U->coeff[2][2] = U->coeff[2][1] - (int64_t)round(m) * U->coeff[2][2];
      U->coeff[2][1] = tmp;
      det = -det;
    }

    B1 = int64_vector_norml2sqr(v1);

    //To have an acute basis.
  }

  if (M_PI_2 < angle_2_coordinate(v0, v1))
  {
    for (unsigned int i = 0; i < 2; i++) {
      v1->c[i] = -v1->c[i];
    }

    if (U != NULL) {
      U->coeff[1][2] = -U->coeff[1][2];
      U->coeff[2][2] = -U->coeff[2][2];
      det = -det;
    }
  }

#ifndef NDEBUG
  if (U != NULL) {
    ASSERT(v0_tmp->c[0] * U->coeff[1][1] + v1_tmp->c[0] * U->coeff[2][1]
        == v0->c[0]);
    ASSERT(v0_tmp->c[1] * U->coeff[1][1] + v1_tmp->c[1] * U->coeff[2][1]
        == v0->c[1]);
    ASSERT(v0_tmp->c[0] * U->coeff[1][2] + v1_tmp->c[0] * U->coeff[2][2]
        == v1->c[0]);
    ASSERT(v0_tmp->c[1] * U->coeff[1][2] + v1_tmp->c[1] * U->coeff[2][2]
        == v1->c[1]);
    ASSERT(determinant2(U) == det);
    ASSERT(ABS(det) == 1);
  }

  int64_vector_clear(v0_tmp);
  int64_vector_clear(v1_tmp);
#endif // NDEBUG

  return det;
}

int gauss_reduction_zero(int64_vector_ptr v0, int64_vector_ptr v1,
    mat_int64_ptr U, int64_vector_srcptr v0_root, int64_vector_srcptr v1_root)
{
  int det = gauss_reduction(v0, v1, U, v0_root, v1_root);

  for (unsigned int i = 3; i < v1->dim; i++) {
    v0->c[i] = 0;
    v1->c[i] = 0;
  }

  return det; 
}

void skew_LLL(mat_int64_ptr MSLLL, mat_int64_srcptr M_root,
    int64_vector_srcptr skewness)
{
  ASSERT(skewness->dim == M_root->NumCols);
  ASSERT(M_root->NumRows == M_root->NumCols);
  ASSERT(MSLLL->NumRows == M_root->NumCols);
  ASSERT(MSLLL->NumRows == M_root->NumCols);

  mat_int64_t I_s;
  mat_int64_init(I_s, M_root->NumRows, M_root->NumCols);
  mat_int64_set_diag(I_s, skewness);

  mat_int64_t M;
  mat_int64_init(M, M_root->NumRows, M_root->NumCols);
  mat_int64_mul_mat_int64(M, I_s, M_root);
  
  mat_int64_t U;
  mat_int64_init(U, M_root->NumRows, M_root->NumCols);
  mat_int64_LLL_unimodular_transpose(U, M);
  mat_int64_mul_mat_int64(MSLLL, M_root, U);

  mat_int64_clear(U);
  mat_int64_clear(I_s);
  mat_int64_clear(M);
}

//TODO: why not merge with gauss reduction?
int reduce_qlattice(int64_vector_ptr v0, int64_vector_ptr v1,
    int64_vector_srcptr v0_root, int64_vector_srcptr v1_root, int64_t I)
{
  ASSERT(v0_root->c[1] == 0);
  ASSERT(v1_root->c[1] == 1);
  ASSERT(v0_root->dim == v1_root->dim);
  ASSERT(v0->dim == v1->dim);
  ASSERT(v0_root->dim == v1->dim);
  ASSERT(I > 0);
#ifndef NDEBUG
  for (unsigned int i = 3; i < v1->dim; i++) {
    ASSERT(v0_root->c[i] == v1_root->c[i]);
  }
#endif // NDEBUG

  int64_vector_set(v0, v0_root);
  int64_vector_set(v1, v1_root);

  int64_t a0 = -v0->c[0], b0 = v1->c[0], a1 = 0, b1 = 1, k;

  const int64_t hI = I;
  const int64_t mhI = -hI;

  while (b0 >= hI) {
    k = a0 / b0; a0 %= b0; a1 -= k * b1;

    if (a0 > mhI) {
      break;
    }
    k = b0 / a0; b0 %= a0; b1 -= k * a1;

    if (b0 < hI) {
      break;
    }
    k = a0 / b0; a0 %= b0; a1 -= k * b1;

    if (a0 > mhI) {
      break;
    }
    k = b0 / a0; b0 %= a0; b1 -= k * a1;

  }
  k = b0 - hI - a0;
  if (b0 > -a0) {
    if (!a0) {
      return 0;
    }
    k /= a0; b0 -= k * a0; b1 -= k * a1;

  } else {
    if (!b0) {
      return 0;
    }
    k /= b0; a0 += k * b0; a1 += k * b1;

  }

  ASSERT(a0 > mhI);
  ASSERT(0 >= a0);
  ASSERT(0 <= b0);
  ASSERT(b0 < hI);
  ASSERT(a1 > 0);
  ASSERT(b1 > 0);

  v0->c[0] = a0;
  v0->c[1] = a1;
  v1->c[0] = b0;
  v1->c[1] = b1;

  return 1;
}

int reduce_qlattice_zero(int64_vector_ptr v0, int64_vector_ptr v1,
    int64_vector_srcptr v0_root, int64_vector_srcptr v1_root, int64_t I)
{
  int res = reduce_qlattice(v0, v1, v0_root, v1_root, I);

  for (unsigned int i = 3; i < v1->dim; i++) {
    v0->c[i] = 0;
    v1->c[i] = 0;
  }

  return res;
}

/*
 * For instance, it is SV3.
 */
void SV4(list_int64_vector_ptr SV, int64_vector_srcptr v0_root,
    int64_vector_srcptr v1_root, int64_vector_srcptr v2)
{
  ASSERT(v0_root->dim == v1_root->dim);
#ifndef NDEBUG
  for (unsigned int j = 3; j < v0_root->dim; j++) {
    ASSERT(v0_root->c[j] == v1_root->c[j]);
    ASSERT(v0_root->c[j] == 0);
  }
#endif // NDEBUG
  ASSERT(v2->c[1] == 0);
  ASSERT(v2->c[2] == 1);

  int64_vector_t u;
  int64_vector_init(u, v0_root->dim);

  int64_vector_t v0;
  int64_vector_t v1;
  int64_vector_init(v0, v0_root->dim);
  int64_vector_init(v1, v1_root->dim);
  int64_vector_set(v0, v0_root);
  int64_vector_set(v1, v1_root);

  mat_int64_t U;
  mat_int64_init(U, 2, 2);

  int det = gauss_reduction(v0, v1, U, v0, v1);
  ASSERT(det != 0);

  double target0 = (double)v2->c[0] / (double)v0_root->c[0];
  //U^-1 * [target0, 0] = (v2_new_base_x, v2_new_base_y).
  double v2_new_base_x = (double)det * (double)U->coeff[2][2] * target0;
  double v2_new_base_y = -(double)det * (double)U->coeff[2][1] * target0;
  int64_t a = (int64_t)round(v2_new_base_x);
  int64_t b = (int64_t)round(v2_new_base_y);

  for (unsigned int i = 0; i < 2; i++) {
    u->c[i] = a * v0->c[i] + b * v1->c[i];
  }
  u->c[2] = 0;
  list_int64_vector_add_int64_vector(SV, u);

  //Build a triangle around the projection of v2 in the plane z = 0.
  if (v2_new_base_x - (double)a < 0) {
    for (unsigned int i = 0; i < 2; i++) {
      u->c[i] = SV->v[0]->c[i] - v0->c[i];
    }
    u->c[2] = 0;
    list_int64_vector_add_int64_vector(SV, u);
  } else {
    for (unsigned int i = 0; i < 2; i++) {
      u->c[i] = SV->v[0]->c[i] + v0->c[i];
    }
    u->c[2] = 0;
    list_int64_vector_add_int64_vector(SV, u);
  }

  if (v2_new_base_y - (double)b < 0) {
    for (unsigned int i = 0; i < 2; i++) {
      u->c[i] = SV->v[0]->c[i] - v1->c[i];
    }
    u->c[2] = 0;
    list_int64_vector_add_int64_vector(SV, u);
  } else {
    for (unsigned int i = 0; i < 2; i++) {
      u->c[i] = SV->v[0]->c[i] + v1->c[i];
    }
    u->c[2] = 0;
    list_int64_vector_add_int64_vector(SV, u);
  }

#ifndef NDEBUG
  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, v2->dim);
  int64_vector_set(v_tmp, v2);
  for (unsigned int i = 2; i < v_tmp->dim; i++) {
    v_tmp->c[i] = 0;
  }
  ASSERT(int64_vector_in_polytop_list_int64_vector(v_tmp, SV) == 1);
  int64_vector_clear(v_tmp);
#endif // NDEBUG

  for (unsigned int i = 0; i < 3; i++) {
    int64_vector_sub(SV->v[i], v2, SV->v[i]);
  }

  int64_vector_clear(v0);
  int64_vector_clear(v1);
  int64_vector_clear(u);
  mat_int64_clear(U);
}

void SV4_Mqr(list_int64_vector_ptr SV, mat_int64_srcptr Mqr)
{
  ASSERT(Mqr->NumRows == Mqr->NumCols);
  ASSERT(Mqr->NumRows == 3);

  int64_vector_t * v = malloc(sizeof(int64_vector_t) * Mqr->NumRows);
  for (unsigned int i = 0; i < Mqr->NumCols; i++) {
    int64_vector_init(v[i], Mqr->NumRows);
    mat_int64_extract_vector(v[i], Mqr, i);
  }
  SV4(SV, v[0], v[1], v[2]);

  for (unsigned int i = 0; i < Mqr->NumCols; i++) {
    int64_vector_clear(v[i]);
  }
  free(v);
}

/*
 * Return the gap between the x coordinate and the closest border of the sieving
 * region defined by the sieving bound H.
 *
 * v: current vector.
 * H: sieving bound.
 */
/*static uint64_t difference_bound_x(int64_vector_srcptr v,*/
    /*sieving_bound_srcptr H)*/
/*{*/
  /*return (uint64_t) MIN(ABS(-(int64_t)H->h[0] - v->c[0]),*/
      /*ABS((int64_t)H->h[0] - 1 - v->c[0]));*/
/*}*/

static int64_t compute_k(int64_t x, int64_t xfk, int64_t H0)
{
  /*printf("%" PRId64 ", %" PRId64 ", %" PRId64 "\n", x, xfk, H0);*/
  /*printf("%f\n", (double)(-H0 - x) / (double)xfk);*/
  /*printf("%f\n", (double)(H0 - 1 - x) / (double)xfk);*/

  /*ASSERT(ceil((double)(-H0 - x) / (double)xfk) ==*/
      /*floor((double)(H0 - 1 - x) / (double)xfk - 1));*/

  return (int64_t)ceil((double)(-H0 - x) / (double)xfk);
}

void add_FK_vector(int64_vector_ptr v, list_int64_vector_srcptr list,
    int64_vector_srcptr e0, int64_vector_srcptr e1, sieving_bound_srcptr H)
{
  ASSERT(v->dim == 3);

  int64_vector_t v_use;
  int64_vector_init(v_use, e0->dim);

  //Select the Franke-Kleinjung vector which have the larger x coordinate.
  if (-e0->c[0] > e1->c[0]) {
    ASSERT(v->dim == e0->dim);
    //Set v_use = -e0;
    for (unsigned int i = 0; i < e0->dim; i++) {
      v_use->c[i] = -e0->c[i];
    }
  } else {
    int64_vector_set(v_use, e1);
  }

  ASSERT(v_use->c[0] > 0);

  //Compute the distance.
  unsigned int pos = 0;
  int64_t k = compute_k(list->v[0]->c[0], v_use->c[0], (int64_t)H->h[0]);

  ASSERT(list->v[0]->c[0] + k * v_use->c[0] < (int64_t)H->h[0]);
  ASSERT(list->v[0]->c[0] + k * v_use->c[0] >= -(int64_t)H->h[0]);

  uint64_t min_y = (uint64_t)ABS(list->v[0]->c[1] + k * v_use->c[1]);

  for (unsigned int i = 1; i < list->length; i++) {
    int64_t k_tmp = compute_k(list->v[i]->c[0], v_use->c[0], (int64_t)H->h[0]);
   
    ASSERT(list->v[i]->c[0] + k_tmp * v_use->c[0] < (int64_t)H->h[0]);
    ASSERT(list->v[i]->c[0] + k_tmp * v_use->c[0] >= -(int64_t)H->h[0]);

    uint64_t tmp = (uint64_t)ABS(list->v[i]->c[1] + k_tmp * v_use->c[1]);
    if (tmp < min_y) {
      min_y = tmp;
      pos = i;
      k = k_tmp;
    }
  }

  int64_vector_set(v, list->v[pos]);

  for (unsigned int i = 0; i < 2; i++) {
    v->c[i] = v->c[i] + k * v_use->c[i];
  }

  ASSERT(v->c[0] < (int64_t)H->h[0]);
  ASSERT(v->c[0] >= -(int64_t)H->h[0]);

  int64_vector_clear(v_use);
}

unsigned int enum_pos_with_FK(int64_vector_ptr v, int64_vector_srcptr v_old,
    int64_vector_srcptr v0, int64_vector_srcptr v1, int64_t A, int64_t I)
{
  //v0 = (alpha, beta) and v1 = (gamma, delta)
  ASSERT(I > 0);
  ASSERT(v0->c[0] > -I);
  ASSERT(v0->c[0] <= 0);
  ASSERT(v0->c[1] > 0);
  ASSERT(v1->c[0] < I);
  ASSERT(v1->c[0] >= 0);
  ASSERT(v1->c[1] > 0);
  ASSERT(v1->c[0] - v0->c[0] >= I);
#ifndef NDEBUG
  for (unsigned int j = 3; j < v0->dim; j++) {
    ASSERT(v0->c[j] == 0);
    ASSERT(v1->c[j] == 0);
  }
#endif // NDEBUG
  ASSERT(v_old->c[0] < A + I);
  ASSERT(v_old->c[0] >= A);

  if (v_old->c[0] >= A - v0->c[0]) {
    int64_vector_add(v, v_old, v0);
    return 0;
  } else if (v_old->c[0] < A + I - v1->c[0]) {
    int64_vector_add(v, v_old, v1);
    return 1;
  } else {
    int64_vector_add(v, v_old, v0);
    int64_vector_add(v, v, v1);
    return 2;
  }
}

unsigned int enum_neg_with_FK(int64_vector_ptr v, int64_vector_srcptr v_old,
    int64_vector_srcptr v0, int64_vector_srcptr v1, int64_t A, int64_t I)
{
  //v0 = (alpha, beta) and v1 = (gamma, delta)
  ASSERT(I > 0);
  ASSERT(v0->c[0] > -I);
  ASSERT(v0->c[0] <= 0);
  ASSERT(v0->c[1] > 0);
  ASSERT(v1->c[0] < I);
  ASSERT(v1->c[0] >= 0);
  ASSERT(v1->c[1] > 0);
  ASSERT(v1->c[0] - v0->c[0] >= I);
#ifndef NDEBUG
  for (unsigned int j = 3; j < v0->dim; j++) {
    ASSERT(v0->c[j] == 0);
    ASSERT(v1->c[j] == 0);
  }
#endif // NDEBUG
  ASSERT(v_old->c[0] > -A - I);
  ASSERT(v_old->c[0] <= -A);

  if (v_old->c[0] <= -A + v0->c[0]) {
    int64_vector_sub(v, v_old, v0);
    return 0;
  } else if (v_old->c[0] > -A - I + v1->c[0]) {
    int64_vector_sub(v, v_old, v1);
    return 1;
  } else {
    int64_vector_sub(v, v_old, v0);
    int64_vector_sub(v, v, v1);
    return 2;
  }
}

//TODO: is it a good idea to retun 0 if fail?
//TODO: index is not signed, why?
uint64_t index_vector(int64_vector_srcptr v, sieving_bound_srcptr H,
    uint64_t number_element)
{
  uint64_t index = 0;

  int64_vector_t v_start;
  int64_vector_init(v_start, v->dim);
  int64_vector_set_zero(v_start);
  int64_vector_t v_end;
  int64_vector_init(v_end, v->dim);
  int64_vector_set(v_end, v);
  for (unsigned int i = 0; i < v->dim - 1; i++) {
    if (v->c[i] >= 0) {
      v_start->c[i] = -(int64_t)H->h[i];
    } else {
      v_start->c[i] = (int64_t)(H->h[i] - 1);
    }
    v_end->c[i] = v_start->c[i] + v->c[i];
  }
  if (int64_vector_in_sieving_region(v_end, H)) {
    uint64_t index_end = array_int64_vector_index(v_end, H, number_element);
    uint64_t index_start =
      array_int64_vector_index(v_start, H, number_element);
    if (index_end > index_start) {
      index = index_end - index_start;
    } else {
      index = index_start - index_end;
    }
  } else {
    index = 0;
  }

  ASSERT(index < number_element);
  int64_vector_clear(v_start);
  int64_vector_clear(v_end);

  return index;
}

void coordinate_FK_vector(uint64_t * coord_v0, uint64_t * coord_v1,
    int64_vector_srcptr v0, int64_vector_srcptr v1, sieving_bound_srcptr H,
    uint64_t number_element)
{
  ASSERT(2*H->h[0] > 0);
  ASSERT(v0->c[0] > -2*(int64_t)H->h[0]);
  ASSERT(v0->c[0] <= 0);
  ASSERT(v0->c[1] > 0);
  ASSERT(v1->c[0] < 2*(int64_t)H->h[0]);
  ASSERT(v1->c[0] >= 0);
  ASSERT(v1->c[1] > 0);
  ASSERT(v1->c[0] - v0->c[0] >= 2*(int64_t)H->h[0]);
#ifndef NDEBUG
  for (unsigned int j = 3; j < v0->dim; j++) {
    ASSERT(v0->c[j] == 0);
    ASSERT(v1->c[j] == 0);
  }
#endif // NDEBUG

  int64_vector_t e0;
  int64_vector_init(e0, v0->dim);
  int64_vector_set(e0, v0);
  int64_vector_t e1;
  int64_vector_init(e1, v1->dim);
  int64_vector_set(e1, v1);
 
  e0->c[0] = e0->c[0] + (int64_t)(H->h[0] - 1);
  e1->c[0] = e1->c[0] - (int64_t)(H->h[0]);
  e0->c[1] = e0->c[1] - (int64_t)(H->h[1]);
  e1->c[1] = e1->c[1] - (int64_t)(H->h[1]);

  if (int64_vector_in_sieving_region(e0, H)) {
    * coord_v0 = array_int64_vector_index(e0, H, number_element) - (2 *
        (int64_t) H->h[0] - 1);
  } else {
    * coord_v0 = 0; 
  }
  if (int64_vector_in_sieving_region(e1, H)) {
    * coord_v1 = array_int64_vector_index(e1, H, number_element);
  } else {
    * coord_v1 = 0;
  }

  ASSERT(* coord_v0 == index_vector(v0, H, number_element));
  ASSERT(* coord_v1 == index_vector(v1, H, number_element));

  int64_vector_clear(e0);
  int64_vector_clear(e1);
}

/*
 * Return the position of the vector with a x coordinate minimal, according to
 * the classification defined by the stamp array and the value of the
 * classification val_stamp.
 *
 * SV: list of vector.
 * H: sieving bound.
 * stamp: array with length equal to SV, gives the classification of the
 *  vectors.
 * val_stamp: value we want for the stamp of the vector.
 */
unsigned int find_min_y(list_int64_vector_srcptr SV, unsigned char * stamp,
    unsigned char val_stamp)
{
  int64_t y = -1; 
  unsigned int pos = 0;

  for (unsigned int i = 0; i < SV->length; i++) {
    if (stamp[i] == val_stamp) {
      //Find the closest y to 0.
      //TODO: warning, with this, we forget the problem in H1 - 1.
      int64_t tmp = ABS(SV->v[i]->c[1]);
      if (y == -1) {
        y = tmp;
      }
      if (y >= tmp) {
        y = tmp;
        pos = i;
      }
    }
  }
  return pos;
}

void plane_sieve_next_plane(int64_vector_ptr vs, list_int64_vector_srcptr SV,
    int64_vector_srcptr e0, int64_vector_srcptr e1, sieving_bound_srcptr H)
{
  // Contain all the possible vectors to go from z=d to z=d+1.
  list_int64_vector_t list;
  list_int64_vector_init(list, 3);
  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, SV->v[0]->dim);
  /*
   * 0: in the sieving region.
   * 1: in [-H_0, H_0[
   * 2: outside
   */
  unsigned char * assert = malloc(sizeof(unsigned char) * SV->length);
  unsigned char found = 2;

  for (unsigned int i = 0; i < SV->length; i++) {
    int64_vector_add(v_tmp, vs, SV->v[i]);
    list_int64_vector_add_int64_vector(list, v_tmp);

    if (-(int64_t) H->h[0] <= v_tmp->c[0] && (int64_t)H->h[0] > v_tmp->c[0]) {
      if (-(int64_t) H->h[1] <= v_tmp->c[1] && (int64_t)H->h[1] > v_tmp->c[1])
      {
        assert[i] = 0;
        found = 0;
      } else {
        assert[i] = 1;
        if (found > 1) {
          found = 1;
        }
      }
    } else {
      assert[i] = 2;
    }
  }
  int64_vector_clear(v_tmp);

  if (found < 2) {
    unsigned pos = find_min_y(list, assert, found);
    int64_vector_set(vs, list->v[pos]);
  } else {
    ASSERT(found == 2);
    add_FK_vector(vs, list, e0, e1, H);
  }
  
  ASSERT(vs->c[0] < (int64_t)H->h[0]);
  ASSERT(vs->c[0] >= -(int64_t)H->h[0]);
  free(assert);
  
  list_int64_vector_clear(list);
}

void double_vector_gram_schmidt(list_double_vector_ptr list_new,
    mat_double_ptr m, list_double_vector_srcptr list_old)
{
  for (unsigned int i = 0; i < list_old->length; i++) {
    list_double_vector_add_double_vector(list_new, list_old->v[i]);
  }

  double_vector_t v_tmp;
  double_vector_init(v_tmp, list_new->v[0]->dim);
  for (unsigned int i = 0; i < list_new->length; i++) {
    m->coeff[i + 1][i + 1] = 1;
  }

  for (unsigned int i = 0; i < list_new->length; i++) {
    for (unsigned int j = 0; j < i; j++) {
      m->coeff[i + 1][j + 1] = double_vector_orthogonal_projection(v_tmp,
          list_new->v[j], list_new->v[i]); 
      double_vector_sub(list_new->v[i], list_new->v[i], v_tmp);
    }
  }

  double_vector_clear(v_tmp);
}

#ifdef SPACE_SIEVE_REDUCE_QLATTICE

static int reduce_qlattice_output(list_int64_vector_ptr list, int64_t r,
    int64_t I)
{
  //list->v[0]->c[0] <= 0, list->v[1]->c[0] >= 0
  //index: where is the vector of Franke-Kleinjung.
  unsigned int index = 0;
  unsigned int index_new = 1;
  if (list->v[0]->c[0] > 0) {
    index = 1;
    index_new = 0;
  }

  ASSERT(list->v[index]->c[1] >= 0);
  ASSERT(I > 0);
#ifndef NDEBUG
  for (unsigned int i = 3; i < list->v[index]->dim; i++) {
    ASSERT(0 == list->v[index]->c[i]);
  }
#endif // NDEBUG

  //TODO: not sure that only this case can fail.
  if (list->v[index]->c[0] == 0 || list->v[index]->c[1] == 0) {
    return 0;
  }

  int64_t x = 0;
  int64_t y = 0;

  if (index == 0) {
    ASSERT(list->v[0]->c[0] < 0);

    int64_t g = 0;
    int64_xgcd(&g, &x, &y, list->v[0]->c[1], -list->v[0]->c[0]);

    if (g == -1) {
      g = 1;
      x = -x;
      y = -y;
    }

    ASSERT(g == 1);
  } else {
    ASSERT(list->v[0]->c[0] > 0);

    int64_t g = 0;
    int64_xgcd(&g, &x, &y, -list->v[0]->c[1], list->v[0]->c[0]);

    if (g != 1) {
      g = -g;
      x = -x;
      y = -y;
    }

    ASSERT(g == 1);
  }

  //Beware of overflow
  x = x * r;
  y = y * r;

  int64_t k = 0;
  if (index == 0) {
    //k = ceil((I - x) / v[0])
    k = siceildiv(I - x, list->v[0]->c[0]);
    /*k = (int64_t) ceil(((double)I - (double)x) / (double)list->v[0]->c[0]);*/
    if (I - x == k * list->v[0]->c[0]) {
      k++;
    }
  } else {
    //k = ceil((-I - x) / v[0])
    k = siceildiv(-I - x, list->v[1]->c[0]);
    /*k = (int64_t) ceil((-(double)I - (double)x) / (double)list->v[1]->c[0]);*/
    if (-I - x == k * list->v[0]->c[0]) {
      k++;
    }
  }
  list->v[index_new]->c[0] = x + k * list->v[index]->c[0];
  list->v[index_new]->c[1] = y + k * list->v[index]->c[1];

#ifndef NDEBUG
  ASSERT(list->v[0]->c[0] * list->v[1]->c[1] - list->v[0]->c[1]
    * list->v[1]->c[0] == -r);
#endif // NDEBUG

  return 1;
}
#endif // SPACE_SIEVE_REDUCE_QLATTICE

static int space_sieve_good_vector(int64_vector_srcptr v,
    sieving_bound_srcptr H)
{
  ASSERT(v->dim == H->t);

  for (unsigned int i = 0; i < v->dim - 1; i++) {
    if ((-2 * (int64_t)H->h[i]) >= v->c[i] ||
        (2 * (int64_t)H->h[i]) <= v->c[i]) {
      return 0;
    }
  }
  if (0 > v->c[v->dim - 1] || (int64_t)H->h[v->dim - 1] <= v->c[v->dim - 1]) {
    return 0;
  }
  return 1; 
}

static int int64_vector_in_list_zero(int64_vector_srcptr v_tmp,
    list_int64_vector_index_srcptr list_zero)
{
  for (unsigned int i = 0; i < list_zero->length; i++) {
    for (unsigned int j = 0; j < v_tmp->dim - 1; j++) {
      //TODO: ABS is no longer required.
      if (ABS(v_tmp->c[j]) == ABS(list_zero->v[i]->vec->c[j])) {
        return 1;
      }
    }
  }
  return 0;
}

static unsigned int good_vector_in_list(list_int64_vector_index_ptr list,
    list_int64_vector_index_ptr list_zero, int64_vector_srcptr v,
    sieving_bound_srcptr H, int64_t number_element)
{
  unsigned int vector_1 = 0;
  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, v->dim);
  int64_vector_set(v_tmp, v);
  int64_vector_reduce(v_tmp, v_tmp);
  if (space_sieve_good_vector(v_tmp, H)) {
    if (v_tmp->c[2] == 0) {
      if (v_tmp->c[1] != 0 || v_tmp->c[0] != 0) {
        if (v_tmp->c[1] < 0) {
          v_tmp->c[0] = -v_tmp->c[0];
          v_tmp->c[1] = -v_tmp->c[1];
        }
        if (!int64_vector_in_list_zero(v_tmp, list_zero)) {
          list_int64_vector_index_add_int64_vector_index(list_zero,
              v_tmp, index_vector(v_tmp, H, number_element));
        }
      }
    } else {
      list_int64_vector_index_add_int64_vector_index(list, v_tmp, 0);
      if (v_tmp->c[2] == 1) {
        vector_1 = 1;
      }
    }
  }
  int64_vector_clear(v_tmp);
  return vector_1;
}

//TODO: change that.
static unsigned int space_sieve_linear_combination(
    list_int64_vector_index_ptr list, list_int64_vector_index_ptr list_zero,
    mat_int64_srcptr MSLLL, sieving_bound_srcptr H, uint64_t number_element)
{
  unsigned int vector_1 = 0;

  list_int64_vector_t list_tmp;
  list_int64_vector_init(list_tmp, 3);
  list_int64_vector_extract_mat_int64(list_tmp, MSLLL);

  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, MSLLL->NumRows);
  int64_vector_t v_tmp_n;
  int64_vector_init(v_tmp_n, MSLLL->NumRows);
  int64_vector_set_zero(v_tmp);
  int64_t l = -1;
  int64_t m = -1;
  int64_t n = -2;
  //v_tmp = l * v[0] + n * v[1] + 1 * v[2], l in [-1, 2[, m in [-1, 2[, n in
  //[-1, 2[.
  int64_vector_sub(v_tmp, v_tmp, list_tmp->v[0]);
  int64_vector_sub(v_tmp, v_tmp, list_tmp->v[1]);
  int64_vector_addmul(v_tmp, v_tmp, list_tmp->v[2], -2);

  //14 = 3^3 / 2 + 1;
  for (unsigned int i = 0; i < 14; i++) {
    if (n < 1) {
      n++;
      int64_vector_add(v_tmp, v_tmp, list_tmp->v[2]);
    } else {
      n = -1;
      int64_vector_addmul(v_tmp, v_tmp, list_tmp->v[2], -2);
      if (m < 1) {
        m++;
        int64_vector_add(v_tmp, v_tmp, list_tmp->v[1]);
      } else {
        m = -1;
        int64_vector_addmul(v_tmp, v_tmp, list_tmp->v[1], -2);
        l = l + 1;
        int64_vector_add(v_tmp, v_tmp, list_tmp->v[0]);
      }
    }

#ifndef NDEBUG
  int64_vector_t v_tmp1;
  int64_vector_init(v_tmp1, MSLLL->NumRows);
  int64_vector_set_zero(v_tmp1);
  int64_vector_addmul(v_tmp1, v_tmp1, list_tmp->v[0], l); 
  int64_vector_addmul(v_tmp1, v_tmp1, list_tmp->v[1], m); 
  int64_vector_addmul(v_tmp1, v_tmp1, list_tmp->v[2], n);
  ASSERT(int64_vector_equal(v_tmp, v_tmp1));
  int64_vector_clear(v_tmp1);
#endif // NDEBUG

    if (v_tmp->c[2] < 0) {
      for (unsigned int i = 0; i < v_tmp->dim; i++) {
        v_tmp_n->c[i] = -v_tmp->c[i];
      }
      vector_1 = good_vector_in_list(list, list_zero, v_tmp_n, H,
          number_element);
    } else {
      vector_1 = good_vector_in_list(list, list_zero, v_tmp, H,
          number_element);
    }
  }

  for (unsigned int i = 0; i < list_zero->length; i++) {
    ASSERT(int64_vector_gcd(list_zero->v[i]->vec) == 1);
  }

  int64_vector_clear(v_tmp);
  int64_vector_clear(v_tmp_n);
  list_int64_vector_clear(list_tmp);

  return vector_1;
}

#ifdef SPACE_SIEVE_ENTROPY
static void space_sieve_generate_new_vectors(
    list_int64_vector_index_ptr list, list_int64_vector_index_ptr list_zero,
    sieving_bound_srcptr H, uint64_t number_element)
{
  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, list->vector_dim);
  unsigned int ind = list->length - 1;
  for (unsigned int i = 0; i < ind; i++) {
    //v_tmp = l * v[0] + n * v[1] + m * v[2]
    int64_vector_set(v_tmp, list->v[ind]->vec);
    int64_vector_add(v_tmp, v_tmp, list->v[i]->vec);
    good_vector_in_list(list, list_zero, v_tmp, H, number_element);
    int64_vector_set(v_tmp, list->v[ind]->vec);
    int64_vector_sub(v_tmp, v_tmp, list->v[i]->vec);
    good_vector_in_list(list, list_zero, v_tmp, H, number_element);
  }

#ifndef NDEBUG
  for (unsigned int i = 0; i < list_zero->length; i++) {
    ASSERT(int64_vector_gcd(list_zero->v[i]->vec) == 1);
  }
#endif // NDEBUG

  int64_vector_clear(v_tmp);
}
#endif // SPACE_SIEVE_ENTROPY

/*
 * Compute g = a * u + b * v
 */
int int64_vector_in_sieving_region_dim(int64_vector_srcptr v,
    sieving_bound_srcptr H) {
  ASSERT(v->dim == H->t);

  for (unsigned int i = 0; i < v->dim - 1; i++) {
    if (-(int64_t)H->h[i] > v->c[i] || ((int64_t)H->h[i]) <= v->c[i]) {
      return 0;
    }
  }
  return 1; 
}

void plane_sieve_1_enum_plane_incomplete(int64_vector_ptr v_in,
    int64_vector_srcptr e0, int64_vector_srcptr e1, sieving_bound_srcptr H)
{
  int64_vector_t v;
  int64_vector_init(v, v_in->dim);
  int64_vector_set(v, v_in);
  //Perform Franke-Kleinjung enumeration.
  //x increases.
  while (v->c[1] < (int64_t)H->h[1]) {
    if (v->c[1] >= -(int64_t)H->h[1]) {
      int64_vector_set(v_in, v);
      int64_vector_clear(v);
      return;
    }
    enum_pos_with_FK(v, v, e0, e1, -(int64_t)H->h[0], 2 * (int64_t)
        H->h[0]);
  }

  //x decreases.
  int64_vector_set(v, v_in);
  enum_neg_with_FK(v, v, e0, e1, -(int64_t)H->h[0] + 1, 2 * (int64_t)H->h[0]);
  while (v->c[1] >= -(int64_t)H->h[1]) {
    if (v->c[1] < (int64_t)H->h[1]) {
      int64_vector_set(v_in, v);
      int64_vector_clear(v);
      return;
    }
    enum_neg_with_FK(v, v, e0, e1, -(int64_t)H->h[0] + 1, 2 *
        (int64_t) H->h[0]);
  }
  int64_vector_clear(v);
}

void plane_sieve_1_incomplete(int64_vector_ptr s_out, int64_vector_srcptr s,
    MAYBE_UNUSED mat_int64_srcptr Mqr, sieving_bound_srcptr H,
    list_int64_vector_srcptr list_FK, list_int64_vector_srcptr list_SV)
{
  ASSERT(Mqr->NumRows == Mqr->NumCols);
  ASSERT(Mqr->NumRows == 3);

  int64_vector_set(s_out, s);

  plane_sieve_next_plane(s_out, list_SV, list_FK->v[0], list_FK->v[1], H);
  //Enumerate the element of the sieving region.
  for (unsigned int d = (unsigned int) s->c[2] + 1; d < H->h[2]; d++) {
    plane_sieve_1_enum_plane_incomplete
      (s_out, list_FK->v[0], list_FK->v[1], H);
    if (int64_vector_in_sieving_region(s_out, H)) {
      return;
    }

    //Jump in the next plane.
    plane_sieve_next_plane(s_out, list_SV, list_FK->v[0], list_FK->v[1], H);
  }
}

unsigned int space_sieve_1_init(list_int64_vector_index_ptr list_vec,
    list_int64_vector_index_ptr list_vec_zero, ideal_1_srcptr r,
    mat_int64_srcptr Mqr, sieving_bound_srcptr H, uint64_t number_element)
{
  int64_vector_t skewness;
  int64_vector_init(skewness, 3);
  skewness->c[0] = 1;
  skewness->c[1] = 1;
  //TODO: bound?
  skewness->c[2] = (int64_t)(4 * H->h[0] * H->h[0] * H->h[0]) / (int64_t)r->ideal->r;

  mat_int64_t MSLLL;
  mat_int64_init(MSLLL, Mqr->NumRows, Mqr->NumCols);
  skew_LLL(MSLLL, Mqr, skewness);
  int64_vector_clear(skewness);

  for (unsigned int col = 1; col <= MSLLL->NumCols; col++) {
    if (MSLLL->coeff[3][col] < 0) {
      for (unsigned int row = 1; row <= MSLLL->NumRows; row++) {
        MSLLL->coeff[row][col] = - MSLLL->coeff[row][col];
      }
    }
  }
  
  //TODO: we must generate all the zero vectors!
  unsigned int vector_1 = space_sieve_linear_combination(
      list_vec, list_vec_zero, MSLLL, H, number_element);
  mat_int64_clear(MSLLL);

  list_int64_vector_index_sort_last(list_vec);

  return vector_1;
}

int space_sieve_1_plane_sieve_init(list_int64_vector_ptr list_SV,
    list_int64_vector_ptr list_FK, list_int64_vector_index_ptr list_vec,
    list_int64_vector_index_ptr list_vec_zero, MAYBE_UNUSED ideal_1_srcptr r,
    sieving_bound_srcptr H, mat_int64_srcptr Mqr,
    unsigned int vector_1, uint64_t number_element)
{

  int64_vector_t * vec = malloc(sizeof(int64_vector_t) * Mqr->NumRows);
  for (unsigned int i = 0; i < Mqr->NumCols; i++) {
    int64_vector_init(vec[i], Mqr->NumRows);
    mat_int64_extract_vector(vec[i], Mqr, i);
  }
  if (vector_1 && list_vec->length != 0) {
    unsigned int cpt = 0;
    while (cpt < list_vec->length && list_vec->v[cpt]->vec->c[2] < 2) {
      if (list_vec->v[cpt]->vec->c[2] == 1) {
        list_int64_vector_add_int64_vector(list_SV,
            list_vec->v[cpt]->vec);
      }
      cpt++;
    }
  } else {
    ASSERT(vector_1 == 0);
    SV4(list_SV, vec[0], vec[1], vec[2]);
  }
  int boolean = 0;
#ifdef SPACE_SIEVE_REDUCE_QLATTICE
  if (list_vec_zero->length == 0) {
    //Stupid add, just to declare this two vector.
    list_int64_vector_add_int64_vector(list_FK, vec[0]);
    list_int64_vector_add_int64_vector(list_FK, vec[1]);
    boolean = reduce_qlattice(list_FK->v[0], list_FK->v[1], vec[0],
        vec[1], (int64_t)(2 * H->h[0]));
  } else if (list_vec_zero->length == 1) {
    list_int64_vector_add_int64_vector(list_FK,
        list_vec_zero->v[0]->vec);
    list_int64_vector_add_int64_vector(list_FK,
        list_vec_zero->v[0]->vec);
    boolean = reduce_qlattice_output(list_FK, (int64_t)r->ideal->r,
        (int64_t)(2 * H->h[0]));
  } else {
    ASSERT(list_vec_zero->length == 2);
    if (0 >= list_vec_zero->v[0]->vec->c[0]) {
      list_int64_vector_add_int64_vector(list_FK,
          list_vec_zero->v[0]->vec);
      list_int64_vector_add_int64_vector(list_FK,
          list_vec_zero->v[1]->vec);
    } else {
      list_int64_vector_add_int64_vector(list_FK,
          list_vec_zero->v[1]->vec);
      list_int64_vector_add_int64_vector(list_FK,
          list_vec_zero->v[0]->vec);
    }
    boolean = 1;
  }
#else
  list_int64_vector_add_int64_vector(list_FK, vec[0]);
  list_int64_vector_add_int64_vector(list_FK, vec[1]);
  boolean = reduce_qlattice(list_FK->v[0], list_FK->v[1], vec[0],
      vec[1], (int64_t)(2 * H->h[0]));
#endif

  for (unsigned int i = 0; i < Mqr->NumCols; i++) {
    int64_vector_clear(vec[i]);
  }
  free(vec);


  if (!boolean) {
    ASSERT(boolean == 0);
    ASSERT(list_FK->v[0]->c[1] == 0);
    ASSERT(list_FK->v[1]->c[0] == 0);

    return 0;
  }

  ASSERT(list_FK->v[0]->c[0] > -(int64_t)(2 * H->h[0]));
  ASSERT(0 >= list_FK->v[0]->c[0]);
  ASSERT(0 <= list_FK->v[1]->c[0]);
  ASSERT(list_FK->v[1]->c[0] < (int64_t)(2 * H->h[0]));
  ASSERT(list_FK->v[0]->c[1] > 0);
  ASSERT(list_FK->v[1]->c[1] > 0);
  //TODO: Go up, and just when we need.

  if (space_sieve_good_vector(list_FK->v[0], H)) {
    if (!int64_vector_in_list_zero(list_FK->v[0], list_vec_zero)) {
      list_int64_vector_index_add_int64_vector_index(list_vec_zero,
          list_FK->v[0],
          index_vector(list_FK->v[0], H, number_element));
    }
  }
  if (space_sieve_good_vector(list_FK->v[1], H)) {
    if (!int64_vector_in_list_zero(list_FK->v[1], list_vec_zero)) {
      list_int64_vector_index_add_int64_vector_index(list_vec_zero,
          list_FK->v[1],
          index_vector(list_FK->v[1], H, number_element));
    }
  }

  return 1;
}

unsigned int space_sieve_1_next_plane_seek(int64_vector_ptr s_tmp,
    unsigned int * index_vec, unsigned int * s_change,
    list_int64_vector_srcptr list_s, list_int64_vector_index_srcptr list_vec,
    sieving_bound_srcptr H, int64_vector_srcptr s)
{
  ASSERT(* s_change == 0);

  unsigned int hit = 0;
  int64_vector_t v_tmp;
  int64_vector_init(v_tmp, s->dim);

  for (unsigned int i = 0; i < s->dim; i++) {
    s_tmp->c[i] = (int64_t)H->h[i];
  }
  for (unsigned int i = 0; i < list_s->length; i++) {
    ASSERT(list_s->v[i]->c[2] == s->c[2]);

    for (unsigned int j = 0; j < list_vec->length; j++) {
      int64_vector_add(v_tmp, list_s->v[i], list_vec->v[j]->vec);
      if (int64_vector_in_sieving_region_dim(v_tmp, H)) {
        if (v_tmp->c[2] < s_tmp->c[2]) {
          int64_vector_set(s_tmp, v_tmp);
          * index_vec = j;
          if (!int64_vector_equal(s, list_s->v[i])) {
            * s_change = 1;
          }
        }
        hit = 1;
      }
    }
  }

  int64_vector_clear(v_tmp);

  return hit;
}

