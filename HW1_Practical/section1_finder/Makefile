CC=gcc
CFLAGS=-Wall -O2
all: finder
finder: finder.c
	$(CC) $(CFLAGS) -pthread -o finder finder.c
clean:
	rm -f finder
