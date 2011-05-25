CC=gcc -D_DEBUG -g3 -gstabs+ -Wall 
proxy: proxy.o
clean:
	rm -f *.o proxy core
