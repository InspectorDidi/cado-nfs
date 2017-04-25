#include "cado.h"

// #include <stdio.h>
// #include <stdlib.h>
// #include <fcntl.h>		/* for _O_BINARY */
// #include <string.h>		/* for strcmp */
#ifdef HAVE_OPENMP
#include <omp.h>
#endif

#include <limits>
#include <vector>
#include <iostream>
#include <sstream>
#include <queue>
#include <set>

#include "portability.h"

/* We're going to use this transitionally */
#define MKZTYPE_CAVALLAR 0
#define MKZTYPE_PURE 1
#define MKZTYPE_LIGHT 2

#define REPORT_INCR 3.0

#include "filter_config.h"
#include "utils_with_io.h"
// #include "merge_replay_matrix.h" /* for filter_matrix_t */
// #include "report.h"     /* for report_t */
// #include "markowitz.h" /* for MkzInit */
// #include "merge_mono.h" /* for mergeOneByOne */
// #include "sparse.h"

#include "medium_int.hpp"
#include "indexed_priority_queue.hpp"
#include "compressible_heap.hpp"
#include "get_successive_minima.hpp"
#define DEBUG_SMALL_SIZE_POOL
#include "small_size_pool.hpp"
#include "minimum_spanning_tree.hpp"

/* NOTE: presently this value has a very significant impact on I/O speed
 * (about a factor of two when reading the purged file), and we have
 * reasons to believe that just about every operation will suffer from
 * the very same problems...
 */
static const int compact_column_index_size = 8;
static const int merge_row_heap_batch_size = 16384;

static void declare_usage(param_list pl)
{
    param_list_decl_usage(pl, "mat", "input purged file");
    param_list_decl_usage(pl, "out", "output history file");
    /*
    param_list_decl_usage(pl, "resume", "resume from history file");
    param_list_decl_usage(pl, "forbidden-cols",
			  "list of columns that cannot be "
			  "used for merges");
    param_list_decl_usage(pl, "path_antebuffer",
			  "path to antebuffer program");
    */
    param_list_decl_usage(pl, "force-posix-threads", "(switch)");
    param_list_decl_usage(pl, "v", "verbose level");
    param_list_decl_usage(pl, "t", "number of threads");
}

static void usage(param_list pl, char *argv0)
{
    param_list_print_usage(pl, argv0, stderr);
    exit(EXIT_FAILURE);
}

struct merge_matrix {
  size_t nrows;
  size_t ncols;
  size_t rem_nrows;     /* number of remaining rows */
  size_t rem_ncols;     /* number of remaining columns, including the buried
                             columns */
  size_t weight;        /* non-zero coefficients in the active part */
  size_t nburied;       /* the number of buried columns */
  size_t total_weight;  /* Initial total number of non-zero coefficients */
  size_t keep;          /* target for nrows-ncols */
  int cwmax;
  int maxlevel;         /* says it */
  unsigned int mkztype;
  unsigned int wmstmax;
  double target_density;
  std::map<int,int> stats;

  merge_matrix()
  {
      rem_ncols = 0;
      total_weight = 0;
      maxlevel = DEFAULT_MERGE_MAXLEVEL;
      keep = DEFAULT_FILTER_EXCESS;
      nburied = DEFAULT_MERGE_SKIP;
      target_density = DEFAULT_MERGE_TARGET_DENSITY;
      mkztype = DEFAULT_MERGE_MKZTYPE;
      wmstmax = DEFAULT_MERGE_WMSTMAX;
  }
  /* {{{ merge_row_ideal type details */
#ifdef FOR_DL
  template<int index_size = 8>
  struct merge_row_ideal {
      typedef medium_int<index_size> index_type;
      // typedef size_t index_type;
      typedef int32_t exponent_type;
      /* exponent_type could be compressed, although we acknowledge the
       * fact that merge entails some increase of the exponent values,
       * obviously */
      private:
      index_type h;
      exponent_type e;
      public:
      merge_row_ideal() {}
      merge_row_ideal(prime_t p) : h(p.h), e(p.e) {}
      index_type const & index() const { return h; }
      index_type & index() { return h; }
      exponent_type const & exponent() const { return e; }
      exponent_type & exponent() { return e; }
      bool operator<(merge_row_ideal const& a) const { return h < a.h; }
      template<int otherwidth> friend class merge_row_ideal;
      template<int otherwidth>
      merge_row_ideal(merge_row_ideal<otherwidth> const & p) : h(p.h), e(p.e) {}
      merge_row_ideal operator*(exponent_type const& v) const {
          merge_row_ideal res;
          res.h = h;
          res.e = e * v;
          return res;
      }
  };
#else
  template<int index_size = 8>
  struct merge_row_ideal {
      typedef medium_int<index_size> index_type;
      private:
      index_type h;
      public:
      merge_row_ideal() {}
      merge_row_ideal(prime_t p) : h(p.h) {}
      index_type const & index() const { return h; }
      index_type & index() { return h; }
      bool operator<(merge_row_ideal const& a) const { return h < a.h; }
      template<int otherwidth> friend class merge_row_ideal;
      template<int otherwidth>
      merge_row_ideal(merge_row_ideal<otherwidth> const & p) : h(p.h) {}
  };
#endif
  /* }}} */


  static void declare_usage(param_list_ptr pl);
  bool interpret_parameters(param_list_ptr pl);

  /* {{{ row-oriented structures */
  typedef unsigned int row_weight_t;
  /* This structure holds the rows. We strive to avoid fragmentation
   * here. */

  typedef merge_row_ideal<compact_column_index_size> row_value_t;

  typedef compressible_heap<
      row_value_t,
      row_weight_t,
      merge_row_heap_batch_size> rows_t;
  rows_t rows;
  struct row_weight_iterator : public decltype(rows)::iterator {
      typedef decltype(rows)::iterator super;
      typedef std::pair<size_t, row_weight_t> value_type;
      row_weight_iterator(super const & x) : super(x) {}
      value_type operator*() {
          super& s(*this);
          ASSERT_ALWAYS(s->first);
          return std::make_pair(index(), s->second);
      }
  };

