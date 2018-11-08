/**
 *
 * Copyright (c) 2017, King Abdullah University of Science and Technology
 * All rights reserved.
 *
 **/

/**
 *
 * @file timing_pdgeqdwh.c
 *
 *  QDWH is a high performance software framework for computing 
 *  the polar decomposition on distributed-memory manycore systems provided by KAUST
 *
 * @version 3.0.0
 * @author Dalal Sukkari
 * @author Hatem Ltaief
 * @date 2018-11-08
 *
 **/

#include <stdlib.h>
#include "polar.h"


/* Default values of parameters */
int nprow         = 1;
int npcol         = 1;
int lvec          = 1;
int rvec          = 1;
int n             = 5120;
int nb            = 128;
int mode          = 4;
double cond       = 9.0072e+15;
int optcond       = 0;
int start         = 5120;
int stop          = 5120;
int step          = 1;
int niter         = 1;
int check         = 0;
int verbose       = 0;

static inline double cWtime(void)
{
    struct timeval tp;
    gettimeofday( &tp, NULL );
    return tp.tv_sec + 1e-6 * tp.tv_usec;
}

void print_usage(void)
{
    fprintf(stderr,
            "======= QDWH timing using ScaLAPACK\n"
            " -p      --nprow         : Number of MPI process rows\n"
            " -q      --npcol         : Number of MPI process cols\n"
            " -jl     --lvec          : Compute left singular vectors\n"
            " -jr     --rvec          : Compute right singular vectors\n"
            " -n      --N             : Dimension of the matrix\n"
            " -b      --nb            : Block size\n"
            " -m      --mode          : [1:6] Mode from pdlatms used to generate the matrix\n"
            " -k      --cond          : Condition number used to generate the matrix\n"
            " -o      --optcond       : Estimate Condition number using QR\n"
            " -i      --niter         : Number of iterations\n"
            " -r      --n_range       : Range for matrix sizes Start:Stop:Step\n"
            " -c      --check         : Check the solution\n"
            " -v      --verbose       : Verbose\n"
            " -h      --help          : Print this help\n" );
}

#define GETOPT_STRING "p:q:x:y:n:b:m:i:o:r:Q,S:s:w:e:c:f:t:v:h"

