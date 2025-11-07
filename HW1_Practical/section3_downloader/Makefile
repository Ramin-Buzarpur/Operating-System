CC=gcc
CFLAGS=-Wall -O2
all: downloader
downloader: downloader.c
	$(CC) $(CFLAGS) -pthread -o downloader downloader.c
clean:
	rm -f downloader part_*.bin *.bin