  void read_rows(const char *purgedname);
  void push_relation(earlyparsed_relation_ptr rel);
  /* }}} */

  /* {{{ column-oriented structures. We have several types of columns:
   *  - "buried" columns. They're essentially deleted from the matrix
   *    forever.
   *  - "inactive" columns, for which R_table[j]==0
   *  - "active" columns, for which col_weights[j] <= cwmax (cwmax loops
   *    from 2 to maxlevel).
   */
  typedef uint32_t col_weight_t;  /* we used to require that it be signed.
                                     It's no longer the case. In fact,
                                     it's even the contrary... */
  /* weight per column index */
  std::vector<col_weight_t> col_weights;
  /* The integer below decides how coarse the allocation should be.
   * Larger means potentially faster reallocs, but also more memory usage
   *
   * Note that this is inherently tied to the col_weights vector !
   */
  typedef small_size_pool<size_t, col_weight_t, 1> R_pool_t;
  R_pool_t R_pool;
  std::vector<R_pool_t::size_type> R_table;
  void clear_R_table();
  void prepare_R_table();
  size_t count_columns_below_weight(size_t *nbm, size_t wmax);
  void bury_heavy_columns();
  void renumber_columns();
  /* }}} */

  /* {{{ priority queue for active columns to be considered for merging
   */
  typedef int32_t pivot_priority_t;
  struct markowitz_comp_t {
      typedef std::pair<size_t, pivot_priority_t> value_t;
      bool operator()(value_t const& a, value_t const& b) {
          if (a.second < b.second) return true;
          if (b.second < a.second) return false;
          if (a.first < b.first) return true;
          if (b.first < a.first) return false;
          return false;
      }
  };
  typedef sparse_indexed_priority_queue<size_t, pivot_priority_t> markowitz_table_t;
  markowitz_table_t markowitz_table;
  pivot_priority_t markowitz_count(size_t j) const;
  void fill_markowitz_table();
  /* }}} */

  /*{{{ general high-level operations */
  void addRj(size_t i);
  void subRj(size_t i, size_t keepcolumn = SIZE_MAX);
  void remove_column(size_t j);
  void remove_row(size_t i, size_t keepcolumn = SIZE_MAX);
  size_t remove_singletons();
  size_t remove_singletons_iterate();
  void pivot_with_column(size_t j);
#if 0
#ifdef FOR_DL
  void multipliers_for_row_combination(
        row_value_t::exponent_type & e0, row_value_t::exponent_type & e1,
        size_t i0, size_t i1, size_t j);
#endif
  /* The set is optional. The routine adds to it the set of column
   * indices (not counting j) which appear more than once in rows i0 and
   * i1).
   */
  row_weight_t weight_of_sum_of_rows(size_t i0, size_t i1, size_t j, std::set<size_t> * S = NULL) const;
#endif
#if 0
  struct compare_by_row_weight {
      merge_matrix const & M;
      compare_by_row_weight(merge_matrix const & M):M(M){}
      bool operator()(size_t a, size_t b) const {
          if (!M.rows[a].first) return true;
          return M.rows[a].second < M.rows[b].second;
      }
  };
#endif
  high_score_table<size_t, row_weight_t> heavy_rows;
  size_t remove_excess(size_t count = SIZE_MAX);
  void merge();
  /*}}}*/

  uint64_t WN_cur;
  uint64_t WN_min;
  double WoverN;
  /* report lines are printed for each multiple of report_incr */
  double report_incr = REPORT_INCR;
  double report_next;
  void report_init() {
      WN_cur = (uint64_t) rem_nrows * (uint64_t) weight;
      WN_min = WN_cur;
      report_next = ceil (WoverN / report_incr) * report_incr;
  }
  double report(bool force = false) {
      WN_cur = (uint64_t) rem_nrows * (uint64_t) weight;
      WN_min = std::min(WN_min, WN_cur);
      WoverN = (double) weight / (double) rem_nrows;

      bool disp = false;
      if ((disp = WoverN >= report_next))
          report_next += report_incr;
      if (disp || force) {
          long vmsize = Memusage();
          long vmrss = Memusage2();
          printf ("N=%zu (%zd) m=%d W=%zu W*N=%.2e W/N=%.2f #Q=%zu [%.1f/%.1f]\n",
                  rem_nrows, rem_nrows-rem_ncols, cwmax, weight,
                  (double) WN_cur, WoverN, 
                  markowitz_table.size(),
                  vmrss/1024.0, vmsize/1024.0);
          printf("# rows %.1f\n", rows.allocated_bytes() / 1048576.);
          fflush (stdout);
      }
      return WoverN;
  }
};

/*{{{ parameters */
void merge_matrix::declare_usage(param_list_ptr pl) {
    param_list_decl_usage(pl, "keep",
            "excess to keep (default "
            STR(DEFAULT_FILTER_EXCESS) ")");
    param_list_decl_usage(pl, "skip",
            "number of heavy columns to bury (default "
            STR(DEFAULT_MERGE_SKIP) ")");
    param_list_decl_usage(pl, "maxlevel",
            "maximum number of rows in a merge " "(default "
            STR(DEFAULT_MERGE_MAXLEVEL) ")");
    param_list_decl_usage(pl, "target_density",
            "stop when the average row density exceeds this value"
            " (default " STR(DEFAULT_MERGE_TARGET_DENSITY) ")");
    param_list_decl_usage(pl, "mkztype",
			  "controls how the weight of a merge is "
			  "approximated (default " STR(DEFAULT_MERGE_MKZTYPE)
			  ")");
    param_list_decl_usage(pl, "wmstmax",
			  "controls until when a mst is used with "
			  "-mkztype 2 (default " STR(DEFAULT_MERGE_WMSTMAX)
			  ")");
}

