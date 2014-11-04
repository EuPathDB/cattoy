CC=gcc
CFLAGS=-shared -fPIC -Isqlite3 -lz

all: access_log error_log

access_log: 
	$(CC) $(CFLAGS)  -o access_log.so  access_log.c 

error_log:
	$(CC) $(CFLAGS)  -o error_log.so  error_log.c 

clean:
	rm -f *.so