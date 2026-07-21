CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

.PHONY: test clean

test: test_dsp
	./test_dsp

test_dsp: src/noiseboy_dsp.c test/test_dsp.c
	$(CC) $(CFLAGS) -Isrc src/noiseboy_dsp.c test/test_dsp.c -o test_dsp $(LDFLAGS)

clean:
	rm -f test_dsp *.o dsp.so
