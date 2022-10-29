all: csv

csv: csv.c arena.h vector.h
	gcc -O3 -lcsv -o csv csv.c

install: csv
	rm ${HOME}/bin/csv
	cp csv ${HOME}/bin

clean:
	rm csv