bool merge_matrix::interpret_parameters(param_list_ptr pl) {
    param_list_parse_size_t(pl, "keep", &keep);
    param_list_parse_size_t(pl, "skip", &nburied);
    param_list_parse_int(pl, "maxlevel", &maxlevel);
    param_list_parse_double(pl, "target_density", &target_density);
    param_list_parse_uint(pl, "mkztype", &mkztype);
    param_list_parse_uint(pl, "wmstmax", &wmstmax);
    if (maxlevel <= 0 || maxlevel > MERGE_LEVEL_MAX) {
        fprintf(stderr,
                "Error: maxlevel should be positive and less than %d\n",
                MERGE_LEVEL_MAX);
        return false;
    }
    if (mkztype > 2) {
	fprintf(stderr, "Error: -mkztype should be 0, 1, or 2.\n");
        return false;
    }
    return true;
}
/*}}}*/

/* {{{ merge_matrix::push_relation and ::read_rows */
void merge_matrix::push_relation(earlyparsed_relation_ptr rel)
{
    merge_row_ideal<> temp[rel->nb];
    std::copy(rel->primes, rel->primes + rel->nb, temp);
    std::sort(temp, temp + rel->nb);
    /* we should also update the column weights */
    for(weight_t i = 0 ; i < rel->nb ; i++) {
        col_weight_t & w(col_weights[temp[i].index()]);
        rem_ncols += !w;
        w += w != std::numeric_limits<col_weight_t>::max();
    }
    total_weight += rel->nb;
    rows.push_back(temp, temp+rel->nb);
}

/* callback function called by filter_rels */
void * merge_matrix_push_relation (void *context_data, earlyparsed_relation_ptr rel)
{
    merge_matrix & M(*(merge_matrix*) context_data);
    M.push_relation(rel);
    return NULL;
}

void merge_matrix::read_rows(const char *purgedname)
{
    /* Read number of rows and cols on first line of purged file */
    purgedfile_read_firstline(purgedname, &nrows, &ncols);
    col_weights.assign(ncols, col_weight_t());

    char *fic[2] = {(char *) purgedname, NULL};

    /* read all rels. */
    rem_nrows = filter_rels(fic,
            (filter_rels_callback_t) &merge_matrix_push_relation, 
            (void*)this,
            EARLYPARSE_NEED_INDEX, NULL, NULL);
    ASSERT_ALWAYS(rem_nrows == nrows);
    weight = total_weight;

    heavy_rows.set_depth(rem_nrows - rem_ncols);
    for(size_t i = 0 ; i < rem_nrows ; i++)
        heavy_rows.push(i, rows[i].second);


    /* print weight count */
    size_t nbm[256];
    size_t active = count_columns_below_weight(nbm, 256);
    for (int h = 1; h <= maxlevel; h++)
        printf ("# There are %zu column(s) of weight %d\n", nbm[h], h);
    printf ("# Total %zu active columns\n", active);
    ASSERT_ALWAYS(rem_ncols == ncols - nbm[0]);

    printf("# Total weight of the matrix: %zu\n", total_weight);

    bury_heavy_columns();

    printf("# Weight of the active part of the matrix: %zu\n", weight);

    /* XXX for DL, we don't want to do this, do we ? */
#ifndef FOR_DL
    renumber_columns();
#endif
    
#if 0
    /* Allocate mat->R[h] if necessary */
    initMatR (mat);

    /* Re-read all rels (in memory) to fill-in mat->R */
    printf("# Start to fill-in columns of the matrix...\n");
    fflush (stdout);
    fillR (mat);
    printf ("# Done\n");
#endif
}
/* }}} */

/*{{{ size_t merge_matrix::count_columns_below_weight */
/* Put in nbm[w] for 0 <= w < wmax, the number of ideals of weight w.
 * Return the number of active columns (w > 0) -- this return value is
 * independent of wmax.
 */
size_t merge_matrix::count_columns_below_weight (size_t * nbm, size_t wmax)
{
  size_t active = 0;
  for (size_t h = 0; h < wmax; nbm[h++] = 0) ;
  for (size_t h = 0; h < ncols; h++) {
      col_weight_t w = col_weights[h];
      if ((size_t) w < wmax) nbm[w]++;
      active += w > 0;
  }
  return active;
}
/*}}}*/
void merge_matrix::bury_heavy_columns()/*{{{*/
{
    if (!nburied) {
        printf("# No columns were buried.\n");
        weight = total_weight;
        return;
    }

    static const col_weight_t colweight_max=std::numeric_limits<col_weight_t>::max();

    double tt = seconds();
    std::vector<size_t> heaviest = get_successive_minima(col_weights, nburied, std::greater<col_weight_t>());

    col_weight_t max_buried_weight = col_weights[heaviest.front()];
    col_weight_t min_buried_weight = col_weights[heaviest.back()];

    /* Compute weight of buried part of the matrix. */
    size_t weight_buried = 0;
    for (size_t i = 0; i < heaviest.size() ; i++)
    {
        col_weight_t& w = col_weights[heaviest[i]];
        weight_buried += w;
        /* since we saturate the weights at 2^31-1, weight_buried might be less
           than the real weight of buried columns, however this can occur only
           when the number of rows exceeds 2^32-1 */
#if DEBUG >= 1
        fprintf(stderr, "# Burying j=%zu (wt = %zu)\n",
                heaviest[i], (size_t) w);
#endif
        /* reset to 0 the weight of the buried columns */
        w = 0;
    }
    printf("# Number of buried columns is %zu (%zu >= weight >= %zu)\n",
            nburied, (size_t) max_buried_weight, (size_t) min_buried_weight);

    bool weight_buried_is_exact = max_buried_weight < colweight_max;
    printf("# Weight of the buried part of the matrix is %s%zu\n",
            weight_buried_is_exact ? "" : ">= ", weight_buried);

    /* Remove buried columns from rows in mat structure */
    printf("# Start to remove buried columns from rels...\n");
    fflush (stdout);
    // // #ifdef HAVE_OPENMP
    // // #pragma omp parallel for
    // // #endif
    for (auto it = rows.begin() ; it != rows.end() ; ++it) {
        row_value_t * ptr = it->first;
        row_weight_t nl = 0;
        for (row_weight_t i = 0; i < it->second; i++) {
            size_t h = ptr[i].index();
            col_weight_t w = col_weights[h];
#if 0
            bool bury = w > min_buried_weight;
            bury = bury || (w == min_buried_weight && h > heaviest.back());
            if (!bury)
                ptr[nl++] = ptr[i];
#else
            if (w)
                ptr[nl++] = ptr[i];
#endif
        }
        rows.shrink_value(it, nl);
    }

    printf("# Done. Total bury time: %.1f s\n", seconds()-tt);
    /* compute the matrix weight */
    weight = 0;
    for (auto const & R : rows)
        weight += R.second;

    if (weight_buried_is_exact)
        ASSERT_ALWAYS (weight + weight_buried == total_weight);
    else /* weight_buried is only a lower bound */
        ASSERT_ALWAYS (weight + weight_buried <= total_weight);
}/*}}}*/
/*{{{ void merge_matrix::renumber_columns() */
/* Renumber the non-zero columns to contiguous values [0, 1, 2, ...]
 * We can use this function only for factorization because in this case we do
 * not need the indexes of the columns (contrary to DL where the indexes of the
 * column are printed in the history file). */
