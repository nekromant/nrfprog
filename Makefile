all:	nrfdude

.SUFFIXES:

CFLAGS= -ggdb -Wall -lusb

CFILES = nrfprog.c device-uisp-flashrom.c

nrfdude: $(CFILES) bp.h nrf24le1.h
	$(CC) $(CFLAGS) -fstack-protector -o nrfdude $(CFILES)

install:
	cp nrfdude /usr/local/bin

clean:
	rm -f nrfprog
