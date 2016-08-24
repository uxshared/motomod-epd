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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/gpio.h>

#include <arch/board/epd.h>
#include <arch/board/epd_test.h>
#include <arch/board/epd_spi.h>
#include <arch/board/mods.h>



#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))



int epd_test(void) {
    int rc = 0;
    EPD_size display_size = EPD_2_0;
    FAR struct spi_dev_s *spi;

    // GPIO should already be setup

    // get spi devices
    spi = spi_create();
    if (spi != NULL) {
        dbg("spi init %x\n", spi);
    } else {
        return -1;
    }

    gpio_direction_out(panel_on_pin, 1);
    gpio_direction_out(border_pin, 1);
    gpio_direction_out(discharge_pin, 1);
    gpio_direction_out(reset_pin, 1);
    gpio_direction_out(busy_pin, 0);

    EPD_type *epd = EPD_create(display_size,
                        panel_on_pin,
                        border_pin,
                        discharge_pin,
                        reset_pin,
                        busy_pin,
                        spi);

    if (NULL == epd) {
        dbg("EPD_setup failed\n");
        return -1;
    }
    dbg("epd created %x\n", epd);
	

	 // EPD display
    dbg("clear display\n");
    EPD_begin(epd);

    
    
    dbg("begin stopped\n");
    EPD_clear(epd);
    dbg("clear stopped\n");
    EPD_end(epd);
    dbg("end stopped\n");
    EPD_destroy(epd);
    dbg("destroy stopped\n");
    return rc;
}

int spi_test(void) {

   FAR struct spi_dev_s *spi;

   // get spi devices
    spi = spi_create();
    if (spi != NULL) {
        dbg("spi init %s", spi);
    } else {
        return -1;
    }

    SPI_on(spi);    
	

   return 0;
    
}


