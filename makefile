LDFLAGS=-lm 
CFLAGS=-ggdb
proxy: cjson/cJSON.o proxy.o
clean:
	rm -f *.o proxy core