void merge_matrix::renumber_columns()
{
    double tt = seconds();

    printf("# renumbering columns\n");
    std::vector<size_t> p(ncols);
    /* compute the mapping of column indices, and adjust the col_weights
     * table */
    size_t h = 0;
    for (size_t j = 0; j < ncols; j++) {
        if (!col_weights[j]) continue;
        p[j]=h;
        col_weights[h] = col_weights[j];
#ifdef TRACE_COL
        if (TRACE_COL == h || TRACE_COL == j)
            printf ("TRACE_COL: column %zu is renumbered into %zu\n", j, h);
#endif
        h++;
    }
    col_weights.erase(col_weights.begin() + h, col_weights.end());
    /* h should be equal to rem_ncols, which is the number of columns with
     * non-zero weight */
    ASSERT_ALWAYS(h + nburied == rem_ncols);

    ncols = h;

    /* apply mapping to the rows. As p is a non decreasing function, the
     * rows are still sorted after this operation. */
    for (auto & R : rows) {
        row_value_t * ptr = R.first;
        for (row_weight_t i = 0 ; i < R.second ; i++)
            ptr[i].index()=p[ptr[i].index()];
    }
    printf("# renumbering done in %.1f s\n", seconds()-tt);
}
/*}}}*/

void merge_matrix::clear_R_table()/*{{{*/
{
    R_table.clear();
    R_pool.clear();
    R_table.assign(col_weights.size(), R_pool_t::size_type());
}
/* }}} */
void merge_matrix::prepare_R_table()/*{{{*/
{
    // double tt = seconds();
    clear_R_table();
    for(size_t j = 0 ; j < col_weights.size() ; j++) {
        if (col_weights[j] > (col_weight_t) cwmax || !col_weights[j]) continue;
        R_table[j] = R_pool.alloc(col_weights[j]);
    }
    // printf("# Time for allocating R: %.2f s\n", seconds()-tt);
    // tt=seconds();
    ASSERT_ALWAYS(cwmax <= std::numeric_limits<uint8_t>::max());
    std::vector<uint8_t> pos(col_weights.size(), 0);
    for(auto it = rows.begin() ; it != rows.end() ; ++it) {
        row_value_t * ptr = it->first;
        for (row_weight_t k = 0; k < it->second; k++) {
            size_t j = ptr[k].index();
            if (!R_table[j]) continue;
            col_weight_t w = col_weights[j];
            R_pool_t::value_type * Rx = R_pool(w, R_table[j]);
            ASSERT_ALWAYS(pos[j] < cwmax);
            Rx[pos[j]++]=it.i;
        }
    }
    for(size_t j = 0 ; j < col_weights.size() ; j++) {
        if (col_weights[j] > (col_weight_t) cwmax || !col_weights[j]) continue;
        ASSERT_ALWAYS(pos[j] == col_weights[j]);
    }
    // printf("# Time for filling R: %.2f s\n", seconds()-tt);
}/*}}}*/


/*{{{ pivot_priority_t merge_matrix::markowitz_count */
/* This functions returns the difference in matrix element when we add the
 * lightest row with ideal j to all other rows. If ideal j has weight w,
 * and the lightest row has weight w0:
 * * we remove w elements corresponding to ideal j
 * * we remove w0-1 other elements for the lightest row
 * * we add w0-1 elements to the w-1 other rows
 * Thus the result is (w0-1)*(w-2) - w = (w0-2)*(w-2) - 2.
 * (actually we negate this, because we want a high score given that we
 * put all that in a max heap)
 *  
 * Note: we could also take into account the "cancelled ideals", i.e.,
 * the ideals apart the pivot j that got cancelled "by chance". However
 * some experiments show that this does not improve much (if at all) the
 * result of merge.  A possible explanation is that the contribution of
 * cancelled ideals follows a normal distribution, and that on the long
 * run we mainly see the average.
 */
merge_matrix::pivot_priority_t merge_matrix::markowitz_count(size_t j) const
{
    col_weight_t w = col_weights[j];

    /* empty cols and singletons have max priority */
    if (w <= 1) return std::numeric_limits<pivot_priority_t>::max();
    if (w == 2) return 2;

    /* inactive columns don't count of course, but we shouldn't reach
     * here anyway */
    ASSERT_ALWAYS(R_table[j]);
    if (!R_table[j]) return std::numeric_limits<pivot_priority_t>::min();

    const R_pool_t::value_type * Rx = R_pool(w, R_table[j]);
    row_weight_t w0 = std::numeric_limits<row_weight_t>::max();
    for(col_weight_t k = 0 ; k < w ; k++)
        w0 = std::min(w0, rows[Rx[k]].second);

    return 2 - (w0 - 2) * (w - 2);
}
/*}}}*/

