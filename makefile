CFLAGS=-O3
LDFLAGS=-s
proxy: proxy.o
clean:
	rm -f *.o proxy core
