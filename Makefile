
CC	= gcc
CFLAGS	= -O0 -ggdb


default: spawn sqbwn

main.o: main.c
	$(CC) -o $@ -c $<

spawn: main.o
	$(CC) -o $@ $<

sqbwn: main.o
	$(CC) -o $@ $<

