all: nschrooter pidsearch

nschrooter: nschrooter.c
	gcc -Os -Wall -static -o nschrooter nschrooter.c

pidsearch: pidsearch.c
	gcc -Os -Wall -o pidsearch pidsearch.c

clean:
	rm -f nschrooter pidsearch
