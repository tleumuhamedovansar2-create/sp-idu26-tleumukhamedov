CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -O2
LDLIBS  = -lm

all: server client clients

server: server.o eval.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
client: client.o
	$(CC) $(CFLAGS) -o $@ $^
clients: clients.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c common.h eval.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f server client clients *.o

.PHONY: all clean
