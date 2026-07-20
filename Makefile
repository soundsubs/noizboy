CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

.PHONY: test test-dbcell clean

test: test_dsp test-dbcell
	./test_dsp

test-dbcell: test_dbcell
	./test_dbcell

test_dsp: src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c -o test_dsp $(LDFLAGS)

test_dbcell: src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c -o test_dbcell $(LDFLAGS)

clean:
	rm -f test_dsp test_dbcell *.o dsp.so