void merge_matrix::fill_markowitz_table()/*{{{*/
{
    // double tt = seconds();
    for(size_t j = 0 ; j < col_weights.size() ; j++) {
        if (!R_table[j]) continue;
        markowitz_table.push(std::make_pair(j, markowitz_count(j)));
    }
    // printf("# Time for filling markowitz table: %.2f s\n", seconds()-tt);
}/*}}}*/

void merge_matrix::addRj(size_t i) /*{{{*/
{
    /* add to the R table the entries for row i */
    rows_t::const_reference R = rows[i];
    for(const row_value_t * p = R.first ; p != R.first + R.second ; p++) {
        size_t j = p->index();
        if (!R_table[j]) { col_weights[j]++; continue; }
        // printf("# addRj for col %zu\n", j);
        /* add mention of this row in the R entry associated to j */
        col_weight_t w = col_weights[j];
        R_pool.realloc(col_weights[j], R_table[j], w+1);
        R_pool(col_weights[j], R_table[j])[w] = i;
        markowitz_table.update(std::make_pair(j, markowitz_count(j)));
    }
}/*}}}*/

void merge_matrix::subRj(size_t i, size_t keepcolumn)/*{{{*/
{
    /* subtract from the R table the entries for row i */
    rows_t::const_reference R = rows[i];
    for(const row_value_t * p = R.first ; p != R.first + R.second ; p++) {
        size_t j = p->index();
        if (j == keepcolumn) continue;
        if (!R_table[j]) { col_weights[j]--; continue; }
        // printf("# subRj for col %zu\n", j);
        /* remove mention of this row in the R entry associated to j */
        col_weight_t w = col_weights[j];
        R_pool_t::value_type * Rx = R_pool(w, R_table[j]);
        R_pool_t::value_type * me = std::find(Rx, Rx + w, i);
        ASSERT_ALWAYS(me < Rx + w);
        std::copy(me + 1, Rx + w, me);
        R_pool.realloc(col_weights[j], R_table[j], w-1);
        markowitz_table.update(std::make_pair(j, markowitz_count(j)));
    }
}
/*}}}*/

void merge_matrix::remove_row(size_t i, size_t keepcolumn)
{
    ASSERT_ALWAYS(rows[i].first);
    subRj(i, keepcolumn);   // do not touch column j now.
    weight -= rows.kill(i);
    heavy_rows.remove(i);
    rem_nrows--;
}

void merge_matrix::remove_column(size_t j)/*{{{*/
{
    col_weight_t & w = col_weights[j];
    R_pool_t::value_type * Rx = R_pool(w, R_table[j]);
    R_pool_t::value_type Rx_copy[w];
    std::copy(Rx, Rx+w, Rx_copy);
    for(col_weight_t k = 0 ; k < w ; k++)
        remove_row(Rx_copy[k],j);
    R_pool.free(col_weights[j], R_table[j]);
    rem_ncols--;
}
/*}}}*/

size_t merge_matrix::remove_singletons()/*{{{*/
{
    size_t nr = 0;
    for(size_t j = 0 ; j < col_weights.size() ; j++) {
        /* We might have created more singletons without re-creating the
         * R table entries for these columns.
         */
        if (col_weights[j] == 1 && R_table[j]) {
            remove_column(j);
            nr++;
        }
    }
    return nr;
}/*}}}*/

size_t merge_matrix::remove_singletons_iterate()/*{{{*/
{
    // double tt = seconds();
    size_t tnr = 0;
    for(size_t nr ; (nr = remove_singletons()) ; ) {
        // printf("# removed %zu singletons in %.2f s\n", nr, seconds() - tt);
        tnr += nr;
        // tt = seconds();
    }
    return tnr;
}
/*}}}*/

