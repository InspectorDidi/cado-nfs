#include "cado.h"
#include "las-output.hpp"
#include "utils.h"
#include "las-config.h"

/* {{{ las_verbose things */
static void las_verbose_enter(cxx_param_list & pl, FILE * output, int verbose)
{
    verbose_interpret_parameters(pl);
    verbose_output_init(NR_CHANNELS);
    verbose_output_add(0, output, verbose + 1);
    verbose_output_add(1, stderr, 1);
    /* Channel 2 is for statistics. We always print them to las' normal output */
    verbose_output_add(2, output, 1);
    if (param_list_parse_switch(pl, "-stats-stderr")) {
        /* If we should also print stats to stderr, add stderr to channel 2 */
        verbose_output_add(2, stderr, 1);
    }
#ifdef TRACE_K
    const char *trace_file_name = param_list_lookup_string(pl, "traceout");
    FILE *trace_file = stderr;
    if (trace_file_name != NULL) {
        trace_file = fopen(trace_file_name, "w");
        DIE_ERRNO_DIAG(trace_file == NULL, "fopen", trace_file_name);
    }
    verbose_output_add(TRACE_CHANNEL, trace_file, 1);
#endif
}

static void las_verbose_leave()
{
    verbose_output_clear();
}
/* }}} */

/*{{{ stuff related to las output: -out, -stats-stderr, and so on. */

void las_output::fflush()
{
    verbose_output_start_batch();
    ::fflush(output);
    verbose_output_end_batch();
}

void las_output::set(cxx_param_list & pl)
{
    ASSERT_ALWAYS(output == NULL);
    output = stdout;
    outputname = param_list_lookup_string(pl, "out");
    if (outputname) {
	if (!(output = fopen_maybe_compressed(outputname, "w"))) {
	    fprintf(stderr, "Could not open %s for writing\n", outputname);
	    exit(EXIT_FAILURE);
	}
    }
    verbose = param_list_parse_switch(pl, "-v");
    setvbuf(output, NULL, _IOLBF, 0);      /* mingw has no setlinebuf */
    las_verbose_enter(pl, output, verbose);

    param_list_print_command_line(output, pl);
    las_display_config_flags();
}

/* This cannot be a dtor because las_output is a global, and it holds
 * references to pl which is local -- Thus we want to control the exact
 * time where fclose is called.
 */
void las_output::release()
{
    if (outputname)
        fclose_maybe_compressed(output, outputname);
    las_verbose_leave();
    outputname = NULL;
}


void las_output::configure_switches(cxx_param_list & pl)
{
    param_list_configure_switch(pl, "-stats-stderr", NULL);
    param_list_configure_switch(pl, "-v", NULL);
}

void las_output::declare_usage(cxx_param_list & pl)
{
    param_list_decl_usage(pl, "v",    "verbose mode, also prints sieve-area checksums");
    param_list_decl_usage(pl, "out",  "filename where relations are written, instead of stdout");
#ifdef TRACE_K
    param_list_decl_usage(pl, "traceout", "Output file for trace output, default: stderr");
#endif
    param_list_decl_usage(pl, "stats-stderr", "print stats to stderr in addition to stdout/out file");
}
/*}}}*/

