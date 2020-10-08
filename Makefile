COPS = -std=c99 -O2 -g -Wall -pedantic
STATIC=

sim: *.c *.h Makefile sim1 thumbulator
sim1:
	gcc $(COPS) -Wp,-w -D"RAM_START=0x40000000" -c sim_support.c


board: *.c *.h Makefile board1 thumbulator
board1:
	gcc $(COPS) -Wp,-w -D"RAM_START=0x20000000" -c sim_support.c

thumbulator:
	gcc $(COPS) -c sim_main.c
	gcc $(COPS) -c rsp-server.c
	gcc $(COPS) -c decode.c
	gcc $(COPS) -c exmemwb.c
	gcc $(COPS) -c exmemwb_arith.c
	gcc $(COPS) -c exmemwb_logic.c
	gcc $(COPS) -c exmemwb_mem.c
	gcc $(COPS) -c exmemwb_misc.c
	gcc $(COPS) -c exmemwb_branch.c
	gcc $(COPS) -c except.c
	gcc $(COPS) -c programpoints.c
	gcc $(COPS) -o sim_main$(STATIC) sim_support.o exmemwb_*.o exmemwb.o decode.o except.o programpoints.o rsp-server.o sim_main.o -lssl -lcrypto $(STATIC)
	rm -f *.o

clean :
	rm -f *.o
	rm -f sim_main
	rm -f *~
