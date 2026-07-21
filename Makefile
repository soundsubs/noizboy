CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

.PHONY: test test-dbcell test-new-stages test-karplus-sustain test-sweep clean

test: test_dsp test-dbcell test-new-stages test-karplus-sustain test-sweep
	./test_dsp

test-dbcell: test_dbcell
	./test_dbcell

test-new-stages: test_new_stages
	./test_new_stages

test-karplus-sustain: test_karplus_sustain
	./test_karplus_sustain

test-sweep: test_comprehensive_sweep
	./test_comprehensive_sweep

test_dsp: src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c -o test_dsp $(LDFLAGS)

test_dbcell: src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c -o test_dbcell $(LDFLAGS)

test_new_stages: src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c -o test_new_stages $(LDFLAGS)

test_karplus_sustain: src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_sustain.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_sustain.c -o test_karplus_sustain $(LDFLAGS)

test_comprehensive_sweep: src/distroy_dsp.c src/noiseboy_dsp.c test/test_comprehensive_sweep.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_comprehensive_sweep.c -o test_comprehensive_sweep $(LDFLAGS)

clean:
	rm -f test_dsp test_dbcell test_new_stages test_karplus_sustain test_comprehensive_sweep *.o dsp.so
