all: nschrooter pidsearch nssu

nschrooter: nschrooter.c
	gcc -Os -Wall -static -o nschrooter nschrooter.c `pkg-config --static --libs libseccomp`

pidsearch: pidsearch.c
	gcc -Os -Wall -o pidsearch pidsearch.c

nssu: nssu.c
	gcc -Os -Wall -o nssu nssu.c

clean:
	rm -f nschrooter pidsearch nssu
