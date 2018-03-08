CC = gcc
CFLAGS ?= -Os -Wall

all: nschrooter pidsearch nssu


nschrooter: nschrooter.c
	gcc -Os -Wall -static -o nschrooter nschrooter.c
	strip nschrooter

pidsearch: pidsearch.c
	$(CC) $(CFLAGS) -o pidsearch pidsearch.c

nssu: nssu.c
	$(CC) $(CFLAGS) -o nssu nssu.c

clean:
	rm -f nschrooter nschrooter.static pidsearch nssu
