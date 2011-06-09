# *Should set CC to the location of the klee-gcc compile script (e.g., cloud9/compile/klee-gcc)

TARGETS=cs1 cs2-sympackets
all: $(TARGETS)

cs1: cs1.c
	$(CC) -c $?
	$(CC) cs1.o -o $@ -lpthread

cs2-sympackets: cs2-sympackets.c
	$(CC) -c $? -I../../include
	$(CC) cs2-sympackets.o -o $@ -lpthread

clean:
	rm -f $(TARGETS) *.bc *.o *~ 

