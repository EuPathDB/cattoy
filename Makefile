CC=gcc
CFLAGS=-shared -fPIC -Isqlite3 -lz

all:
	$(CC) $(CFLAGS)  -o httpdlog.so  httpdlog.c 

clean:
	rm -f *.so