struct row_combiner {/*{{{*/
    typedef merge_matrix::rows_t rows_t;
    typedef merge_matrix::row_value_t row_value_t;
    typedef merge_matrix::row_weight_t row_weight_t;
    merge_matrix & M;
    size_t i0;
    size_t i1;
    size_t j;
    row_value_t const * p0;
    row_weight_t n0;
    row_value_t const * p1;
    row_weight_t n1;
    row_weight_t sumweight;
#ifdef FOR_DL
    row_value_t::exponent_type e0;
    row_value_t::exponent_type e1;
    row_value_t::exponent_type emax0;
    row_value_t::exponent_type emax1;
#endif
    row_combiner(merge_matrix & M, size_t i0, size_t i1, size_t j)
        : M(M), i0(i0), i1(i1), j(j)
    {
        rows_t::const_reference R0 = M.rows[i0]; p0 = R0.first; n0 = R0.second;
        rows_t::const_reference R1 = M.rows[i1]; p1 = R1.first; n1 = R1.second;
        ASSERT_ALWAYS(p0);
        ASSERT_ALWAYS(p1);
        sumweight = 0;
#ifdef FOR_DL
        e0 = e1 = 0;
        emax0 = emax1 = 0;
#endif
    }
#ifdef FOR_DL
    void multipliers(row_value_t::exponent_type& x0, row_value_t::exponent_type& x1)/*{{{*/
    {
        if (!e0) {
            row_weight_t k0, k1;
            /* We scan backwards because we have better
             * hope of finding j early that way */
            for(k0 = n0, k1 = n1 ; k0 && k1 ; k0--,k1--) {
                if (p0[k0-1].index() < p1[k1-1].index()) {
                    k0++; // we will replay this one.
                } else if (p0[k0-1].index() > p1[k1-1].index()) {
                    k1++; // we will replay this one.
                } else if (p0[k0-1].index() == j && p1[k1-1].index() == j) {
                    e0 = p0[k0-1].exponent();
                    e1 = p1[k1-1].exponent();
                    break;
                }
            }
            ASSERT_ALWAYS(e0 && e1);
            row_value_t::exponent_type d = gcd_int64 ((int64_t) e0, (int64_t) e1);
            e0 /= d;
            e1 /= d;
        }
        x0=e0;
        x1=e1;
    }/*}}}*/
#endif
    row_weight_t weight_of_sum(std::set<size_t> * S = NULL)/*{{{*/
    {
        if (sumweight) return sumweight;
        /* Compute the sum of the (non-buried fragment of) rows i0 and i1,
         * based on the fact that these are expected to be used for
         * cancelling column j.
         */
#ifdef FOR_DL
        /* for discrete log, we need to pay attention to valuations as well */
        multipliers(e0,e1);
#endif
        row_weight_t k0, k1;
        for(k0 = 0, k1 = 0 ; k0 < n0 && k1 < n1 ; k0++,k1++) {
#ifdef FOR_DL
            if (std::abs(p0[k0].exponent()) > emax0)
                emax0=std::abs(p0[k0].exponent());
            if (std::abs(p1[k1].exponent()) > emax1)
                emax1=std::abs(p1[k1].exponent());
#endif
            if (p0[k0].index() < p1[k1].index()) {
                k1--; // we will replay this one.
                sumweight++;
            } else if (p0[k0].index() > p1[k1].index()) {
                k0--; // we will replay this one.
                sumweight++;
            } else if (p0[k0].index() != j) {
                /* Otherwise this column index appears several times */
#ifdef FOR_DL
                /* except for the exceptional case of the ideal we're
                 * cancelling, we have expectations that we'll get a new
                 * coefficient */
                row_value_t::exponent_type e = p1[k1].exponent()*e0-p0[k0].exponent()*e1;
                sumweight += (e != 0);
#endif
                if (S) S->insert(p0[k0].index());
            }
        }
        sumweight += n0 - k0 + n1 - k1;
        return sumweight;
    } /*}}}*/
    int mst_score(std::set<size_t> * S = NULL)
    {
        weight_of_sum(S);
#ifdef FOR_DL
        if (std::abs((int64_t) emax0 * (int64_t) e1) > 65536)
            return INT_MAX;
        if (std::abs((int64_t) emax1 * (int64_t) e0) > 65536)
            return INT_MAX;
#endif
        return sumweight;
    }


    void subtract_naively()/*{{{*/
    {
        /* This creates a new row with R[i0]-R[i1], with mulitplying
         * coefficients adjusted so that column j is cancelled. Rows i0 and
         * i1 are not removed from the pile of rows just yet, this comes at a
         * later step */
        
        /* This also triggers the computation of the exponents if needed.  */
        weight_of_sum();

#ifdef FOR_DL
        multipliers(e0,e1);
        static row_value_t::exponent_type emax = 4;
#endif

        row_weight_t n2 = sumweight;
        row_value_t p2[n2];

        /* It's of course pretty much the same loop as in other cases. */
        row_weight_t k0 = 0, k1 = 0, k2 = 0;
        for( ; k0 < n0 && k1 < n1 ; k0++,k1++) {
            if (p0[k0].index() < p1[k1].index()) {
                k1--; // we will replay this one.
#ifdef FOR_DL
                p2[k2] = p0[k0] * (-e1);
                    if (std::abs(p2[k2].exponent()) > emax) {
                        emax = std::abs(p2[k2].exponent());
                        printf("# New record coefficient: %ld\n", (long) emax);
                    }
#else
                p2[k2] = p0[k0];
#endif
                k2++;
            } else if (p0[k0].index() > p1[k1].index()) {
                k0--; // we will replay this one.
#ifdef FOR_DL
                p2[k2] = p1[k1] * e0;
                    if (std::abs(p2[k2].exponent()) > emax) {
                        emax = std::abs(p2[k2].exponent());
                        printf("# New record coefficient: %ld\n", (long) emax);
                    }
#else
                p2[k2] = p1[k1];
#endif
                k2++;
            } else if (p0[k0].index() != j) {
                /* Otherwise this column index appears several times */
#ifdef FOR_DL
                /* except for the exceptional case of the ideal we're
                 * cancelling, we have expectations that we'll get a new
                 * coefficient */
                row_value_t::exponent_type e = p1[k1].exponent()*e0-p0[k0].exponent()*e1;
                if (e) {
                    p2[k2].index() = p0[k0].index();
                    p2[k2].exponent() = e;
                    if (std::abs(p2[k2].exponent()) > emax) {
                        emax = std::abs(p2[k2].exponent());
                        printf("# New record coefficient: %ld\n", (long) emax);
                    }
                    k2++;
                }
#endif
            }
        }
        for( ; k0 < n0 ; k0++) {
#ifdef FOR_DL
                p2[k2] = p0[k0] * e0;
                    if (std::abs(p2[k2].exponent()) > emax) {
                        emax = std::abs(p2[k2].exponent());
                        printf("# New record coefficient: %ld\n", (long) emax);
                    }
#else
                p2[k2] = p0[k0];
#endif
                k2++;
        }
        for( ; k1 < n1 ; k1++) {
#ifdef FOR_DL
                p2[k2] = p1[k1] * (-e1);
                    if (std::abs(p2[k2].exponent()) > emax) {
                        emax = std::abs(p2[k2].exponent());
                        printf("# New record coefficient: %ld\n", (long) emax);
                    }
#else
                p2[k2] = p1[k1];
#endif
                k2++;
        }
        ASSERT_ALWAYS(k2 == n2);
        M.rows.push_back(p2, p2 + n2);
        M.heavy_rows.push(M.rows.size()-1, n2);
        M.rem_nrows++;
        M.weight += n2;
    }/*}}}*/
};
/*}}}*/

