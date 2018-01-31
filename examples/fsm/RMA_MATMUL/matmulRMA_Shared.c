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



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <math.h>
#include <time.h>
#include "mpi.h"
#include "rma.h"


int main(int argc, char **argv)
{

    int rank, nprocs; //Global communicator
    int srank, snprocs; //Shared communicators
    int mat_dim, blk_dim, blk_num;
    int px, py, bx, by, rx, ry;
    int flag;
    double *mat_a, *mat_b, *mat_c;
    double *local_a, *local_b, *local_c;
    MPI_Aint disp_a, disp_b, disp_c;
    MPI_Aint offset_a, offset_b, offset_c;
    int i, j, k;
    int global_i, global_j;
    int myseed;

    double *win_mem;
    MPI_Win win;

    double t1, t2;

    MPI_Datatype blk_dtp;

    /* initialize MPI environment */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);


   /* Initalized shared memory comunicator */
   MPI_Comm shmcomm = MPI_COMM_WORLD;

   MPI_Comm_size(shmcomm, &snprocs);
   MPI_Comm_rank(shmcomm, &srank);

   /*Code only for comm_world*/
  
    if( nprocs != snprocs)
    {
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if ( argc > 5) {
        myseed = atoi(argv[5]);
    } else {
        myseed = 1;
    }

    /* argument checking and setting */
    setup(rank, nprocs, argc, argv, &mat_dim, &blk_dim,
          &px, &py, &flag);
    if (flag == 1) {
        MPI_Finalize();
        exit(0);
    }

    /* number of blocks in one dimension */
    blk_num = mat_dim / blk_dim;

    /* determine my coordinates (x,y) -- r=x*a+y in the 2d processor array */
    rx = rank % px;
    ry = rank / px;

    /* determine distribution of work */
    bx = blk_num / px;
    by = blk_num / py;

    if (!rank) {
        /* create Shared Memory window */
        MPI_Win_allocate_shared(3*mat_dim*mat_dim*sizeof(double), sizeof(double),
                         MPI_INFO_NULL, shmcomm, &win_mem, &win);

        /* initialize matrices */
        init_mats(mat_dim, win_mem, &mat_a, &mat_b, &mat_c, myseed);
        MPI_Win_sync(win);
    }
    else {
        MPI_Win_allocate_shared(0, sizeof(double), MPI_INFO_NULL, shmcomm,
                         &win_mem, &win);
    }

    /* allocate local buffer */
    MPI_Alloc_mem(3*blk_dim*blk_dim*sizeof(double), MPI_INFO_NULL, &local_a);
    local_b = local_a + blk_dim * blk_dim;
    local_c = local_b + blk_dim * blk_dim;

    /* create block datatype */
    MPI_Type_vector(blk_dim, blk_dim, mat_dim, MPI_DOUBLE, &blk_dtp);
    MPI_Type_commit(&blk_dtp);

    disp_a = 0;
    disp_b = disp_a + mat_dim * mat_dim;
    disp_c = disp_b + mat_dim * mat_dim;

    MPI_Barrier(shmcomm);

    t1 = MPI_Wtime();

    MPI_Win_lock_all(0, win);

    for (i = 0; i < by; i++) {
        for (j = 0; j < bx; j++) {

            global_i = i + by * ry;
            global_j = j + bx * rx;

            /* get block from mat_a */
            offset_a = global_i * blk_dim * mat_dim + global_j * blk_dim;

            MPI_Get(local_a, blk_dim*blk_dim, MPI_DOUBLE,
                      0, disp_a+offset_a, 1, blk_dtp, win);

            MPI_Win_flush(0, win);

            for (k = 0; k < blk_num; k++) {

                /* get block from mat_b */
                offset_b = global_j * blk_dim * mat_dim + k * blk_dim;
                MPI_Get(local_b, blk_dim*blk_dim, MPI_DOUBLE,
                        0, disp_b+offset_b, 1, blk_dtp, win);

                MPI_Win_flush(0, win);


                /* local computation */
                dgemm(local_a, local_b, local_c, blk_dim);

                /* accumulate block to mat_c */
                offset_c = global_i * blk_dim * mat_dim + k * blk_dim;

                MPI_Accumulate(local_c, blk_dim*blk_dim, MPI_DOUBLE,
                               0, disp_c+offset_c, 1, blk_dtp, MPI_SUM, win);

                MPI_Win_flush(0, win);
            }
        }
    }

    MPI_Win_unlock_all(win);

    t2 = MPI_Wtime();

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        check_mats(mat_a, mat_b, mat_c, mat_dim);

        print_mat(mat_a, mat_dim);
        print_mat(mat_b, mat_dim);
        print_mat(mat_c, mat_dim);

    }

    MPI_Type_free(&blk_dtp);
    MPI_Free_mem(local_a);
    MPI_Win_free(&win);
    MPI_Finalize();
}
