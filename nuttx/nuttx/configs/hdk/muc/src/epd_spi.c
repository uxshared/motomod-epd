// Copyright 2013-2015 Pervasive Displays, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied.  See the License for the specific language
// governing permissions and limitations under the License.


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
//#include <err.h>
#include <sys/ioctl.h>
//#include <linux/spi/spidev.h>

#include <arch/board/epd_spi.h>


#define EPD_COG_VERSION 2



// prototypes
static void set_spi_mode(FAR struct spi_dev_s *spi, uint8_t mode);



// enable SPI access SPI fd
FAR struct spi_dev_s *spi_create(void) {
    return up_spiinitialize(1);
}


// release SPI fd (if open)
bool SPI_destroy(FAR struct spi_dev_s *spi) {
    
    
/*
	if (NULL == spi) {
		return false;
	}
	close(spi->fd);
	free(spi);
*/
	return true;
}


// enable SPI, ensures a zero byte was sent (MOSI=0)
// using SPI MODE 2 and that CS and clock remain high
void SPI_on(FAR struct spi_dev_s *spi) {
	//const uint8_t buffer[1] = {0};

    SPI_LOCK(spi, true);
	//SPI_SETMODE(spi,SPIDEV_MODE0);
	

	set_spi_mode(spi, SPIDEV_MODE0);

    SPI_SELECT(spi, SPIDEV_NONE, true);

	SPI_SEND(spi, 0 & 0xff);

	//SPI_SELECT(spi, SPIDEV_NONE, false);

	//SPI_send(spi, buffer, sizeof(buffer));
}


// disable SPI, ensures a zero byte was sent (MOSI=0)
// using SPI MODE 0 and that CS and clock remain low
void SPI_off(FAR struct spi_dev_s *spi) {
	//const uint8_t buffer[1] = {0};

	set_spi_mode(spi, SPIDEV_MODE0);
	//set_spi_mode(spi, SPI_MODE_0);
	//SPI_SELECT(spi, SPIDEV_NONE, true);

	SPI_SEND(spi, 0 & 0xff);
	
	//SPI_send(spi, buffer, sizeof(buffer));

    SPI_SELECT(spi, SPIDEV_NONE, false);
    //SPI_LOCK(spi, false);
}

// send a data block to SPI
// will only change CS if the SPI_CS bits are set
void SPI_send(FAR struct spi_dev_s *spi, const void *buffer, size_t length) {
	/*struct spi_ioc_transfer transfer_buffer[1] = {
		{
			.tx_buf = (unsigned long)(buffer),
			.rx_buf = 0,  // nothing to receive
			.len = length,
			.delay_usecs = 2,
			.speed_hz = spi->bps,
			.bits_per_word = 8,
			.cs_change = 0
		}
	};

	if (-1 == ioctl(spi->fd, SPI_IOC_MESSAGE(1), transfer_buffer)) {
		warn("SPI: send failure");
	}*/
	//SPI_SELECT(spi, SPIDEV_NONE, true);

	SPI_SNDBLOCK(spi, buffer, length);
	
	//SPI_SELECT(spi, SPIDEV_NONE, false);
}

// send a data block to SPI and return last bytes returned by slave
// will only change CS if the SPI_CS bits are set
void SPI_read(FAR struct spi_dev_s *spi, const void *buffer, void *received, size_t length) {
    SPI_EXCHANGE(spi, buffer, received, length);

/*
	struct spi_ioc_transfer transfer_buffer[1] = {
		{
			.tx_buf = (unsigned long)(buffer),
			.rx_buf = (unsigned long)(received),
			.len = length,
			.delay_usecs = 2,
			.speed_hz = spi->bps,
			.bits_per_word = 8,
			.cs_change = 0
		}
	};

	if (-1 == ioctl(spi->fd, SPI_IOC_MESSAGE(1), transfer_buffer)) {
		warn("SPI: read failure");
	}

	*/
}


// internal functions
// ==================

static void set_spi_mode(FAR struct spi_dev_s *spi, uint8_t in_mode) {

	//uint8_t mode = in_mode;
	//uint8_t bits = 8;
	//uint8_t lsb_first = 0;
	//uint32_t speed_hz = spi->bps;

        //uint32_t freq= 0;

        

	SPI_SETMODE(spi,in_mode);
	SPI_SETBITS(spi,8);
	SPI_SETFREQUENCY(spi,EPD_SPI_BPS);

        //dbg("mode: %d freq: %d\n", in_mode, freq);

	// WR
	/*if (-1 == ioctl(spi->fd, SPI_IOC_WR_MODE, &mode)) {
		err(1,"SPI: cannot set SPI_IOC_WR_MODE  =%d", mode);
	}

	if (-1 == ioctl(spi->fd, SPI_IOC_WR_BITS_PER_WORD, &bits)) {
		err(1,"SPI: cannot set SPI_IOC_WR_BITS_PER_WORD = %d", bits);
	}

	if (-1 == ioctl(spi->fd, SPI_IOC_WR_LSB_FIRST, &lsb_first)) {
		err(1,"SPI: cannot set SPI_IOC_WR_LSB_FIRST = %d", lsb_first);
	}

	if (-1 == ioctl(spi->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz)) {
		err(1,"SPI: cannot set SPI_IOC_WR_MAX_SPEED_HZ = %d", speed_hz);
	} */
}
