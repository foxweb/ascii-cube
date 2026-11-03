# Override: make CC=gcc CFLAGS='-Os -Wall'
CC      = clang
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = -lm

BINS = cube cube_colored

.PHONY: all clean test

all: $(BINS)

cube: cube.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

cube_colored: cube_colored.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(BINS)

test: all
	./test.sh
