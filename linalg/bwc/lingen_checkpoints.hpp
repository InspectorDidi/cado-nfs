#ifndef LINGEN_CHECKPOINTS_HPP_
#define LINGEN_CHECKPOINTS_HPP_

#include "lingen.hpp"
#include "lingen_bmstatus.hpp"
#include "params.h"

#include "lingen_matpoly_select.hpp"

int load_checkpoint_file(bmstatus & bm, matpoly & pi, unsigned int t0, unsigned int t1);
int save_checkpoint_file(bmstatus & bm, matpoly & pi, unsigned int t0, unsigned int t1);

#ifdef ENABLE_MPI_LINGEN        /* in lingen.hpp */
#include "lingen_bigmatpoly.hpp"
int load_mpi_checkpoint_file(bmstatus & bm, bigmatpoly & xpi, unsigned int t0, unsigned int t1);
int save_mpi_checkpoint_file(bmstatus & bm, bigmatpoly const & xpi, unsigned int t0, unsigned int t1);
#endif

void lingen_checkpoints_decl_usage(cxx_param_list & pl);
void lingen_checkpoints_lookup_parameters(cxx_param_list & pl);
void lingen_checkpoints_interpret_parameters(cxx_param_list & pl);

#endif	/* LINGEN_CHECKPOINTS_HPP_ */