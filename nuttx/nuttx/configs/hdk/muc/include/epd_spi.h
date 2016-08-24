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


#if !defined(EPD_SPI_H)
#define EPD_SPI_H 1

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <nuttx/spi/spi.h>

#define EPD_SPI_BPS     800000

// type to hold SPI data
//typedef struct SPI_struct SPI_type;


// functions
// =========

// enable SPI access SPI fd
FAR struct spi_dev_s *spi_create();

// release SPI fd
bool SPI_destroy(FAR struct spi_dev_s *spi);

// enable SPI, ensures a zero byte was sent (MOSI=0)
// using SPI MODE 2 and that CS and clock remain high
void SPI_on(FAR struct spi_dev_s *spi);

// disable SPI, ensures a zero byte was sent (MOSI=0)
// using SPI MODE 0 and that CS and clock remain low
void SPI_off(FAR struct spi_dev_s *spi);

// send a data block to SPI
// will only change CS if the SPI_CS bits are set
uint8_t SPI_send(FAR struct spi_dev_s *spi, const void *buffer, size_t length);


uint8_t SPI_send_image(FAR struct spi_dev_s *spi, const void *buffer, size_t length);


// send a data block to SPI and return last bytes returned by slave
// will only change CS if the SPI_CS bits are set
void SPI_read(FAR struct spi_dev_s *spi, const void *buffer, void *received, size_t length);

#endif
