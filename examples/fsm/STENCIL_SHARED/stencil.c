/*
 * Copyright (c) 2018, Raj Shukla (rajexplo@gmail.com). All rights reserved.
 * Copyright (c) 2017-2018 Hewlett Packard Enterprise Development LP.  All rights reserved.

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
#include "stencil.h"

/* The code solves the heat equation using 4 point stencil.
 * Following is distribution of block for domain decomposoition.
 *
 *  ++++++++++++++++++++
 *  +        + +       +
 *  +        + +       +
 *  +-- bx-- + +       +
 *  +        + +       +
 *  ++++++++++ +++++++++
 *       
 *  ++++++++++ ++++++++++
 *  +        + +   |    + 
 *  +        + +   |    +
 *  + --bx-- + +   by   +
 *  +        + +   |    +
 *  ++++++++++ ++++++++++
 *  
 *  
 *
 *
 *



*/   
int main(int argc, char **argv)
{
    int rank, nprocs; //Global communicator
    int srank, snprocs; //Shared communicators
    int n, niter;  //Number of grid points in one direction, Num of time updates
    int px, py, bx, by, rx, ry;  // px: process and rankx process and rank in x direction 
    int flag;
    int offsetx, offsety;
    int east, west, north, south;
    
    
    MPI_Aint sz; 
    int disp;
    int i, j, k;

    double *win_mem;
    MPI_Win win;
    double *memshm;
    double *unew;
    double *uold;
    double t1, t2;
    double *nptr, *nptr2;
    double *sptr, *sptr2;
    double *eptr, *eptr2;
    double *wptr, *wptr2;
    double temper; 
    double total_temper; 
    int initdata[1][2];
    int dataPtr;
    int localData[1][2];
    int iter;

    /* initialize MPI environment */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

   /* Initalized shared memory comunicator to MPI_COMM_WORLD because all visible nodes can access FAM */
   MPI_Comm shmcomm = MPI_COMM_WORLD;

   MPI_Comm_size(shmcomm, &snprocs);
   MPI_Comm_rank(shmcomm, &srank);

   /*Code only for comm_world*/
  
    if( nprocs != snprocs)
    {
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    

    /* argument checking and setting */
    
   setup(rank, nprocs, argc, argv, &niter, &px, &py, &n, &flag);
    
    if (flag == 1) {
        MPI_Finalize();
        exit(0);
    }
   

    /* determine my coordinates (x,y) -- r=x*a+y in the 2d processor array */
    rx = rank % px;
    ry = rank / px;

    
    
    
    /* determine distribution of work */
    bx = n / px;
    by = n / py;
    offsetx=rx*bx;  // For equal Load Balancing in X-dimension
    offsety=ry*by;  // For equal load balancing in Y direction

   
    
    // Neigborning process for filling up the halo zone
   //North Neighbor
    north=px*(ry-1) + rx;
    
    if (ry -1 < 0){
      north=MPI_PROC_NULL;
    }
   // South Neighbor
    
    south=px*(ry+1) + rx;

    if (ry + 1 >= py){
      south=MPI_PROC_NULL;
    }

    // East Neighbor    
    east=px*ry + (rx+1);

    if (rx + 1 >= px){
      east=MPI_PROC_NULL;
    }
// West Neighbor
 west=px*ry + (rx-1);

    if (rx - 1 < 0){
      west=MPI_PROC_NULL;
    }

  
    int sizep=(bx+2)*(bx+2);//Size of the data Block at each processes


  // Alocate shared memory of size twice of old data to accomadate the Old and new data over the shared memory space

  MPI_Win_allocate_shared(2*sizep*sizeof(double),1, MPI_INFO_NULL, shmcomm, &memshm, &win);
        unew=memshm;
        uold=memshm+sizep;
        MPI_Win_shared_query(win, north, &sz, &disp, &nptr); // Shared memory region for neighboring process
        MPI_Win_shared_query(win, south, &sz, &disp, &sptr); 
        MPI_Win_shared_query(win, east, &sz, &disp, &eptr); // Shared memory region for neighbor
        MPI_Win_shared_query(win, west, &sz, &disp, &wptr); 
        nptr2=nptr+sizep;
        sptr2=sptr+sizep;
        eptr2=eptr+sizep;
        wptr2=wptr+sizep; 
       
        initialize(bx, by, n, localData, initdata, &dataPtr,  offsetx, offsety );
        
      

        t1= MPI_Wtime();
        
        MPI_Win_lock_all(0, win);

      
      for(iter=0; iter < niter; ++iter){
      
         uold[localData[0][1]*(bx+2)+localData[0][0]] +=-1.0;


         MPI_Win_sync(win);

         MPI_Barrier(shmcomm);

         if( north !=MPI_PROC_NULL){
         
         for(i=0; i < bx; ++i){
           uold[i+1]=nptr2[by*(bx+2)+i+1];
         }
       }
      
      if( south != MPI_PROC_NULL){
         
         for(i=0; i < bx; ++i){
           uold[(by+1)*(bx+2)+i+1]=sptr2[(bx+2)+i+1];
         }
      }
      
       if( east != MPI_PROC_NULL){
         
         for(i=0; i < by; ++i){
           uold[(i+1)*(bx+2)+bx+1]=eptr2[(i+1)*(bx+2)+1];
         }
      }
      
       if( west != MPI_PROC_NULL){
         
         for(i=0; i < by; ++i){
           uold[(i+1)*(bx+2)]=wptr2[(i+1)*(bx+2)+bx];
         }
      }
      
      fd_solver(&temper, uold, unew, bx, by);
      printf("At Rank %d Heat Value is %10.3f\n", rank, temper); 
      double *temp_ptr;

      temp_ptr=unew;
      unew=uold;
      uold=temp_ptr;
      }
      
      MPI_Win_unlock_all(win);
      t2=MPI_Wtime();
      
      MPI_Allreduce(&temper, &total_temper, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

      if(!rank){
         printf("Final Result is %10.6f\n", total_temper); 
         printf("Truncated Final Result is %d\n", (int) total_temper); 
         printf("Array is \n");
      for (i =0; i < n; ++i){
          for (j=0; j < n; j++){
           printf("%10.6f\t", unew[ i*n + j ]);
      }
      printf("\n");
   }
      
 }
      

       MPI_Win_free(&win);

       /* MPI_Comm_free(&shmcomm); */

       MPI_Finalize();
}
