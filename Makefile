
PREFIX   = /dev/shm/spawn

CC       = gcc
CPPFLAGS = -DSPAWN_INSTALL_PREFIX=$(PREFIX)
CFLAGS   = -O0 -ggdb

default: spawn
all    : default install

main.o: main.c
	$(CC) -o $@ -c $<

spawn: main.o
	$(CC) -o $@ $<

install:
	rm -rf $(PREFIX)
	#
	install -d -m755 $(PREFIX)
	install -d -m755 $(PREFIX)/bin
	install -d -m755 $(PREFIX)/lib
	install -d -m755 $(PREFIX)/libexec
	#
	install -m 755 spawn $(PREFIX)/libexec/spawn
	ln -s $(PREFIX)/libexec/spawn $(PREFIX)/bin

clean:
	rm -f spawn
	rm -f *.o
	rm -rf $(PREFIX)

