CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -O2

all: race msort race_tsan msort_tsan

race: race.c
	$(CC) $(CFLAGS) -o $@ $<

# версия для ThreadSanitizer: -O1 -g, чтобы в отчёте были строки исходника
race_tsan: race.c
	$(CC) -Wall -Wextra -std=c11 -pthread -O1 -g -fsanitize=thread -o $@ $<

# отладочная версия для valgrind --tool=helgrind
race_dbg: race.c
	$(CC) -Wall -Wextra -std=c11 -pthread -O0 -g -o $@ $<

msort_tsan: msort.c
	$(CC) -Wall -Wextra -std=c11 -pthread -O1 -g -fsanitize=thread -o $@ $<

msort: msort.c
	$(CC) $(CFLAGS) -o $@ $<

bench: msort
	./msort -b -r 5

clean:
	rm -f race race_tsan race_dbg msort msort_tsan msort_bench.csv

.PHONY: all clean bench