static struct option long_options[] =
    {
        /* PaRSEC specific options */
        {"nprow",      required_argument,  0, 'p'},
        {"npcol",      required_argument,  0, 'q'},
        {"jl",         no_argument,        0, 'x'},
        {"lvec",       no_argument,        0, 'x'},
        {"jr",         no_argument,        0, 'y'},
        {"rvec",       no_argument,        0, 'y'},
        {"N",          required_argument,  0, 'n'},
        {"n",          required_argument,  0, 'n'},
        {"nb",         required_argument,  0, 'b'},
        {"b",          required_argument,  0, 'b'},
        {"mode",       required_argument,  0, 'm'},
        {"m",          required_argument,  0, 'm'},
        {"cond",       required_argument,  0, 'k'},
        {"k",          required_argument,  0, 'k'},
        {"optcond",    required_argument,  0, 'o'},
        {"o",          required_argument,  0, 'o'},
        {"i",          required_argument,  0, 'i'},
        {"niter",      required_argument,  0, 'i'},
        {"r",          required_argument,  0, 'r'},
        {"n_range",    required_argument,  0, 'r'},
        {"check",      no_argument,        0, 'c'},
        {"verbose",    no_argument,        0, 'v'},
        {"help",       no_argument,        0, 'h'},
        {"h",          no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };

static void parse_arguments(int argc, char** argv)
{
    int opt = 0;
    int c;
    int myrank_mpi;

    MPI_Comm_rank(MPI_COMM_WORLD, &myrank_mpi);

    do {
#if defined(HAVE_GETOPT_LONG)
        c = getopt_long_only(argc, argv, "",
                        long_options, &opt);
#else
        c = getopt(argc, argv, GETOPT_STRING);
        (void) opt;
#endif  /* defined(HAVE_GETOPT_LONG) */

        switch(c) {
        case 'p': nprow     = atoi(optarg); break;
        case 'q': npcol     = atoi(optarg); break;
        case 'n': n         = atoi(optarg); start = n; stop = n; step = 1; break;
        case 'b': nb        = atoi(optarg); break;
        case 'm': mode      = atoi(optarg); break;
        case 'k': cond      = atof(optarg); break;
        case 'o': optcond   = atof(optarg); break;
        case 'i': niter     = atoi(optarg); break;
        case 'r': get_range( optarg, &start, &stop, &step ); break;
        case 'c': check     = 1; break;
        case 'v': verbose   = 1; break;
        case 'h':
            if (myrank_mpi == 0) print_usage(); MPI_Finalize(); exit(0);
            break;
        default:
            break;
        }
    } while(-1 != c);
}

int main(int argc, char **argv) {


    int myrank_mpi, nprocs_mpi;
    int ictxt, myrow, mycol;
    int mloc, nloc, mlocW;
    int mpi_comm_rows, mpi_comm_cols;
    int i, j, k, iter, size, info_facto_mr, info_facto_dc, info_facto_qw, info_facto_el, info_facto_sl, info, iseed;
    int my_info_facto;
    int i0 = 0, i1 = 1;
    int lwork, liwork, ldw;

    /* Allocation for the input/output matrices */
    int descA[9], descH[9];
    double *A=NULL, *H=NULL;

    /* Allocation to check the results */
    int descAcpy[9], descC[9]; 
    double *Acpy=NULL, *C=NULL;

    /* Allocation for pdlatsm */
    double *Wloc1=NULL, *Wloc2=NULL, *D=NULL;

    double eps = LAPACKE_dlamch_work('e');
    int iprepad, ipostpad, sizemqrleft, sizemqrright, sizeqrf, sizeqtq,
        sizechk, sizesyevx, isizesyevx,
        sizesubtst, isizesubtst, sizetst,
        isizetst;
/**/

    double flops, GFLOPS;

    double orth_Uqw, berr_UHqw;
    double frobA;

    double alpha, beta;
    char *jobu, *jobvt;

    double my_elapsed_polarqdwh = 0.0, elapsed_polarqdwh = 0.0, sumtime_polarqdwh = 0.0;
    double max_time_polarqdwh  = 0.0, min_time_polarqdwh  = 1e20;

/**/

    if (verbose & myrank_mpi == 0) fprintf(stderr, "Program starts... \n");

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank_mpi);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs_mpi);

    if (verbose & myrank_mpi == 0) fprintf(stderr, "MPI Init done\n");

    parse_arguments(argc, argv);

    if (verbose & myrank_mpi == 0) fprintf(stderr, "Checking arguments done\n");

    Cblacs_get( -1, 0, &ictxt );
    Cblacs_gridinit( &ictxt, "R", nprow, npcol );
    Cblacs_gridinfo( ictxt, &nprow, &npcol, &myrow, &mycol );

    if (verbose & myrank_mpi == 0) fprintf(stderr, "BLACS Init done\n");

    if (myrank_mpi == 0) {
        fprintf(stderr, "# \n");
        fprintf(stderr, "# NPROCS %d P %d Q %d\n", nprocs_mpi, nprow, npcol);
        fprintf(stderr, "# niter %d\n", niter);
        fprintf(stderr, "# n_range %d:%d:%d mode: %d cond: %2.4e \n", start, stop, step, mode, cond);
        fprintf(stderr, "# \n");
    }

    if (verbose & myrank_mpi == 0) fprintf(stderr, "Range loop starts\n");


     // Begin loop over range
    for (size = start; size <= stop; size += step) {
        while ( (int)((double)size / (double)nb) < ( max(nprow , npcol) )){
            if (myrank_mpi == 0) fprintf(stderr, " Matrix size is small to be facrorized using this number of processors \n");
               size += step;
        }
        n = size; ldw = 2*n;
	mloc  = numroc_( &n, &nb, &myrow, &i0, &nprow );
	nloc  = numroc_( &n, &nb, &mycol, &i0, &npcol );
	mlocW = numroc_( &ldw, &nb, &myrow, &i0, &nprow );
        if (verbose & myrank_mpi == 0) fprintf(stderr, "Desc Init starts %d\n", mloc);

	descinit_( descA, &n, &n, &nb, &nb, &i0, &i0, &ictxt, &mloc, &info );
	descinit_( descAcpy, &n, &n, &nb, &nb, &i0, &i0, &ictxt, &mloc, &info );
	descinit_( descC, &n, &n, &nb, &nb, &i0, &i0, &ictxt, &mloc, &info );
	descinit_( descH, &n, &n, &nb, &nb, &i0, &i0, &ictxt, &mloc, &info );
        if (verbose & myrank_mpi == 0) fprintf(stderr, "Desc Init ends %d\n", mloc);

	A     = (double *)malloc(mloc*nloc*sizeof(double)) ;
	H     = (double *)malloc(mloc*nloc*sizeof(double)) ;
	C     = (double *)malloc(mloc*nloc*sizeof(double)) ;
	Acpy  = (double *)malloc(mloc*nloc*sizeof(double)) ;
	D     = (double *)malloc(n*sizeof(double)) ;

        
        /* Initialize the timing counters */
        my_elapsed_polarqdwh = 0.0, elapsed_polarqdwh = 0.0, sumtime_polarqdwh = 0.0; 
        max_time_polarqdwh  = 0.0, min_time_polarqdwh  = 1e20;

        /* Generate matrix by pdlatms */
        {
           char   *dist = "N"; /* NORMAL( 0, 1 )  ( 'N' for normal ) */
           int    iseed[4] = {1, 0, 0, 1};
           char   *sym = "P"; /* The generated matrix is symmetric, with
                                eigenvalues (= singular values) specified by D, COND,
                                MODE, and DMAX; they will not be negative.
                                "N" not supported. */
           //int    mode = 4; /* sets D(i)=1 - (i-1)/(N-1)*(1 - 1/COND) */
           //double cond = 1.0/eps;
           double dmax = 1.0;
           int    kl   = n;
           int    ku   = n;
           char   *pack = "N"; /* no packing */
           int    order = n;
           int    info;
         
           pdlasizesep_( descA, 
                         &iprepad, &ipostpad, &sizemqrleft, &sizemqrright, &sizeqrf, 
                         &lwork, 
                         &sizeqtq, &sizechk, &sizesyevx, &isizesyevx, &sizesubtst, 
                         &isizesubtst, &sizetst, &isizetst );
           if (verbose & myrank_mpi == 0) fprintf(stderr, "Setting lwork done\n");
           Wloc1 = (double *)calloc(lwork,sizeof(double)) ;
           pdlatms_(&n, &n, dist,
                    iseed, sym, D, &mode, &cond, &dmax,
                    &kl, &ku, pack, 
                    A, &i1, &i1, descA, &order, 
                    Wloc1, &lwork, &info);
           if (verbose & myrank_mpi == 0) fprintf(stderr, "MatGen done\n");
           if (info != 0) {
               fprintf(stderr, "An error occured during matrix generation: %d\n", info );
               return EXIT_FAILURE;
           }
           pdlacpy_( "All", &n, &n, 
                     A, &i1, &i1, descA, 
                     Acpy, &i1, &i1, descAcpy ); 
           frobA  = pdlange_ ( "f", &n, &n, A, &i1, &i1, descA, Wloc1);
           beta = 0.0;

           if (verbose & myrank_mpi == 0) fprintf(stderr, "Copy to Acpy done\n");

           free( Wloc1 );
        }

        if (myrank_mpi == 0) fprintf(stderr, "\n\n");
        if (myrank_mpi == 0) fprintf(stderr, "/////////////////////////////////////////////////////////////////////////\n");
        if (myrank_mpi == 0) fprintf(stderr, "/////////////////////////////////////////////////////////////////////////\n");
        if (myrank_mpi == 0) fprintf(stderr, "/////////////////////////////////////////////////////////////////////////\n");

        // QDWH
	//if ( qwmr || qwdc || qwel || polarqdwh) {
        jobu   = lvec ? "V" : "N";
        int lWork1, lWork2, lWi;
        Wloc1  = (double *)calloc(1,sizeof(double)) ;
        Wloc2  = (double *)calloc(1,sizeof(double)) ;
        lWork1 = -1; 
        lWork2 = -1; 

        pdgeqdwh( "H", n, n,
                  A, i1, i1, descA, 
                  H, i1, i1, descH, 
                  //NULL, lWork, //debug
                  Wloc1, lWork1,
                  Wloc2, lWork2,
                  &my_info_facto);

        lWork1 = Wloc1[0];
        lWork2 = Wloc2[0];

        
        //Wloc  = (double *)malloc(lWork*n*sizeof(double));
	Wloc1  = (double *)malloc((lWork1*nloc)*sizeof(double));
	Wloc2  = (double *)malloc((lWork2*nloc)*sizeof(double));

        //int MB3 = 3*n;
        //int mlocW3 = numroc_( &MB3, &nb, &myrow, &i0, &nprow );
        //mlocW3 = ((mlocW3+(nb-1))/nb)*nb;
	//Wloc  = (double *)malloc((mlocW3*nloc)*sizeof(double));
               

        for (iter = 0; iter < niter; iter++) {
            pdlacpy_( "All", &n, &n, 
                      Acpy, &i1, &i1, descAcpy, 
                      A,    &i1, &i1, descA ); 
            flops = 0.0;
            if (verbose & myrank_mpi == 0) fprintf(stderr, "\nQDWH + ScaLAPACK EIG done\n");
            if (verbose & myrank_mpi == 0) fprintf(stderr, "QDWH starts...\n");

            /*
             * Find polar decomposition using QDWH. 
             * C contains the positive-definite factor. 
             * A contains the orthogonal polar factor.
             */
            my_elapsed_polarqdwh   = 0.0;
            my_elapsed_polarqdwh   =- MPI_Wtime();

            pdgeqdwh( "H", n, n,
                      A, i1, i1, descA, 
                      H, i1, i1, descH, 
                      //NULL, lWork, //debug
                      Wloc1, lWork1,
                      Wloc2, lWork2,
                      &my_info_facto);

            my_elapsed_polarqdwh   += MPI_Wtime();
            MPI_Allreduce( &my_elapsed_polarqdwh, &elapsed_polarqdwh, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            sumtime_polarqdwh += elapsed_polarqdwh;
            if ( elapsed_polarqdwh >= max_time_polarqdwh ) { max_time_polarqdwh = elapsed_polarqdwh;} 
            if ( elapsed_polarqdwh <= min_time_polarqdwh ) { min_time_polarqdwh = elapsed_polarqdwh;} 
	    MPI_Allreduce( &my_info_facto, &info_facto_qw, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

	    MPI_Allreduce( &my_info_facto, &info_facto_qw, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

            if (verbose & myrank_mpi == 0) fprintf(stderr, "QDWH ends...\n");
            if (verbose & check & myrank_mpi == 0) fprintf(stderr, "Testing QDWH starts...\n");
            /*
             * Checking the polar factorization
             */
            if(check ){
               /*
                * checking orthogonality of Up
                */ 
                alpha = 0.0; beta = 1.0;
                pdlaset_( "G", &n, &n, &alpha, &beta, C, &i1, &i1, descC);
                alpha = 1.0; beta = -1.0;
                pdgemm_( "T", "N", &n, &n, &n, 
                         &alpha, 
                         A, &i1, &i1, descA, 
                         A, &i1, &i1, descA, 
                         &beta, 
                         C, &i1, &i1, descC);
                orth_Uqw  = pdlange_ ( "f", &n, &n, C, &i1, &i1, descC, Wloc1)/frobA;
               /*
                * checking the factorization |A-Up*H|
                */ 
                pdlacpy_( "A", &n, &n, Acpy, &i1, &i1, descAcpy, C, &i1, &i1, descC );
                alpha = 1.0; beta = -1.0;
                pdgemm_( "N", "N", &n, &n, &n, 
                         &alpha, 
                         A, &i1, &i1, descA, 
                         H, &i1, &i1, descH, 
                         &beta, 
                         C, &i1, &i1, descC);
                berr_UHqw  = pdlange_ ( "f", &n, &n, C, &i1, &i1, descC, Wloc1)/frobA;
            }
        }
        if (  myrank_mpi == 0) {
            fprintf(stderr, "# QDWH \n"); 
            fprintf(stderr, "#\n");
	    fprintf(stderr, "# \tN     \tNB   \tNP   \tP   \tQ   \tGflop/s \tAvg-Time     \tMax-Time    \tMin-Time    \tBerr_UpH  \tOrth_Up  \tinfo     \n");
	    fprintf(stderr, "   %6d \t%4d \t%4d \t%3d \t%3d \t%8.2f", n, nb, nprocs_mpi, nprow, npcol, flops/1e9/min_time_polarqdwh);
	    fprintf(stderr, "\t%6.2f \t\t%6.2f \t\t%6.2f \t\t%2.4e \t%2.4e \t%d \n", sumtime_polarqdwh/niter, max_time_polarqdwh, min_time_polarqdwh, berr_UHqw, orth_Uqw,info_facto_qw);
            fprintf(stderr, "/////////////////////////////////////////////////////////////////////////\n");
         }



        if (myrank_mpi == 0) fprintf(stderr, "/////////////////////////////////////////////////////////////////////////\n");

	free( A );
	free( Acpy );
	free( C );
	free( H );
	free( D );
        free(Wloc1);
        free(Wloc2);
        if (verbose & myrank_mpi == 0) fprintf(stderr, "Free matrices done\n");
    } // End loop over range


        if (verbose & myrank_mpi == 0) fprintf(stderr, "Range loop ends\n");

	blacs_gridexit_( &i0 );
	MPI_Finalize();
        if (verbose & myrank_mpi == 0) fprintf(stderr, "Program ends...\n");
	return 0;
}
