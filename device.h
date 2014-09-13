#ifndef DEV_H
#define DEV_H

struct spi_device {
	int (*init)(int argc, char* argv);
	int (*write)(void* data, size_t len);
	int (*read)(void* data, size_t len);
	int (*cs)(int v);
};


extern struct spi_device uisp_device; 

#endif

