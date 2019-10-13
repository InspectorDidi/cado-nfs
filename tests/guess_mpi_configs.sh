#!/usr/bin/env bash

# This file can be sourced from shell scripts, and defines a few shell
# variables and arrays.
#
# Input: we want $1 to be one of:
#       "" (empty string)
#       mpi_rect1
#       mpi_rect2
#       mpi_square1
#       mpi_square2
#       We also want to have either $bindir or $PROJECT_BINARY_DIR point
#       to the top of the build tree.
# Output: 
#       $mpi is maked as exported, and set to the mpi geometry, or an
#       empty string if running without mpi.
#       $exporter_mpirun is an exported variable that contain bash-quoted contents
#       meant to be de-serialized by eval "$mpirun".
#       ditto for exporter_mpi_extra_args and mpi_extra_args

: ${bindir:=$PROJECT_BINARY_DIR}
: ${bindir?missing variable}

# not sure that it makes sense to have this script distinct from bwc.pl

detect_mpi_family()
{
    output="`$mpicc -v 2>&1`"
    family=
    suffix=
    if [[ $output =~ ^mpi.*for.MVAPICH2.version[[:space:]](.*) ]] ; then
        family=mvapich2
        suffix="${BASH_REMATCH[1]}"
    elif [[ $output =~ ^mpi.*for.MPICH.*version[[:space:]](.*) ]] ; then
        family=mpich
        suffix="${BASH_REMATCH[1]}"
    elif [[ $output =~ ^mpi.*for.*Intel.*MPI.*Library[[:space:]]([0-9].*)[[:space:]]for.* ]] ; then
        family=impi
        suffix="${BASH_REMATCH[1]}"
    else
        output0="$output"
        output="`$mpicc -showme:version 2>&1`"
        if [[ $output =~ ^.*Open[[:space:]]MPI[[:space:]]([^[:space:]]*) ]] ; then
            family=openmpi
            suffix="${BASH_REMATCH[1]}"
        else
            echo "MPI C compiler front-end not recognized, proceeding anyway (output0 was: $output0 ; later was $output)" >&2
        fi
    fi
}


set_choices_from_n()
{
    jobsize="$1"
    nompi=1x1
    # it is also possible to set things such as _mpi_rect1_mpi_args=(foo bar)
    # in order to adjust the per-config parameters.
    if [ "$jobsize" -ge 6 ] ;   then mpi_rect1=2x3 ; mpi_rect2=3x2 ;
    elif [ "$jobsize" -ge 2 ] ; then mpi_rect1=2x1 ; mpi_rect2=1x2 ; fi
    if [ "$jobsize" -ge 9 ] ;   then mpi_square2=3x3 ; fi
    if [ "$jobsize" -ge 4 ] ;   then mpi_square1=2x2 ; fi
}

set_mpi_derived_variables()
{
    case "$mpi_magic" in
        nompi) ;;
        mpi_rect[12]|mpi_square[12]) : ;;
        *) echo "Unkown magic for mpi auto choice: $mpi_magic" >&2 ; return ;;
    esac

    if ! [ "$bindir" ] ; then
        echo "guess_mpi_configs.sh must be sourced with \$bindir set" >&2
    fi

    mpi_bindir=$(perl -ne '/HAVE_MPI\s*"(.*)"\s*$/ && print "$1\n";' $bindir/cado_mpi_config.h)
    mpiexec=$(perl -ne '/MPIEXEC\s*"(.*)"\s*$/ && print "$1\n";' $bindir/cado_mpi_config.h)
    mpicc=$(perl -ne '/MPI_C_COMPILER\s*"(.*)"\s*$/ && print "$1\n";' $bindir/cado_mpi_config.h)

    if ! [ "$mpi_bindir" ] ; then
        echo "Not an MPI build, mpi checks are disabled"
        return
    fi

    detect_mpi_family

    unset mpi_args_common

    if [ "$OAR_NODEFILE" ] ; then
        nnodes=$(wc -l < $OAR_NODEFILE)
        if [ "$family" = openmpi ] ; then
            mpi_args_common=(-machinefile $OAR_NODEFILE)
            mpi_extra_args+=(--map-by node --mca plm_rsh_agent oarsh)
        fi
        # maybe auto-detect some hardware and decide on the right config
        # options ? should we really have to do that ?
    elif [ "$SLURM_NPROCS" ] ; then
        nnodes=$SLURM_NPROCS
    else
        nnodes=1
    fi

    ncores=$(egrep -c '^processor[[:space:]]+:' /proc/cpuinfo)

    case "$nnodes,$ncores,$family" in
        1,*,openmpi) 
            mpi_extra_args+=(-mca mtl ^psm2,ofi,cm --mca btl ^openib --bind-to core)
            set_choices_from_n $ncores
            ;;
        *,openmpi) 
            set_choices_from_n $nnodes
            ;;
        *)
            echo "Script does not know which mpi tests to enable"
            ;;
    esac
    mpi="${!mpi_magic}"
    if ! [ "$mpi" ] ; then
        echo "No MPI run possible for magic choice $mpi_magic ; no-op exit" >&2
        return
    fi
    _t="${mpi_magic}_mpi_args"[@]
    set `echo $mpi | tr 'x' ' '`
    if ! [ "$1" ] || ! [ "$2" ] ; then
        # should never happen.
        echo "Bad test configuration, mpi should be of the form \d+x\d+ for MPI test" >&2
        exit 1
    fi
    njobs=$(($1*$2))
    mpi_args_common+=(-n $njobs)
    mpirun=("$mpiexec" "${mpi_args_common[@]}" "${!_t}" "${mpi_extra_args[@]}")
    # pass on to subcalls
    export mpi
    export exporter_mpirun="${mpirun[@]@A}"
    export exporter_mpi_extra_args="${mpi_extra_args[@]@A}"
}