CC=mpicc
CFLAGS=-g

.PHONY: clean

matmulRMA: matmulRMA_Shared.o rma.o
	$(CC) -g -o matmulRMA matmulRMA_Shared.o rma.o

matmulRMA.o: matmulRMA_Shared.c rma.h
	$(CC) -g -c matmulRMA_Shared.c 

rma.o: rma.c rma.h
	$(CC) -g -c rma.c

clean:
	rm -f matmulRMA matmulRMA_Shared.o rma.o

