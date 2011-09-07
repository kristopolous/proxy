LDFLAGS=-lm 
proxy: cjson/cJSON.o proxy.o
clean:
	rm -f *.o proxy core
