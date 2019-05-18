#ifndef LINGEN_MATPOLY_H_
#define LINGEN_MATPOLY_H_

#include "mpfq_layer.h"

struct matpoly;
struct polymat;

#include "lingen-polymat.hpp"

struct submatrix_range {
    unsigned int i0=0,j0=0;
    unsigned int i1=0,j1=0;
    unsigned int nrows() const { return i1-i0; }
    unsigned int ncols() const { return j1-j0; }
    submatrix_range & range() { return *this; }
    submatrix_range const & range() const { return *this; }
    submatrix_range() = default;
    submatrix_range(unsigned int i0, unsigned int j0, unsigned int ni, unsigned int nj) : i0(i0), j0(j0), i1(i0+ni), j1(j0+nj) {}
    template<typename T>
    submatrix_range(T const & M) : i0(0), j0(0), i1(M.nrows()), j1(M.ncols()) {}
    template<typename T>
    inline bool valid(T const & a) const {
        return i0 <= i1 && i1 <= a.m && j0 <= j1 && j1 <= a.n;
    }
    submatrix_range operator*(submatrix_range const & a) const {
        ASSERT_ALWAYS(ncols() == a.nrows());
        return submatrix_range(i0, nrows(), a.j0, a.ncols());
    }
};

/* This is used only for plingen. */

/* We use abvec because this offers the possibility of having flat data
 *
 * Note that this ends up being exactly the same data type as polymat.
 * The difference here is that the stride is not the same.
 */
struct matpoly {
    abdst_field ab = NULL;
    unsigned int m = 0;
    unsigned int n = 0;
    size_t size = 0;
    size_t alloc = 0;
    abvec x = NULL;
    inline unsigned int nrows() const { return m; }
    inline unsigned int ncols() const { return n; }

    matpoly() { m=n=0; size=alloc=0; ab=NULL; x=NULL; }
    matpoly(abdst_field ab, unsigned int m, unsigned int n, int len);
    matpoly(matpoly const&) = delete;
    matpoly& operator=(matpoly const&) = delete;
    matpoly(matpoly &&);
    matpoly& operator=(matpoly &&);
    ~matpoly();
    int check_pre_init() const;
    void realloc(size_t);
    void zero();
    /* {{{ access interface for matpoly */
    inline abdst_vec part(unsigned int i, unsigned int j, unsigned int k=0) {
        ASSERT_ALWAYS(size);
        return abvec_subvec(ab, x, (i*n+j)*alloc+k);
    }
    inline abdst_elt coeff(unsigned int i, unsigned int j, unsigned int k=0) {
        return abvec_coeff_ptr(ab, part(i,j,k), 0);
    }
    inline absrc_vec part(unsigned int i, unsigned int j, unsigned int k=0) const {
        ASSERT_ALWAYS(size);
        return abvec_subvec_const(ab, x, (i*n+j)*alloc+k);
    }
    inline absrc_elt coeff(unsigned int i, unsigned int j, unsigned int k=0) const {
        return abvec_coeff_ptr_const(ab, part(i,j,k), 0);
    }
    /* }}} */
    void set_constant_ui(unsigned long e);
    void set_constant(absrc_elt e);
#if 0
    void swap(matpoly&);
#endif
    void fill_random(unsigned int size, gmp_randstate_t rstate);
    int cmp(matpoly const & b) const;
    void multiply_column_by_x(unsigned int j, unsigned int size);
    void divide_column_by_x(unsigned int j, unsigned int size);
    void truncate(matpoly const & src, unsigned int size);
    void extract_column(
        unsigned int jdst, unsigned int kdst,
        matpoly const & src, unsigned int jsrc, unsigned int ksrc);
    void transpose_dumb(matpoly const & src);
    void zero_column(unsigned int jdst, unsigned int kdst);
    void extract_row_fragment(unsigned int i1, unsigned int j1,
        matpoly const & src, unsigned int i0, unsigned int j0,
        unsigned int n);
    void rshift(matpoly const &, unsigned int k);
    void addmul(matpoly const & a, matpoly const & b);
    void mul(matpoly const & a, matpoly const & b);
    void addmp(matpoly const & a, matpoly const & c);
    void mp(matpoly const & a, matpoly const & c);
    void set_polymat(polymat const & src);
    int coeff_is_zero(unsigned int k) const;
    void coeff_set_zero(unsigned int k);
    struct view_t;
    struct const_view_t;

    struct view_t : public submatrix_range {
        matpoly & M;
        view_t(matpoly & M, submatrix_range S) : submatrix_range(S), M(M) {}
        view_t(matpoly & M) : submatrix_range(M), M(M) {}
        inline abdst_vec part(unsigned int i, unsigned int j) {
            return M.part(i0+i, j0+j);
        }
        inline absrc_vec part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
    };

    struct const_view_t : public submatrix_range {
        matpoly const & M;
        const_view_t(matpoly const & M, submatrix_range S) : submatrix_range(S), M(M) {}
        const_view_t(matpoly const & M) : submatrix_range(M), M(M) {}
        const_view_t(view_t const & V) : submatrix_range(V), M(V.M) {}
        inline absrc_vec part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
    };
    view_t view(submatrix_range S) { ASSERT_ALWAYS(S.valid(*this)); return view_t(*this, S); }
    const_view_t view(submatrix_range S) const { ASSERT_ALWAYS(S.valid(*this)); return const_view_t(*this, S); }
    view_t view() { return view_t(*this); }
    const_view_t view() const { return const_view_t(*this); }
};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif


#endif	/* LINGEN_MATPOLY_H_ */