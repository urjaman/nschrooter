CC = gcc
CFLAGS ?= -Os -Wall

all: nschrooter pidsearch nssu


nschrooter: nschrooter.c
	$(CC) $(CFLAGS) -o nschrooter nschrooter.c `pkg-config --libs libseccomp`

nschrooter.static: nschrooter.c
	$(CC) $(CFLAGS) -static -o nschrooter.static nschrooter.c `pkg-config --static --libs libseccomp`
	strip nschrooter.static

pidsearch: pidsearch.c
	$(CC) $(CFLAGS) -o pidsearch pidsearch.c

nssu: nssu.c
	$(CC) $(CFLAGS) -o nssu nssu.c

clean:
	rm -f nschrooter nschrooter.static pidsearch nssu
