CFLAGS=-O3 -Wall
LDFLAGS=-s
proxy: proxy.o
clean:
	rm -f *.o proxy core
