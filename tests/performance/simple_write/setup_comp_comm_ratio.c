/* 
 * ADIOS is freely available under the terms of the BSD license described
 * in the COPYING file in the top level directory of this source distribution.
 *
 * Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
 */

/* Application that performs computation and communication to a user-given ratio.
   Then it performs output I/O with posix, parallel hdf5 and adios.
   It writes a global 2D array of N*NX x NY, where N is the number of processes and
   NX, NY are user-given sizes.
   
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
//#include <unistd.h>
//#include <fcntl.h>
#include <errno.h>
#include "mpi.h"
#include "comp_comm.h"


// User parameters
int    tcomp;             // Time for computation in one iteration, in millisec
int    tcomm;             // Time for communication in one iteration, in millisec
int    nx;                // array size in x direction
int    ny;                // array size in y direction


void printUsage(char *prgname)
{
    printf("\nUsage: mpirun -np <N> %s <tcomp>  <tcomm>  <nx>  <ny>\n\n"
           "  <tcomp>   computation time per iteration (in millisec)\n"
           "  <tcomm>   communication time per iteration (in millisec)\n"
           "  <nx> <ny> size of a 2D array generated by each process\n"
           "\n"
        ,prgname);
}

int convert_arg_to_int (char **argv, int argpos, char *name, int *value)
{
    char *end;
    errno = 0;
    *value = strtol(argv[argpos], &end, 10);
    if (errno || (end != 0 && *end != '\0')) {
        printf ("ERROR: Invalid argument for %s: '%s'\n", name, argv[argpos]);
        printUsage(argv[0]);
        return 1;
    }
    return 0;
}

int processArgs(int argc, char ** argv)
{
    if (argc < 5) {
        printUsage (argv[0]);
        return 1;
    }

    if (convert_arg_to_int (argv, 1, "<tcomp>", &tcomp)) 
        return 1;
    if (convert_arg_to_int (argv, 2, "<tcomm>", &tcomm)) 
        return 1;
    if (convert_arg_to_int (argv, 3, "<nx>", &nx)) 
        return 1;
    if (convert_arg_to_int (argv, 4, "<ny>", &ny)) 
        return 1;

    return 0;
}

// other global variables
MPI_Comm  comm;
int    rank;
int    nproc;             // # of total procs
int    ncomp;             // number of computation units to get tcomp (without I/O)
int    ncomm;             // number of communication units to get tcomm (without I/O)
double * data;            // data array to do calc on it and output

// communication array
#define NELEMS 8192    // 32 KB of integers
int    sendbuf[NELEMS];     // array to do communication with it
int    recvbuf[NELEMS];     // array to do communication with it

void data_init()
{
    int i,j;
    for (i = 0; i < nx; i++) {
        for (j = 0; j < ny; j++) {
            data[i*ny + j] = 1.0*rank;
        }
    }
}


void determine_computation_ratio ()
{
    /* run computation and communication for a while and establish
       how many times of each needs to be performed to get the desired ratio
       tcomp --> comp_n and tcomm --> comm_n
    */
    double wb, ttotal;
    // run for tcomp milliseconds
    ncomp = 0;
    ttotal = 0.0;
    MPI_Barrier (comm);
    if (!rank) printf ("  # of calc units to get %g sec computation...", tcomp/1000.0);
    while (ttotal < tcomp/1000.0) {
        wb = MPI_Wtime();
        do_calc_unit(data, nx, ny);
        ttotal += MPI_Wtime() - wb;
        ncomp++;
    }
    if (!rank) printf (" = %d. avg unit time = %g sec\n", ncomp, ttotal/ncomp);
    MPI_Barrier (comm);

    // run for tcomm milliseconds
    ncomm = 0;
    ttotal = 0.0;
    if (!rank) printf ("  # of comm units to get %g sec communication...", tcomm/1000.0);
    while (ttotal < tcomm/1000.0) {
        MPI_Barrier (comm);
        wb = MPI_Wtime();
        do_comm_unit(comm);
        ttotal += MPI_Wtime() - wb;
        // use rank 0's ttotal for everyone
        MPI_Bcast (&ttotal, 1, MPI_DOUBLE, 0, comm);
        ncomm++;
    }
    if (!rank) printf (" = %d. avg unit time = %g sec\n", ncomm, ttotal/ncomm);
    // use rank 0's ncomm for everyone
    MPI_Bcast (&ncomm, 1, MPI_INTEGER, 0, comm);
}


int main (int argc, char ** argv ) 
{
    int i;
    MPI_Init (&argc, &argv);
    MPI_Comm_dup (MPI_COMM_WORLD, &comm);
    MPI_Comm_rank (comm, &rank);
    MPI_Comm_size (comm, &nproc);
    comp_comm_init(comm);

    if (processArgs(argc, argv)) {
        return 1;
    }

    if (!rank) {
        printf ("Setup parameters:\n");
        printf ("  computation:    %g sec per iteration\n", tcomp/1000.0);
        printf ("  commmunication: %g sec per iteration\n", tcomm/1000.0);
        printf ("  data per process: %d x %d doubles = %lld bytes\n", nx, ny, sizeof(double) * nx * (uint64_t) ny);
        printf ("  number of processors: %d\n", nproc);
    }

    data = (double*) malloc (sizeof(double) * nx * (size_t) ny);

    /* Test phase to calculate ncomp and ncomm */
    data_init();
    for (i=1; i<=20; i++) {
        if (!rank) printf ("Testing speed (warm up %d)...\n", i);
        determine_computation_ratio ();
    }

    if (!rank) printf ("Testing speed (to determine calc/comm units)...\n");
    data_init();
    determine_computation_ratio ();

    if (!rank) {
        printf ("Test phase completed.\n");
        printf ("  # of computation units in each iteration:   %d\n", ncomp);
        printf ("  # of communication units in each iteration: %d\n", ncomm);
    }

    free (data);
    MPI_Barrier (comm);
    MPI_Finalize ();
    return 0;
}