void merge_matrix::pivot_with_column(size_t j)/*{{{*/
{
    weight_t w = col_weights[j];
    stats[w]++;
    if (w == 0)
        /* m=0 can happen even here: if two ideals have weight 2, and are
         * in the same 2 relations: when we merge one of them, the other
         * one will have weight 0 after the merge */
        return;

    /* m=1, OTOH, cannot happen */
    ASSERT_ALWAYS (w >= 2);

    /* beware: Rx will be invalidated by modifications which touch the R
     * table.  */
    R_pool_t::value_type * Rx = R_pool(w, R_table[j]);

    if (w == 2) {
        /* well, simply subtract the two rows. Ordering does not matter. */
        row_combiner C(*this, Rx[0], Rx[1], j);
        C.subtract_naively();
        addRj(rows.size()-1);
        remove_column(j);
        return;
    }

    matrix<int> weights(w,w,INT_MAX);

    /* while we're doing so, we may also count which are the column
     * indices that happen multiple times in these rows */
    // std::set<size_t> S;
    for(col_weight_t k = 0 ; k < w ; k++) {
        for(col_weight_t l = k + 1 ; l < w ; l++) {
            int s = row_combiner(*this, Rx[k], Rx[l], j).mst_score();
            weights(k,l) = s;
            weights(l,k) = s;
        }
    }
    /* Note: our algorithm for computing the weight matrix can be
     * improved a lot. Currently, for a level-w merge (w is the weight of
     * column j), and given rows with average weight L, our cost is
     * O(w^2*L) for creating the matrix.
     *
     * To improve it, first notice that if V_k is the w-dimensional
     * row vector giving coordinates for column k, then the matrix giving at
     * (i0,i1) the valuation of column k when row i1 is cancelled from
     * row i1 is given by trsp(V_k)*V_j-trsp(V_j)*V_k (obviously
     * antisymmetric).
     *
     * We need to do two things.
     *  - walk all w (sorted!) rows simultaneously, maintaining a
     *    priority queue for the "lowest next" column coordinate. By
     *    doing so, we'll be able to react on all column indices which
     *    happen to appear in several rows (because two consecutive
     *    pop()s from the queue will give the same index). So at select
     *    times, this gives a subset of w' rows, with that index
     *    appearing.
     *    This step will cost O(w*L*log(w)).
     *  - Next, whenever we've identified a column index which appears
     *    multiple times (say w' times), build the w'*w' matrix
     *    trsp(V'_k)*V'_j-trsp(V'_j)*V'_k, where all vectors have length
     *    w'. Inspecting the 0 which appear in this matrix gives the
     *    places where we have exceptional cancellations. This can count
     *    as -1 in the total weight matrix.
     * Overall the cost will become O(w*L*log(w)+w'*L'^2+w^2), which
     * should be somewhat faster.
     */

    auto mst = minimum_spanning_tree(weights);

    if (mst.first == INT_MAX) {
#ifdef FOR_DL
        printf("# disconnected graph for %d-merge due to valuations, skipping merge\n", (int) w);
        return;
#else
        printf("# Fatal: graph is disconnected\n");
        for(weight_t k = 0 ; k < w ; k++) {
            printf("[%d]", (int) Rx[k]);
            rows_t::const_reference R = rows[Rx[k]];
            for(const row_value_t * p = R.first ; p != R.first + R.second ; p++) {
                if ((uint64_t) p->index() == (size_t) j)
                    printf("\033[01;31m");
                printf(" %" PRId64, (uint64_t) p->index());
#ifdef FOR_DL
                printf(":%d", (int) p->exponent());
#endif
                if ((uint64_t) p->index() == (size_t) j)
                    printf("\033[00m");
            }
            printf("\n");
        }
#endif
    }
    ASSERT_ALWAYS(mst.first < INT_MAX);

    // now do the merge for real. We need to be cautious so that we don't
    // do an excessive number of reallocs for the R tables. However
    // that's for another implementation. We'll make do with a
    // simple-minded one for the moment.
    // std::map<size_t, std::vector<R_pool_t::size_type> > dup_R;
    

    /* note that a modifying operation may change the pointers in the R
     * table, so we need to grab the list of indices beforehand */
    R_pool_t::value_type Rx_copy[w];
    std::copy(Rx, Rx+w, Rx_copy);
    for(auto const & edge : mst.second) {
        row_combiner C(*this, Rx_copy[edge.first], Rx_copy[edge.second], j);
        C.subtract_naively();
        addRj(rows.size()-1);
    }
    remove_column(j);
}
/*}}}*/

size_t merge_matrix::remove_excess(size_t count)/*{{{*/
{
    size_t excess = rem_nrows - rem_ncols;

    count = std::min(count, excess - keep);

    if (!count || heavy_rows.size() <= excess - count)
        return 0;
    
    /*
    printf("# Removing %zd excess rows (from excess = %zu-%zu=%zd)\n",
            count, rem_nrows, rem_ncols, excess);
            */

    size_t nb_heaviest = heavy_rows.size() - (excess - count);
    high_score_table<size_t, row_weight_t> heaviest(nb_heaviest);
    heavy_rows.filter_to(heaviest);
    for( ; !heaviest.empty() ; heaviest.pop()) {
        size_t i = heaviest.top().first;
        ASSERT_ALWAYS(rows[i].first);
        remove_row(i);
    }
    heavy_rows.set_depth(rem_nrows - rem_ncols);
    return nb_heaviest;
}/*}}}*/

