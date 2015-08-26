#include "cado.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "bwc_config.h"
#include "parallelizing_info.h"
#include "select_mpi.h"
#include "params.h"
#include "portability.h"
#include "macros.h"

int verbose=0;

void * program(parallelizing_info_ptr pi, param_list pl MAYBE_UNUSED, void * arg MAYBE_UNUSED)
{
    if (verbose) {
        pi_log_init(pi->m);
        pi_log_init(pi->wr[0]);
        pi_log_init(pi->wr[1]);
    }

    // it is here as a cheap sanity check.
    hello(pi);

    if (verbose) {
        pi_log_op(pi->m, "serialize");
        serialize(pi->m);
        pi_log_op(pi->wr[0], "serialize(2nd)");
        serialize(pi->wr[0]);
        pi_log_op(pi->wr[1], "serialize(3rd)");
        serialize(pi->wr[1]);

        pi_log_print_all(pi);
    }

    return NULL;
}

int main(int argc, char * argv[])
{
    int rank;
    int size;
    param_list pl;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    param_list_init (pl);

    parallelizing_info_decl_usage(pl);
    param_list_decl_usage(pl, "v", "(switch) turn on some demo logging");

    const char * programname = argv[0];

    argv++, argc--;
    param_list_configure_switch(pl, "v", &verbose);

    for( ; argc ; ) {
        if (param_list_update_cmdline(pl, &argc, &argv)) { continue; }
        fprintf(stderr, "Unhandled parameter %s\n", argv[0]);
        param_list_print_usage(pl, programname, stderr);
        exit(EXIT_FAILURE);
    }

    parallelizing_info_lookup_parameters(pl);

    if (verbose)
        param_list_display (pl, stderr);
    if (param_list_warn_unused(pl)) {
        param_list_print_usage(pl, programname, stderr);
        exit(EXIT_FAILURE);
    }

    pi_go(program, pl, 0);

    MPI_Finalize();

    param_list_clear(pl);

    return 0;
}

