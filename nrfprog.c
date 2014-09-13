#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include "bp.h"
#include "nrf24le1.h"
#include "device.h"


#define pack_addr(s, b) {\
	b[2] = s & 0xFF;\
	b[1] = (s >> 8) & 0xFF;\
}

/* TODO: Proper SPI adaptor registration factory */
static struct spi_device *spi; 

/* Load a hex file */
int hf_read(char *fn, uint8_t **data, uint32_t *sz) {
	FILE *f;
	int line = 0, i, done = 0;
	uint8_t byte_count, rec_type, checksum, c_checksum;
	uint16_t address = 0, address_high = 0;
	char buf[532];
	uint8_t dat[256];

	// Open the file
	f = fopen(fn, "r");
	if (!f) { 
		perror("fopen");
		return 1;
	}
	// Allocate 16kbytes, the max size
	*sz = NRF24_FLASH_SZ;
	
	// Allocate the data and make sure it's cleared
	*data = (uint8_t *)malloc(sizeof(uint8_t) * (*sz));
	memset(*data, 0xFF, sizeof(uint8_t) * (*sz));

	// Read in the data from the file and store in the array
	while(!done) {
		// Read in the line
		if(fgets(buf, 532, f) == 0)
			break;

		// Make sure this record starts with :, otherwise, skip it
		if(buf[0] != ':')
			continue;

		// Get the byte count, address and record type
		sscanf(buf, ":%02hhX%04hX%02hhX", &byte_count, &address, &rec_type);
		
		// Get the checksum
		sscanf(buf + 9 + (byte_count * 2), "%02hhX", &checksum);

		// Get the data on this line
		for(i=0; i<byte_count; i++)
			sscanf(buf + 9 + i*2, "%02hhX", &(dat[i]));

		// Compute the checksum
		c_checksum = 0;
		for(i=0; i<byte_count; i++)
			c_checksum += dat[i];
		c_checksum += byte_count + rec_type + (address & 0xFF) + ((address >> 8) & 0xFF);
		c_checksum = (0x100 - c_checksum) & 0xFF;
		if(c_checksum != checksum)
			printf("Warning checksum error on line: %i (%02X != %02X)\n", line, c_checksum, checksum); 

		// Check based on the record type
		switch(rec_type) {
		// Data record
		case 0:
			// Copy the data into the buffer, at the right place
			memcpy(&(*data)[(address_high << 16) + address], dat, byte_count);
			break;
		// EOF Record
		case 1:
			// At EOF, we're done
			if(byte_count == 0)
				done = 1;
			break;
		// Extended Segment Address Record
		case 2:
			printf("FIXME: ESA\n");
			break;
		// Start Segment Address Record
		case 3:
			printf("FIXME: SSA\n");
			break;
		// Extended Linear Address Record
		case 4:
			// This is the new high order bits
			address_high = (dat[0] << 8) & dat[1];
			break;
		// Start Linear Address Record
		case 5:
			printf("FIXME: SLA\n");
			break;
		}
		line++;
	}
	// Complete
	fclose(f);
	return 0;
}


void flash_dump(char* filename, int len)
{
	FILE *fd = fopen(filename, "wb+"); 
	int toread = len;
	char cmd[] = { NRF24_SPI_READ, 0x0, 0x0 }; 
	char tmp[4096];
	int chunksize = (len < 4096) ? len : 4096;
 
	printf("Dumping flash to %s", filename);
	fflush(stdout);
	spi->cs(0);
	spi->write(cmd, 3);
	while (toread) { 
		spi->read(tmp, chunksize); 
		fwrite(tmp, chunksize, 1, fd);
		toread -= chunksize;
		printf(".");
		fflush(stdout);
	}
	spi->cs(1);
	printf("done!\n");
}


int flash_verify_buffer(char* data, int len, int offset)
{
	int addr = 0;
	int toread = len;
	char cmd[] = { NRF24_SPI_READ, 0x0, 0x0 }; 
	char tmp[4096];
	int i; 
	fflush(stdout);
	spi->cs(0);
	spi->write(cmd, 3);
	printf("Verifying...");
	while (len) {
		toread = (len < 4096) ? len : 4096;
		spi->read(tmp, toread); 
		for (i = 0; i< toread; i++) 
			if (data[i] != tmp[i]) { 
				printf("Mismatch at address 0x%x\n", addr + i);
				return 1;
//				exit(1);
			}
		addr += toread;
		data += toread;
		len -= toread;
		printf(".");
		fflush(stdout);
	}
	spi->cs(1);
	printf("done!\n");
}

