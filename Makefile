
PREFIX   = /dev/shm/spawn

CC       = gcc
CPPFLAGS = -D'SPAWN_INSTALL_PREFIX="$(PREFIX)"' -I$(PWD)
CFLAGS   = -O0 -ggdb -Wall -std=gnu11 -fPIC #-Wconversion
# -Wl,--export-dynamic (or equivalently -rdynamic) is needed so that
# plugins can resolve symbols from the executable.
LDFLAGS  = -Wl,--export-dynamic -ldl -lpthread -lrt

OBJ      = main.o loop.o plugin.o spawn.o job.o pack.o protocol.o error.o helper.o queue.o comm.o thread.o network.o alloc.o watchdog.o worker.o task.o options.o list.o hostinfo.o msgbuf.o
SO       = plugins/local.so plugins/ssh.so plugins/slurm.so plugins/hello.so plugins/exec.so plugins/pmiexec.so

default: spawn.exe $(SO)
all    : default install

%.o: %.c %.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%.so: %.o
	$(CC) $(LDFLAGS) -shared -o $@ $<

spawn.exe: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

install:
	rm -rf $(PREFIX)
	#
	install -d -m 755 $(PREFIX)
	install -d -m 755 $(PREFIX)/bin
	install -d -m 755 $(PREFIX)/lib
	install -d -m 755 $(PREFIX)/libexec
	install -d -m 755 $(PREFIX)/etc
	#
	install -m 755 spawn.exe $(PREFIX)/libexec/spawn
	ln -s $(PREFIX)/libexec/spawn $(PREFIX)/bin
	#
	install -m 755 plugins/{ssh,slurm,local,hello,exec,pmiexec}.so $(PREFIX)/lib
	#
	sed -e 's+SPAWN_INSTALL_PREFIX+$(PREFIX)+g' config.default > $(PREFIX)/etc/config.default 
	chmod 444 $(PREFIX)/etc/config.default

clean:
	rm -f spawn.exe
	rm -f *.o
	rm -f plugins/*.o
	rm -f plugins/*.so
	rm -rf $(PREFIX)

