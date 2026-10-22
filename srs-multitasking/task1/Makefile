CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -O2
TARGETS = procinfo procfd fddemo

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) demo.log

.PHONY: all clean
