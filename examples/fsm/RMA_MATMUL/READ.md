This test adapts Khemraj Shukla's shared memory matrix-matrix
multiplication code to serve as a test for MPI fabric-shared memory
programming.

Khemraj Shukla's git repository: https://github.com/rajexplo/RMA_MATMUL

This package computes matrix-matrix multiplication using one-sided
communication and Shared Memory collectives.

To run this as a test, edit the hostfile to reflect the actual hosts
you would like to use for testing, then invoke:

./run_matmulRMA_test.sh


%---- original read.md below -----
This package compute matrix-matrix multiplication using one-sided communication and Shared Memory collectives.
The code can be run as follows
mpirun -np 4 ./matmulRMA 4(matrix_size) 2(block size) 2(size of process in x dimesnion) 2(size of processor in y Dimesnion)

mpirun --host node01,node02,node03,node04 --bind-to none `pwd`/matmulRMA 4 2 2 2
