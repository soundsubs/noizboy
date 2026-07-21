CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

.PHONY: test test-dbcell test-new-stages clean

test: test_dsp test-dbcell test-new-stages
	./test_dsp

test-dbcell: test_dbcell
	./test_dbcell

test-new-stages: test_new_stages
	./test_new_stages

test_dsp: src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c -o test_dsp $(LDFLAGS)

test_dbcell: src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c -o test_dbcell $(LDFLAGS)

test_new_stages: src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c -o test_new_stages $(LDFLAGS)

clean:
	rm -f test_dsp test_dbcell test_new_stages *.o dsp.so
