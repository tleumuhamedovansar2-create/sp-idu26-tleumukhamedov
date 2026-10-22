CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -O2
LDLIBS  = -lrt

all: shmcnt

shmcnt: shmcnt.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

bench: shmcnt
	./shmcnt -b -i 500000

clean:
	rm -f shmcnt shmcnt_bench.csv

.PHONY: all clean bench
