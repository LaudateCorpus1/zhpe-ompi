/* 
 * Copyright (c) 2018, Raj Shukla (rajexplo@gmail.com)
 * Copyright (c) 2017-2018 Hewlett Packard Enterprise Development LP.  All rights reserved.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

//
#include "rma.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>

// Matrix*Matrix Multiplication Routine
void dgemm(double *local_a, double *local_b, double *local_c, int blk_dim)
{
    int i, j, k;


    
   for(i=0; i < blk_dim; i++){
      for(j=0; j < blk_dim; j++){
	local_c[i*blk_dim + j]=0.0;
   }
}

    for (j = 0; j < blk_dim; j++) {
        for (i = 0; i < blk_dim; i++) {
            for (k = 0; k < blk_dim; k++)
                local_c[j+i*blk_dim] += local_a[k+i*blk_dim] * local_b[j+k*blk_dim];
        }
    }
}


void print_mat(double *mat, int mat_dim)
{
    int i, j;
    for (i = 0; i < mat_dim; i++) {
        for (j = 0; j < mat_dim; j++) {
            printf("%.4f ", mat[j+i*mat_dim]);
        }
        printf("\n");
    }
    printf("\n");
}
void init_mats(int mat_dim, double *win_mem,
               double **mat_a_ptr, double **mat_b_ptr, double **mat_c_ptr, int myseed)
{
    int i, j;
    double *mat_a, *mat_b, *mat_c;

    /* srand(time(NULL)); */
    /* Setting to a known seed so that we can test */
    srand(myseed);

    mat_a = win_mem;
    mat_b = mat_a + mat_dim * mat_dim;
    mat_c = mat_b + mat_dim * mat_dim;

    for (j = 0; j < mat_dim; j++) {
        for (i = 0; i < mat_dim; i++) {
            mat_a[j+i*mat_dim] = (double) rand() / (RAND_MAX / RAND_RANGE + 1);
            mat_b[j+i*mat_dim] = (double) rand() / (RAND_MAX / RAND_RANGE + 1);
            mat_c[j+i*mat_dim] = (double) 0.0;
        }
    }

    (*mat_a_ptr) = mat_a;
    (*mat_b_ptr) = mat_b;
    (*mat_c_ptr) = mat_c;
}



void setup(int rank, int nprocs, int argc, char **argv,
           int *mat_dim_ptr, int *blk_dim_ptr, int *px_ptr, int *py_ptr, int *flag)
{
    int mat_dim, blk_dim, px, py;
    (*flag)=0;

    if (argc < 5) {
        if (!rank) printf("usage: ga_mpi <m> <b> <px> <py>\n");
        (*flag)=1;
        return;
    }

    mat_dim = atoi(argv[1]);    /* matrix dimension */
    blk_dim = atoi(argv[2]);    /* block dimension */
    px = atoi(argv[3]);         /* 1st dim processes */
    py = atoi(argv[4]);         /* 2st dim processes */

    if (px * py != nprocs)
        MPI_Abort(MPI_COMM_WORLD, 1);
    if (mat_dim % blk_dim != 0)
        MPI_Abort(MPI_COMM_WORLD, 1);
    if ((mat_dim / blk_dim) % px != 0)
        MPI_Abort(MPI_COMM_WORLD, 1);
    if ((mat_dim / blk_dim) % py != 0)
        MPI_Abort(MPI_COMM_WORLD, 1);

    (*mat_dim_ptr) = mat_dim;
    (*blk_dim_ptr) = blk_dim;
    (*px_ptr) = px;
    (*py_ptr) = py;
}


void check_mats(double *mat_a, double *mat_b, double *mat_c, int mat_dim)
{
    int i, j, k;
    int sentinal = 0;
    double temp_c;
    double diff, max_diff = 0.0;

    for (j = 0; j < mat_dim; j++) {
        for (i = 0; i < mat_dim; i++) {
            temp_c = 0.0;
            for (k = 0; k < mat_dim; k++)
                temp_c += mat_a[k+i*mat_dim] * mat_b[j+k*mat_dim];
            diff = mat_c[j+i*mat_dim] - temp_c;
            if (fabs(diff) > 0.00001) {
                sentinal = 1;
                if (fabs(diff) > fabs(max_diff))
                    max_diff = diff;
            }
        }
    }

   if (sentinal==1)
        printf("\nTEST FAILED: (%.5f MAX diff)\n\n", max_diff);
    else
        printf("\nCheck_mats passed.\n\n");
}

