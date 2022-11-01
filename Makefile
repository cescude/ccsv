all: csv

csv: csv.c arena.h vector.h
	gcc -O3 -lcsv -o csv csv.c

install: csv
	cp csv ${HOME}/bin

clean:
	rm csv