#define BANK_INFO 0
#define BANK_DATA 1
void flash_select_bank(int bank) 
{
	char byte = (bank == BANK_INFO) ? 0x8 : 0x0; 
	char cmd[] = { NRF24_SPI_WRSR,  byte }; 
	spi->cs(0);
	spi->write(cmd, 2);
	spi->cs(1);	
}

int flash_write_enable()
{
	char cmd[] = { NRF24_SPI_WREN };
	spi->cs(0);
	spi->write(cmd, 1); 
	spi->cs(1);	
}


int flash_status(uint8_t *status)
{
	char cmd[] = { NRF24_SPI_RDSR };
	spi->cs(0);
	spi->write(cmd, 1);
	spi->read(cmd, 1); 
	spi->cs(1); 
	*status = cmd[0];
	return 0;
}

int flash_wait()
{
	uint8_t status;
	do  { 
		flash_status(&status);
	} while (status);
	return 0; 	
}

int flash_erase_all()
{
	printf("Erasing everything...");
	fflush(stdout);

	flash_write_enable();

	char cmd[] = { NRF24_SPI_ERASE_ALL };
	spi->cs(0);
	spi->write(cmd, 1); 
	spi->cs(1);

	flash_wait();
}

int flash_erase(int addr)
{
	uint8_t npage = (addr >> NRF24_PAGESHIFT);
	char cmd[] = { NRF24_SPI_ERASE_PAGE, npage };
	flash_write_enable();

	spi->cs(0);
	spi->write(cmd, 2); 
	spi->cs(1);
	flash_wait();
}

int flash_write_buffer(char* data, int len, int addr)
{
	char cmd[] = { NRF24_SPI_PROGRAM, 0x0, 0x0 }; 
	int towrite; 

	int chunksize = 256; 

	printf("Erasing blocks ");
	fflush(stdout);
	int i; 
	
	for (i=addr; i < addr + len; i = i + NRF24_BLOCK_SZ)
		flash_erase(i), printf(".", i >> NRF24_PAGESHIFT), fflush(stdout);
	
	printf("done!\n");
	printf("Writing data ");
	while (len) {
		flash_write_enable();

		towrite = (len > chunksize) ? chunksize : len; 
		pack_addr(addr, cmd);

		spi->cs(0); 
		spi->write(cmd, 3); 
		spi->write(data, towrite);
		spi->cs(1);
 
		data+=towrite;
		len-=towrite;
		addr+=towrite;

		fflush(stdout);
		printf(".");

		flash_wait();

	}
	printf("Done!\n");
}


static int current_file_type; 
static struct option long_options[] =
{
	/* Take care of the file types. */
	{"ihx",   no_argument,       &current_file_type, 1},
	{"bin",   no_argument,       &current_file_type, 0},
	/* These options don’t set a flag.
	   We distinguish them by their indices. */
	{"winfo",     required_argument,       0, 'w'},
	{"rinfo",     required_argument,       0, 'r'},
	{"wflash",    required_argument,       0, 'W'},
	{"rflash",    required_argument,       0, 'R'},
	{"offset",    required_argument,       0, 'o'},
	{"length",    required_argument,       0, 'l'},
	{"help",      no_argument,             0, 'h'},
	{"eraseall",  no_argument,             0, 'E'},
	{"erase",     required_argument,       0, 'e'},	
	{0, 0, 0, 0}
};


