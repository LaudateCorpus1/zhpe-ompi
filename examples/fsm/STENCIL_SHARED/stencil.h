#ifndef _RMA_H_
# define _RMA_H_
#endif

#define RAND_RANGE (10)
void setup(int rank, int nprocs, int argc, char **argv, int* iter_ptr, int *px_ptr, int *py_ptr, int *n_ptr, int *flag);



void fd_solver(double *temp_ptr, double *uold, double *unew, int bx, int by);

void initialize(int bx, int by, int n, int localData[][2], int datapos[][2], int *dataPtr, int offsetx, int offsety );
 
