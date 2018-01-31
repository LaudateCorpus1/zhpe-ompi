/*copyright (c) 2018, Raj Shukla (rajexplo@gmail.com)
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

#include "stencil.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>

// Matrix*Matrix Multiplication Routine



void setup(int rank, int nprocs, int argc, char **argv, int* iter_ptr, int *px_ptr, int *py_ptr, int *n_ptr, int *flag)
{
    int  n, iter, px, py;
    (*flag)=0;

    if (argc < 5) {
        if (!rank) printf("usage: stencil <n> <iter> <px> <py>\n");
        (*flag)=1;
        return;
    }

    n = atoi(argv[1]);    /* Number of grid: n x n */
    iter = atoi(argv[2]);    /* block dimension */
    px = atoi(argv[3]);         /* 1st dim processes */
    py = atoi(argv[4]);         /* 2st dim processes */

    if (px * py != nprocs)
        MPI_Abort(MPI_COMM_WORLD, 1);
    if (n % px != 0)
        MPI_Abort(MPI_COMM_WORLD, 1);
    
    if (n % py != 0)
      MPI_Abort(MPI_COMM_WORLD, 1);

    (*n_ptr) = n;
    (*iter_ptr) = iter;
    (*px_ptr) = px;
    (*py_ptr) = py;
}


void fd_solver(double *temp_ptr, double *uold, double *unew, int bx, int by){

int i ,j;

double temp=0.0;

for(i=1; i < bx+1; ++i){
        for(j=1; j < by+1; ++j){

          unew[j*(bx+2)+i]=unew[j*(bx+2)+i]/2.0 + (uold[j*(bx+2)+(i-1)] + uold[j*(bx+2)+(i+1)] + uold[(j-1)*(bx+2)+ i] + uold[(j+1)*(bx+2)+i])/8.0 ;

    temp += unew[j*(bx+2) +i];

        }
}

(*temp_ptr)=temp;



}

void initialize(int bx, int by, int n, int localData[][2], int datapos[][2], int *dataPtr, int offsetx, int offsety ){
 
  int x, y;
  int initData=0;
  datapos[0][0] = n/2;
  datapos[0][1] = n/2;
  
   x=datapos[0][0] - offsetx;
   y=datapos[0][0] - offsety;

   //if(x >= 0 && x < bx && y >=0 && y < by){
   if(x >= 0 && x <= bx && y >=0 && y <= by){
   
   localData[initData][0]=x+1;
   localData[initData][1]=y+1;
   initData++;
   }
 
     (*dataPtr)=initData;
}

