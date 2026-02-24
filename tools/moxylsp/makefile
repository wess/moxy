MOXY ?= ../../build/debug/moxy
CC = cc
CFLAGS = -Wall -Wextra -O2

all: moxylsp

moxylsp: moxylsp.mxy lsp.mxy json.h json.c
	$(MOXY) moxylsp.mxy > gen.c
	$(CC) $(CFLAGS) -o moxylsp gen.c json.c
	rm -f gen.c

clean:
	rm -f moxylsp gen.c

.PHONY: all clean