void merge_matrix::merge()/*{{{*/
{
    report_init();
    int spin = 0;
    report(true);
    for(cwmax = 2 ; cwmax <= maxlevel && spin < 10 ; cwmax++) {

        /* Note: we need the R table for remove_singletons */
        prepare_R_table();
        remove_singletons_iterate();

        markowitz_table.clear();
        fill_markowitz_table();

        for( ; report() < target_density && !markowitz_table.empty() ; ) {
            // ASSERT_ALWAYS(markowitz_table.is_heap());

            markowitz_table_t::value_type q = markowitz_table.top();

            /* We're going to remove this column, so it's important that
             * we remove it from the queue right now, otherwise our
             * actions may cause a shuffling of the queue, and we could
             * very well end up popping the wrong one if pop() comes too
             * late.
             */
            markowitz_table.pop();

            /* if the best score is a (cwmax+1)-merge, then we'd better
             * schedule a re-scan of all potential (cwmax+1)-merges
             * before we process this one */
            if (col_weights[q.first] > (col_weight_t) cwmax)
                break;
            pivot_with_column(q.first);
        }
        // empirical.
        remove_excess((rem_nrows-rem_ncols) * .04);
        remove_singletons_iterate();

        report(true);

        if (cwmax == maxlevel) {
            if (!remove_excess())
                spin++;
            cwmax--;
        }
    }
    report(true);

    printf("# merge stats:\n");
    size_t nmerges=0;
    for(auto x: stats) {
        printf("# %d-merges: %d\n", x.first, x.second);
        nmerges += x.second;
    }
    printf("# Total: %zu merges\n", nmerges);

    /* print weight count */
    size_t nbm[256];
    count_columns_below_weight(nbm, 256);
    for (int h = 1; h <= maxlevel; h++)
        printf ("# There are %zu column(s) of weight %d\n", nbm[h], h);
    printf("# Total weight of the matrix: %zu\n", total_weight);
}
/*}}}*/

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];

#if 0
    filter_matrix_t mat[1];
    report_t rep[1];
#endif

    int nthreads = 1;
    /* use real MST minimum for wt[j] <= wmstmax */

#ifdef HAVE_MINGW
    _fmode = _O_BINARY;		/* Binary open for all files */
#endif

    double tt;
    double wct0 = wct_seconds();
    param_list pl;
    int verbose = 0;
    param_list_init(pl);
    declare_usage(pl);
    merge_matrix::declare_usage(pl);
    argv++, argc--;

    param_list_configure_switch(pl, "force-posix-threads",
				&filter_rels_force_posix_threads);
    param_list_configure_switch(pl, "v", &verbose);

#ifdef HAVE_MINGW
    _fmode = _O_BINARY;		/* Binary open for all files */
#endif

    if (argc == 0)
	usage(pl, argv0);

    for (; argc;) {
	if (param_list_update_cmdline(pl, &argc, &argv))
	    continue;
	fprintf(stderr, "Unknown option: %s\n", argv[0]);
	usage(pl, argv0);
    }
    /* print command-line arguments */
    verbose_interpret_parameters(pl);
    param_list_print_command_line(stdout, pl);
    fflush(stdout);

    const char *purgedname = param_list_lookup_string(pl, "mat");
    const char *outname = param_list_lookup_string(pl, "out");

#if 0
    /* -resume can be useful to continue a merge stopped due  */
    /* to a too small value of -maxlevel                      */
    const char *resumename = param_list_lookup_string(pl, "resume");
    const char *path_antebuffer =
	param_list_lookup_string(pl, "path_antebuffer");
    const char *forbidden_cols =
	param_list_lookup_string(pl, "forbidden-cols");
#endif

    param_list_parse_int(pl, "t", &nthreads);
#ifdef HAVE_OPENMP
    omp_set_num_threads(nthreads);
#endif

    merge_matrix M;

    if (!M.interpret_parameters(pl)) usage(pl, argv0);

    /* Some checks on command line arguments */
    if (param_list_warn_unused(pl)) {
	fprintf(stderr, "Error, unused parameters are given\n");
	usage(pl, argv0);
    }

    if (purgedname == NULL) {
	fprintf(stderr, "Error, missing -mat command line argument\n");
	usage(pl, argv0);
    }
    if (outname == NULL) {
	fprintf(stderr, "Error, missing -out command line argument\n");
	usage(pl, argv0);
    }

    // set_antebuffer_path(argv0, path_antebuffer);

    /* initialize rep (i.e., mostly opens outname) and write matrix dimension */
    /*
    rep->type = 0;
    rep->outfile = fopen_maybe_compressed(outname, "w");
    ASSERT_ALWAYS(rep->outfile != NULL);
    */

    /* Init structure containing the matrix and the heap of potential merges */
    // initMat(mat, maxlevel, keep, skip);

    /* Read all rels and fill-in the mat structure */
    tt = seconds();

    M.read_rows(purgedname);

    printf("# Time for filter_matrix_read: %2.2lfs\n", seconds() - tt);


    M.merge();


#if 0
    /* resume from given history file if needed */
    if (resumename != NULL)
	resume(rep, mat, resumename);
#endif

#if 0
    /* TODO: this is an untested feature, so not easy to put back in
     * operation */
    /* Some columns can be disable so merge won't use them as pivot */
    if (forbidden_cols != NULL) {
	printf("Disabling columns from %s\n", forbidden_cols);
	matR_disable_cols(mat, forbidden_cols);
    }
#endif

#if 0
    tt = seconds();
    MkzInit(mat, 1);
    printf("Time for MkzInit: %2.2lfs\n", seconds() - tt);

    mergeOneByOne(rep, mat, maxlevel, target_density);

    fclose_maybe_compressed(rep->outfile, outname);
    printf("Final matrix has N=%" PRIu64 " nc=%" PRIu64 " (%" PRId64 ") "
	   "W=%" PRIu64 " W*N=%.2e W/N=%.2f\n",
	   mat->rem_nrows, mat->rem_ncols,
	   ((int64_t) mat->rem_nrows) - ((int64_t) mat->rem_ncols),
	   mat->weight, compute_WN(mat), compute_WoverN(mat));
    fflush(stdout);
    MkzClear(mat, 1);
    clearMat(mat);
#endif

    param_list_clear(pl);

    printf("Total merge time: %.2f seconds\n", seconds());

    print_timing_and_memory(stdout, wct0);

    return 0;
}