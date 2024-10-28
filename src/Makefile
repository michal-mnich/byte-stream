CC = gcc
CFLAGS = -O2 -std=gnu17
CFLAGSDEBUG = -DDEBUG -g -Og -Wall -Wextra -Wpedantic -Wshadow -std=gnu17 -fsanitize=address
DEBUG = 1

ifeq ($(DEBUG), 1)
    CFLAGS := $(CFLAGSDEBUG)
endif

.PHONY: all clean

all: ppcbc ppcbs

ppcbc: ppcbc.o common.o err.o protocol.o
	$(CC) $(CFLAGS) -o $@ $^

ppcbs: ppcbs.o common.o err.o protocol.o
	$(CC) $(CFLAGS) -o $@ $^

# Generated with gcc -MM *.c
common.o: common.c common.h err.h protconst.h
err.o: err.c err.h
ppcbc.o: ppcbc.c common.h err.h protocol.h
ppcbs.o: ppcbs.c common.h err.h protocol.h
protocol.o: protocol.c common.h err.h protocol.h

clean:
	rm -f ppcbc ppcbs *.o
