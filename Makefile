
FLAGS=-Wall -O3 -std=c99 -march=core2 -msse4.2 -DPERCENT_DIFF_PAGES=100 -DMAX_DIFF_WORDS_PER_PAGE=10 -DPREFETCH_PAGES=0 -DPREFETCH_BYTES_PER_PAGE=0

all: byte.bin byte-pf.bin word.bin word-pf.bin sse.bin sse-pf.bin sse-nb.bin sse-unroll.bin

test.bin: test.c
	gcc $(FLAGS) $^ -o $@
	objdump -D $@ > $@.S

byte.bin: merge.c
	gcc $(FLAGS) -DBYTE_MERGE $^ -o $@

byte-pf.bin: merge.c
	gcc $(FLAGS) -DBYTE_MERGE -DPREFETCH $^ -o $@

word.bin: merge.c
	gcc $(FLAGS) -DWORD_MERGE $^ -o $@
	objdump -D $@ > $@.S

word-pf.bin: merge.c
	gcc $(FLAGS) -DWORD_MERGE -DPREFETCH $^ -o $@

sse.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE $^ -o $@
	objdump -D $@ > $@.S

sse-pf.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE -DPREFETCH $^ -o $@

sse-nb.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE_NOBRANCH $^ -o $@

sse-unroll.bin: merge.c
	gcc $(FLAGS) -DSSE_MERGE_UNROLL $^ -o $@

run:
	./byte.bin
	./byte-pf.bin
	./word.bin
	./word-pf.bin
	./sse.bin
	./sse-pf.bin
	./sse-nb.bin

clean:
	rm -f *.bin *.S

over: clean all
