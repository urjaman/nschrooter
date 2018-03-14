CC = gcc
CFLAGS ?= -Os -Wall

all: nschrooter pidsearch nssu unsfilter

nschrooter: nschrooter.c
	gcc -Os -Wall -static -o nschrooter nschrooter.c
	strip nschrooter

pidsearch: pidsearch.c
	$(CC) $(CFLAGS) -o pidsearch pidsearch.c

nssu: nssu.c
	$(CC) $(CFLAGS) -o nssu nssu.c

unsfilter: unsfilter.c
	$(CC) $(CFLAGS) -o unsfilter unsfilter.c $(shell pkg-config libseccomp --libs)

clean:
	rm -f nschrooter pidsearch nssu unsfilter