void usage(char* s) {
	fprintf(stderr, 
		"nrfDude: An SPI-programming app for nrf24lu1 chips\n"
		"Loosely based on nrfprog by Joseph Jezak\n"
		"(c) Andrew 'Necromant' Andrianov 2014\n"
		"Usage: %s operation filename [operation arg] ...\n"
		"Operations are executed from left to right\n"
		"Valid ops are: \n"
		"   --ihx  - set file format to ihx\n"
		"   --bin  - set file format to binary(default)\n"
		"   --rinfo  filename - read infopage to filename\n"
		"   --winfo  filename - write infopage from filename\n"
		"   --rflash filename - read flash to filename\n"
		"   --wflash filename - write flash from filename\n"
		"   --length len - Read/Write only fist len bytes of file\n"
		"   --eraseall - erase the whole flash\n"
		"   --erase len - Erase len bytes from --offset (default - 0), round up to pages\n"
		"   --help  - Show this useless help message\n\n"
		"If you didn't get the above, examples: \n"
		"   %s --bin --rinfo info.backup.bin   - Read out infopage\n"
		"   %s --ihx --rflash flash.backup.ihx - Read out flash\n"
		"   %s --bin --wflash flash.bin        - Write flash\n"
		"More complex stuff: \n"
		"   %s --bin --rflash flash.backup.bin --ihx --wflash flash.bin - Backup old firmware, flash new\n\n"
		"   %s --bin --len 8192 --offset 8192 --rflash flash.backup.bin - Backup 8K of firmware starting at 8K\n\n"

		"Well, you got the idea!\n"
		"This is 146%% free software, licensed under GNU GPLv3\n",
		s,s,s,s,s,s
		);
}


int check_spi_adaptor() 
{
	if (NULL==spi) { 
		spi=&uisp_device;
		if (0 != spi->init(NULL, NULL))
			exit(1);
	}
}

int getfile(char *filename, char* dstbuf, int len) 
{
	if (current_file_type) { 
		uint8_t *data; 
		uint32_t sz; 
		if (0 != hf_read(filename, &data, &sz))
			return 1;
		memcpy(dstbuf, data, sz);
		free(data); 
		return 0;
	}
	/* Binary fmt */
	
	FILE *fd = fopen(filename, "rb");
	if (!fd) { 
		perror("fopen"); 
		return 1; 
	}
	int ret = fread(dstbuf, 1, len, fd);
	printf("Loaded %d bytes of data from file %s\n", ret, filename);
	return 0;
}

int main(int argc, char **argv) {
	char c;
	int option_index;
	int offset = 0; 
	int length = 16*1024;
	char*  tmp = malloc(NRF24_FLASH_SZ) ; /* Buffer for all out data */ 
	memset(tmp, 0x0, NRF24_FLASH_SZ);

	while (1) { 
		c = getopt_long (argc, argv, "w:W:r:R:o:l:hEe:",
				 long_options, &option_index);
		/* Detect the end of the options. */
		if (c == -1)
			break;
		
		switch (c) {
		case 'h':
			usage(argv[0]);
			exit(1);
			break;
		case 'o':
			offset = atoi(optarg); 
			break;
		case 'l':
			length = atoi(optarg); 
			break;
		case 'e': { 
			int i; 
			int len = atoi(optarg); 
			check_spi_adaptor();
			printf("Erasing flash from 0x%x to 0x%x", 
			       (offset & ~(NRF24_BLOCK_SZ-1)), 
			       ((offset + len) & ~(NRF24_BLOCK_SZ-1)));
			fflush(stdout);
			for (i = offset; i < offset + len; i+= NRF24_BLOCK_SZ)
				flash_erase(i), printf("."), fflush(stdout);
			printf("done!\n");
		}
		case 'E':
			check_spi_adaptor();
			flash_erase_all();			
			break;
		case 'w':
			check_spi_adaptor();
			flash_select_bank(BANK_INFO);
			if (0 != getfile(optarg, tmp, NRF24_INFO_SZ))
				exit(1);
			flash_write_buffer(tmp, NRF24_INFO_SZ, 0);
			break;

		case 'r':
			check_spi_adaptor();
			flash_select_bank(BANK_INFO);
			flash_dump(optarg, NRF24_INFO_SZ);
			break;

		case 'R':
			check_spi_adaptor();
			flash_select_bank(BANK_DATA);
			flash_dump(optarg, length);
			break;

		case 'W': { 
			check_spi_adaptor();
			flash_select_bank(BANK_DATA);
			if (0 != getfile(optarg, tmp, length))
				exit(1);
			flash_write_buffer(tmp, length, offset);
			flash_verify_buffer(tmp, length, offset);
			break;
			}
		}
	}	
}
