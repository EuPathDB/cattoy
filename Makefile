CC=gcc
CFLAGS=-shared -fPIC -Isqlite3 -lz

all:
	$(CC) $(CFLAGS)  -o access_log.so  access_log.c 

clean:
	rm -f *.so