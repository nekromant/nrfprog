#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <usb.h>
#include "device.h"

#define UISP_TIMEOUT 6000


struct flashprog_verinfo {
	char pgmname[16];   /* Programmer name            */
	uint16_t max_rq;    /* Maximum RQ number          */
	uint16_t api_ver;   /* API version                */
	uint16_t spi_freq;  /* Current SPI frequency      */
	uint16_t cpu_freq;  /* CPU frequency              */
	uint16_t num_spi;   /* Number of SPI devices here */
} __attribute__ ((packed));

#define FLASHPROG_API_VER  1


enum { 
	RQ_VERINFO,
	RQ_SPI_SET_SPEED,
	RQ_SPI_GET_SPEED,
	RQ_SPI_CS,
	RQ_SPI_IO,
	/* Insert new requests here */ 
	RQ_INVALID
};


static usb_dev_handle *hndl;
static struct flashprog_verinfo verinfo;
static uint16_t dev_index = 0 ;


#define msg_perr(fmt,...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define msg_pspew(fmt,...) fprintf(stderr, fmt, ##__VA_ARGS__)

#define UISP_VID   0x1d50
#define UISP_PID   0x6032
#define UISP_MSTR  "www.ncrmnt.org"
#define UISP_PSTR  "uISP-flashprog"

static int  usb_get_string_ascii(usb_dev_handle *dev, int index, int langid, char *buf, int buflen)
{
	char    buffer[256];
	int     rval, i;
	
	if((rval = usb_control_msg(dev, 
				   USB_ENDPOINT_IN, 
				   USB_REQ_GET_DESCRIPTOR, 
				   (USB_DT_STRING << 8) + index, 
				   langid, buffer, sizeof(buffer), 
				   1000)) < 0)
		return rval;
	if(buffer[1] != USB_DT_STRING)
		return 0;
	if((unsigned char)buffer[0] < rval)
		rval = (unsigned char)buffer[0];
	rval /= 2;
	/* lossy conversion to ISO Latin1 */
	for(i=1; i<rval; i++) {
		if(i > buflen)  /* destination buffer overflow */
			break;
		buf[i-1] = buffer[2 * i];
		if(buffer[2 * i + 1] != 0)  /* outside of ISO Latin1 range */
			buf[i-1] = '?';
	}
	buf[i-1] = 0;
	return i-1;
}


static int usb_match_string(usb_dev_handle *handle, int index, char* string)
{
	char tmp[256];
	if (string == NULL)
		return 1; /* NULL matches anything */
	usb_get_string_ascii(handle, index, 0x409, tmp, 256);
	return (strcmp(string,tmp)==0);
}

static usb_dev_handle *usb_check_device(struct usb_device *dev,
				 char *vendor_name, 
				 char *product_name, 
				 char *serial)
{
	usb_dev_handle      *handle = usb_open(dev);
	if(!handle) {
		msg_perr("uisp_spi: Error: Unable to query device info (udev rules problem?)\n");
		return NULL;
	}
	if (
		usb_match_string(handle, dev->descriptor.iManufacturer, vendor_name) &&
		usb_match_string(handle, dev->descriptor.iProduct,      product_name) &&
		usb_match_string(handle, dev->descriptor.iSerialNumber, serial)
		) {
		return handle;
	}
	usb_close(handle);
	return NULL;
	
}

static usb_dev_handle *nc_usb_open(int vendor, int product, char *vendor_name, char *product_name, char *serial)
{
	struct usb_bus      *bus;
	struct usb_device   *dev;
	usb_dev_handle      *handle = NULL;
	

	usb_find_busses();
	usb_find_devices();

	for(bus=usb_get_busses(); bus; bus=bus->next) {
		for(dev=bus->devices; dev; dev=dev->next) {
			            if(dev->descriptor.idVendor == vendor && 
				       dev->descriptor.idProduct == product) {
					    handle = usb_check_device(dev, vendor_name, product_name, serial);
					    if (handle)
						    return handle;
				    }
		}
	}
	return NULL;
}


static int do_control(int rq, int value, int index)
{
	int ret; 
	ret =  usb_control_msg(
		hndl,             // handle obtained with usb_open()
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, // bRequestType
		rq,      // bRequest
		value,              // wValue
		index,              // wIndex
		NULL,             // pointer to destination buffer
		0,  // wLength
		UISP_TIMEOUT
		);
	return ret;
}




static int uisp_spi_read(void *readarr, size_t readcnt)
{
	int ret; 
	ret = usb_control_msg(
		hndl,             // handle obtained with usb_open()
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, // bRequestType
		RQ_SPI_IO,      // bRequest
		0,              // wValue
		dev_index,              // wIndex
		readarr,             // pointer to source buffer
		readcnt,  // wLength
		UISP_TIMEOUT
		);
	if (ret != readcnt) {
		perror(__func__);
		return -1; 
	}
	return 0;
}



static int uisp_spi_write(void *writearr, size_t writecnt)
{
	int ret; 
	ret = usb_control_msg(
		hndl,             // handle obtained with usb_open()
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT, // bRequestType
		RQ_SPI_IO,      // bRequest
		0,              // wValue
		0,              // wIndex
		writearr,             // pointer to source buffer
		writecnt,  // wLength
		UISP_TIMEOUT
		);

	if (ret != writecnt) {
		perror(__func__);
		return -1; 
	}

	return 0;
}

static int uisp_spi_cs(int v)
{
	int ret = do_control(RQ_SPI_CS, v, dev_index);
	return ret; 
}


static int uisp_request_pgm_info() 
{
	int ret; 
	ret = usb_control_msg(
		hndl,             // handle obtained with usb_open()
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN, // bRequestType
		RQ_VERINFO,      // bRequest
		0,              // wValue
		dev_index,              // wIndex
		(char *) &verinfo,             // pointer to source buffer
		sizeof(verinfo),  // wLength
		UISP_TIMEOUT
		);
	
	if (ret != sizeof(verinfo)) {
		perror(__func__);
		return -1; 
	}

	return 0;
}

int uisp_init(int argc, char* argv)
{
	usb_init();
	hndl = nc_usb_open(UISP_VID, UISP_PID, UISP_MSTR, UISP_PSTR, NULL);
	if (!hndl)  {
		fprintf(stderr, "Failed to open usb dev\n");
		return 1;
	}

	uisp_request_pgm_info();

	printf("uisp_spi: Programmer name is \"%s\" \n", verinfo.pgmname);
	printf("uisp_spi: Programmer CPU speed is %u kHz\n", verinfo.cpu_freq);
	printf("uisp_spi: Programmer maximum SPI speed is %u kHz\n", verinfo.spi_freq);
	printf("uisp_spi: Programmer supports %u SPI bus(es)\n", verinfo.num_spi);
	return 0;
}


struct spi_device uisp_device =
{
	.init  = uisp_init,
	.write = uisp_spi_write,
	.read  = uisp_spi_read,
	.cs    = uisp_spi_cs,
};
