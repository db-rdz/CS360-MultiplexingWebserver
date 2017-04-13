CC = gcc
CFLAGS = -g

all: server

server: config-parse.c config-parse.h tcp-server.c http-parser.h http-parser.c  queue.h queue.c main.c
	$(CC) -pthread $(CFLAGS) -o server config-parse.c config-parse.h tcp-server.c http-parser.h http-parser.c queue.h queue.c main.c -lm

clean:
	rm -f server
