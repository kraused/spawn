
PREFIX   = /dev/shm/spawn

CC       = gcc
CPPFLAGS = -D'SPAWN_INSTALL_PREFIX="$(PREFIX)"' -I$(PWD)
CFLAGS   = -O0 -ggdb -Wall -std=gnu11 -fPIC
# -Wl,--export-dynamic (or equivalently -rdynamic) is needed so that
# plugins can resolve symbols from the executable.
LDFLAGS  = -Wl,--export-dynamic -ldl -lpthread

OBJ      = main.o plugin.o spawn.o pack.o protocol.o error.o helper.o queue.o comm.o thread.o network.o alloc.o watchdog.o
SO       = plugins/local.so plugins/ssh.so

default: spawn.exe $(SO)
all    : default install

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%.so: %.o
	$(CC) $(LDFLAGS) -shared -o $@ $<

spawn.exe: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

install:
	rm -rf $(PREFIX)
	#
	install -d -m755 $(PREFIX)
	install -d -m755 $(PREFIX)/bin
	install -d -m755 $(PREFIX)/lib
	install -d -m755 $(PREFIX)/libexec
	#
	install -m 755 spawn.exe $(PREFIX)/libexec/spawn
	ln -s $(PREFIX)/libexec/spawn $(PREFIX)/bin
	#
	install -m 755 plugins/{ssh,local}.so $(PREFIX)/lib

clean:
	rm -f spawn.exe
	rm -f *.o
	rm -f plugins/*.o
	rm -f plugins/*.so
	rm -rf $(PREFIX)

