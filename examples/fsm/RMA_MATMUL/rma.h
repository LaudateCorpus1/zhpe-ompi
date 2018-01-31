#ifndef _RMA_H_
# define _RMA_H_
#endif

#define RAND_RANGE (10)
void setup(int rank, int nprocs, int argc, char **argv,
           int *mat_dim_ptr, int *blk_dim_ptr, int *px_ptr, int *py_ptr,
           int *final_flag);
void init_mats(int mat_dim, double *win_mem,
               double **mat_a_ptr, double **mat_b_ptr, double **mat_c_ptr, int myseed);
void dgemm(double *local_a, double *local_b, double *local_c, int blk_dim);
void print_mat(double *mat, int mat_dim);
void check_mats(double *mat_a, double *mat_b, double *mat_c, int mat_dim);

