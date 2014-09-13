all:	nrfprog
CFLAGS=-ggdb -Wall -lusb

CFILES=nrfprog.c device-uisp-flashrom.c

nrfprog: $(CFILES) bp.h nrf24le1.h
	gcc $(CFLAGS) -g -O0 -fstack-protector -o nrfprog $(CFILES)

clean:
	rm -f nrfprog
