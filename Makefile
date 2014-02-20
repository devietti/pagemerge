
FLAGS=-Wall -O3 -std=c99 -march=core2 -msse4.2 -DPERCENT_DIFF_PAGES=80 -DPERCENT_DIFF_BYTES_PER_PAGE=80

#FLAGS=-Wall -O0 -g -std=c99 -march=core2 -msse4.2 -DPERCENT_DIFF_PAGES=80 -DPERCENT_DIFF_BYTES_PER_PAGE=80

all: byte.bin byte-pf.bin word.bin word-pf.bin sse.bin sse-pf.bin sse-nb.bin test.bin

test.bin: test.c
	gcc $(FLAGS) $^ -o $@
	objdump -D $@ > $@.S

byte.bin: merge.c
	gcc $(FLAGS) -DBYTE_MERGE $^ -o $@

byte-pf.bin: merge.c
	gcc $(FLAGS) -DBYTE_MERGE -DPREFETCH $^ -o $@

word.bin: merge.c
	gcc $(FLAGS) -DWORD_MERGE $^ -o $@

word-pf.bin: merge.c
	gcc $(FLAGS) -DWORD_MERGE -DPREFETCH $^ -o $@

sse.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE $^ -o $@
	objdump -D $@ > $@.S

sse-pf.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE -DPREFETCH $^ -o $@
	objdump -D $@ > $@.S

sse-nb.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE_NOBRANCH $^ -o $@
	objdump -D $@ > $@.S

run:
	./byte.bin
	./byte-pf.bin
	./word.bin
	./word-pf.bin
	./sse.bin
	./sse-pf.bin
	./sse-nb.bin

clean:
	rm -f *.bin

over: clean all
