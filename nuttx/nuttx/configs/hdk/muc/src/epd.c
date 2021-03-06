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
#include <unistd.h>
//#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
//#include <err.h>
#include <time.h>
#include <signal.h>

#include <string.h>

#include <nuttx/arch.h>


//#include "spi.h"

#include <nuttx/gpio.h>

#include <arch/board/epd.h>
#include <arch/board/epd_spi.h>



// delays - more consistent naming
#define Delay_ms(ms) up_mdelay(ms)
#define Delay_us(us) up_udelay(us)
#define LOW 0
#define HIGH 1
#define digitalRead(pin) gpio_get_value(pin)
#define digitalWrite(pin, value) gpio_set_value(pin, value)

// values for border byte
#define BORDER_BYTE_BLACK 0xff
#define BORDER_BYTE_WHITE 0xaa
#define BORDER_BYTE_NULL  0x00


// inline arrays
#define ARRAY(type, ...) ((type[]){__VA_ARGS__})
#define CU8(...) (ARRAY(const uint8_t, __VA_ARGS__))

// types
typedef enum {           // Image pixel -> Display pixel
	EPD_compensate,  // B -> W, W -> B (Current Image)
	EPD_white,       // B -> N, W -> W (Current Image)
	EPD_inverse,     // B -> N, W -> B (New Image)
	EPD_normal       // B -> B, W -> W (New Image)
} EPD_stage;

typedef enum {
	EPD_BORDER_BYTE_NONE,  // no border byte requred
	EPD_BORDER_BYTE_ZERO,  // border byte == 0x00 requred
	EPD_BORDER_BYTE_SET,   // border byte needs to be set
} EPD_border_byte;

// function prototypes

static void power_off(EPD_type *epd);

static int temperature_to_factor_10x(int temperature);
static void frame_fixed(EPD_type *epd, uint8_t fixed_value, EPD_stage stage);
static void frame_data(EPD_type *epd, const uint8_t *image, const uint8_t *mask, EPD_stage stage);
static void frame_fixed_repeat(EPD_type *epd, uint8_t fixed_value, EPD_stage stage);
static void frame_data_repeat(EPD_type *epd, const uint8_t *image, const uint8_t *mask, EPD_stage stage);
static void one_line(EPD_type *epd, uint16_t line, const uint8_t *data, uint8_t fixed_value, const uint8_t *mask, EPD_stage stage);
static void nothing_frame(EPD_type *epd);
static void dummy_line(EPD_type *epd);
static void border_dummy_line(EPD_type *epd);


// panel configuration
struct EPD_struct {
	int EPD_Pin_PANEL_ON;
	int EPD_Pin_BORDER;
	int EPD_Pin_DISCHARGE;
	int EPD_Pin_RESET;
	int EPD_Pin_BUSY;

	EPD_size size;
	int base_stage_time;
	int factored_stage_time;
	int lines_per_display;
	int dots_per_line;
	int bytes_per_line;
	int bytes_per_scan;
	bool middle_scan;

	bool pre_border_byte;
	EPD_border_byte border_byte;

	EPD_error status;

	const uint8_t *channel_select;
	size_t channel_select_length;

	uint8_t *line_buffer;
	size_t line_buffer_size;

	timer_t timer;
	FAR struct spi_dev_s *spi; /* SPI port bound to this slot */

	bool COG_on;
};


EPD_type *EPD_create(EPD_size size,
		     int panel_on_pin,
		     int border_pin,
		     int discharge_pin,
		     int reset_pin,
		     int busy_pin,
		     FAR struct spi_dev_s *spi) {

	// create a polled timer
	timer_t timer;
	struct sigevent event;
	event.sigev_notify = SIGEV_NONE;

	if (-1 == timer_create(CLOCK_REALTIME, &event, &timer)) {
		dbg("falled to create timer");
		return NULL;
	}

	// allocate memory
	EPD_type *epd = malloc(sizeof(EPD_type));
	if (NULL == epd) {
		dbg("falled to allocate EPD structure");
		return NULL;
	}

	epd->spi = spi;
	epd->timer = timer;

	epd->EPD_Pin_PANEL_ON = panel_on_pin;
	epd->EPD_Pin_BORDER = border_pin;
	epd->EPD_Pin_DISCHARGE = discharge_pin;
	epd->EPD_Pin_RESET = reset_pin;
	epd->EPD_Pin_BUSY = busy_pin;

	epd->size = size;
	epd->base_stage_time = 480; // milliseconds
	epd->lines_per_display = 96;
	epd->dots_per_line = 128;
	epd->bytes_per_line = 128 / 8;
	epd->bytes_per_scan = 96 / 4;
	epd->middle_scan = true; // => data-scan-data ELSE: scan-data-scan
	epd->pre_border_byte = false;
	epd->border_byte = EPD_BORDER_BYTE_ZERO;

	// display size dependent items
	{
		static uint8_t cs[] = {0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x00};
		epd->channel_select = cs;
		epd->channel_select_length = sizeof(cs);
	}

	// set up size structure
	switch (size) {
	default:
	case EPD_1_44:  // default so no change
		break;

	case EPD_1_9: {
		epd->lines_per_display = 128;
		epd->dots_per_line = 144;
		epd->bytes_per_line = 144 / 8;
		epd->middle_scan = false;
		epd->bytes_per_scan = 128 / 4 / 2; // scan/2 - data - scan/2
		static uint8_t cs[] = {0x72, 0x00, 0x00, 0x00, 0x03, 0xfc, 0x00, 0x00, 0xff};
		epd->channel_select = cs;
		epd->channel_select_length = sizeof(cs);
		epd->pre_border_byte = false;
		epd->border_byte = EPD_BORDER_BYTE_SET;
		break;
	}

	case EPD_2_0: {
		epd->lines_per_display = 96;
		epd->dots_per_line = 200;
		epd->bytes_per_line = 200 / 8;
		epd->bytes_per_scan = 96 / 4;
		static uint8_t cs[] = {0x72, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xe0, 0x00};
		epd->channel_select = cs;
		epd->channel_select_length = sizeof(cs);
		epd->pre_border_byte = true;
		epd->middle_scan = true;
		epd->border_byte = EPD_BORDER_BYTE_NONE;
		break;
	}

	case EPD_2_6: {
		epd->base_stage_time = 630; // milliseconds
		epd->lines_per_display = 128;
		epd->dots_per_line = 232;
		epd->bytes_per_line = 232 / 8;
		epd->middle_scan = false;
		epd->bytes_per_scan = 128 / 4 / 2; // scan/2 - data - scan/2
		static uint8_t cs[] = {0x72, 0x00, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0xff};
		epd->channel_select = cs;
		epd->channel_select_length = sizeof(cs);
		epd->pre_border_byte = false;
		epd->border_byte = EPD_BORDER_BYTE_SET;
		break;
	}

	case EPD_2_7: {
		epd->base_stage_time = 630; // milliseconds
		epd->lines_per_display = 176;
		epd->dots_per_line = 264;
		epd->bytes_per_line = 264 / 8;
		epd->bytes_per_scan = 176 / 4;
		static uint8_t cs[] = {0x72, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfe, 0x00, 0x00};
		epd->channel_select = cs;
		epd->channel_select_length = sizeof(cs);
		epd->pre_border_byte = true;
		epd->border_byte = EPD_BORDER_BYTE_NONE;
		break;
	}
	}

	// an initial default temperature
	epd->factored_stage_time = epd->base_stage_time;
	EPD_set_temperature(epd, 25);

	// buffer for frame line
	if (epd->middle_scan) {
		epd->line_buffer_size = 2 * epd->bytes_per_line
			+ epd->bytes_per_scan
			+ 3; // command byte, pre_border_byte, border byte
	} else {
		epd->line_buffer_size = epd->bytes_per_line
			+ 2 * epd->bytes_per_scan
			+ 3; // command byte, pre_border_byte, border byte
	}

	epd->line_buffer = malloc(epd->line_buffer_size + 4096);
	if (NULL == epd->line_buffer) {
		free(epd);
		dbg("falled to allocate EPD line buffer");
		return NULL;
	}

	// ensure zero
	memset(epd->line_buffer, 0x00, epd->line_buffer_size);

	// ensure I/O is all set to ZERO
	power_off(epd);

	// COG state for partial update
	epd->COG_on = false;

	return epd;
}


// deallocate memory
void EPD_destroy(EPD_type *epd) {
	if (NULL == epd) {
		return;
	}
	if (NULL != epd->line_buffer) {
		free(epd->line_buffer);
	}
	free(epd);
}


// read current status
EPD_error EPD_status(EPD_type *epd) {
	return epd->status;
}


// starts an EPD sequence
void EPD_begin(EPD_type *epd) {

	// Nothing to do when COG still on
	if (epd->COG_on) {
		return;
	}

	// assume OK
	epd->status = EPD_OK;



	// power up sequence
	gpio_set_value(epd->EPD_Pin_RESET, LOW);
	gpio_set_value(epd->EPD_Pin_PANEL_ON, LOW);
	gpio_set_value(epd->EPD_Pin_DISCHARGE, LOW);
	gpio_set_value(epd->EPD_Pin_BORDER, LOW);


		

	SPI_on(epd->spi);

	uint8_t receive_buffer[2];

	Delay_ms(5);
	gpio_set_value(epd->EPD_Pin_PANEL_ON, HIGH);
	Delay_ms(10);

	gpio_set_value(epd->EPD_Pin_RESET, HIGH);
	gpio_set_value(epd->EPD_Pin_BORDER, HIGH);
	Delay_ms(5);

	gpio_set_value(epd->EPD_Pin_RESET, LOW);
	Delay_ms(5);

	gpio_set_value(epd->EPD_Pin_RESET, HIGH);
	Delay_ms(5);




	// wait for COG to become ready
	while (HIGH == gpio_get_value(epd->EPD_Pin_BUSY)) {
		Delay_us(10);
	}

	// add more delay for busy pin
	Delay_ms(100);







	// read the COG ID
	
	int cog_id = receive_buffer[1];
	// int cog_id = -1;
	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);
/*	
	cog_id = SPI_SEND(epd->spi, 0x71 & 0xff);
	cog_id = SPI_SEND(epd->spi, 0x00 & 0xff);
	cog_id = SPI_SEND(epd->spi, 0x71 & 0xff);
	cog_id = SPI_SEND(epd->spi, 0x00 & 0xff);
	*/
	cog_id = SPI_send(epd->spi, CU8(0x71, 0x00), 1);
	cog_id = SPI_send(epd->spi, CU8(0x71, 0x00), 1);
	
	dbg ("COG ID - 01 %d... %d\n", cog_id,  0x0f & cog_id);
	

	SPI_SELECT(epd->spi, SPIDEV_NONE, false);





	
	//SPI_read(epd->spi, CU8(0x71, 0x00), receive_buffer, sizeof(receive_buffer));
	//SPI_read(epd->spi, CU8(0x71, 0x00), receive_buffer, sizeof(receive_buffer));
	//cog_id = receive_buffer[1];
	
    //dbg ("COG ID!!!!!!!!!!!!!! %d...%d\n", cog_id, 0x0f & cog_id);
    /*
	if (0x02 != (0x0f & cog_id)) {
		epd->status = EPD_UNSUPPORTED_COG;
		power_off(epd);
		dbg ("EXIT!!! COG ID!!!!!!!!!!!!!! %d...%d\n", cog_id, 0x0f & cog_id);
		return;
	}*/



	
		



	// Disable OE

	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);
	
    SPI_send(epd->spi, CU8(0x70, 0x02), 1);
	
    SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);

	
	SPI_send(epd->spi, CU8(0x72, 0x40), 1);

	SPI_SELECT(epd->spi, SPIDEV_NONE, false);

	
//	SPI_SELECT(epd->spi, SPIDEV_NONE, true);
/*
	SPI_SEND(epd->spi, 0x70 & 0xff);
	SPI_SEND(epd->spi, 0x02 & 0xff);
	SPI_SEND(epd->spi, 0x72 & 0xff);
	SPI_SEND(epd->spi, 0x40 & 0xff);
	*/
//	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	

	// check breakage

	Delay_ms(20);
	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);


	SPI_send(epd->spi, CU8(0x70, 0x0f), 1);
	
	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);

	int broken_panel = receive_buffer[1];
	broken_panel = SPI_send(epd->spi, CU8(0x73, 0x00), 1);	

	dbg ("broken_panel!!!!!!!!!!!!!! %d...%d\n", broken_panel, 0x80 & broken_panel);

	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	
//	SPI_SELECT(epd->spi, SPIDEV_NONE, true);
//	int broken_panel = receive_buffer[1];
    /*SPI_SEND(epd->spi, 0x70 & 0xff);
	SPI_SEND(epd->spi, 0x0f & 0xff);	*/
    //broken_panel = SPI_SEND(epd->spi, 0x73 & 0xff);	
    //dbg ("broken_panel!!!!!!!!!!!!!! %d...%d\n", broken_panel, 0x80 & broken_panel);
//	broken_panel = SPI_send(epd->spi, CU8(0x73, 0x00), 2);		
//	dbg ("broken_panel!!!!!!!!!!!!!! %d...%d\n", broken_panel, 0x80 & broken_panel);
//	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
		




/*	
	if (0x00 == (0x80 & broken_panel)) {
		epd->status = EPD_PANEL_BROKEN;
		power_off(epd);
		return;
	}
*/

	SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	Delay_ms(1);
	SPI_SELECT(epd->spi, SPIDEV_NONE, true);

	// power saving mode
	SPI_send(epd->spi, CU8(0x70, 0x0b), 1);
	SPI_send(epd->spi, CU8(0x72, 0x02), 1);

	// channel select
	SPI_send(epd->spi, CU8(0x70, 0x01), 1);
	SPI_send(epd->spi, epd->channel_select, epd->channel_select_length);

	// high power mode osc
	SPI_send(epd->spi, CU8(0x70, 0x07), 1);
	SPI_send(epd->spi, CU8(0x72, 0xd1), 1);

	// power setting
	SPI_send(epd->spi, CU8(0x70, 0x08), 1);
	SPI_send(epd->spi, CU8(0x72, 0x02), 1);

	// Vcom level
	SPI_send(epd->spi, CU8(0x70, 0x09), 1);
	SPI_send(epd->spi, CU8(0x72, 0xc2), 1);

	// power setting
	SPI_send(epd->spi, CU8(0x70, 0x04), 1);
	SPI_send(epd->spi, CU8(0x72, 0x03), 1);

	// driver latch on
	SPI_send(epd->spi, CU8(0x70, 0x03), 1);
	SPI_send(epd->spi, CU8(0x72, 0x01), 1);

	// driver latch off
	SPI_send(epd->spi, CU8(0x70, 0x03), 1);
	SPI_send(epd->spi, CU8(0x72, 0x00), 1);

	Delay_ms(5);

	bool dc_ok = false;

    int i;
	for (i = 0; i < 4; ++i) {
		// charge pump positive voltage on - VGH/VDL on
		SPI_send(epd->spi, CU8(0x70, 0x05), 1);
		SPI_send(epd->spi, CU8(0x72, 0x01), 1);

		Delay_ms(240);

		// charge pump negative voltage on - VGL/VDL on
		SPI_send(epd->spi, CU8(0x70, 0x05), 1);
		SPI_send(epd->spi, CU8(0x72, 0x03), 1);

		Delay_ms(40);

		// charge pump Vcom on - Vcom driver on
		SPI_send(epd->spi, CU8(0x70, 0x05), 1);
		SPI_send(epd->spi, CU8(0x72, 0x0f), 1);

		Delay_ms(40);

		// check DC/DC
		SPI_send(epd->spi, CU8(0x70, 0x0f), 1);
		int dc_state = receive_buffer[1];
		dc_state = SPI_send(epd->spi,CU8(0x73, 0x00), 1);		
		dbg("DC STATE %d\n", 0x40 & dc_state);
		
		if (0x40 == (0x40 & dc_state)) {
			dc_ok = true;
			break;
		}
	}
	if (!dc_ok) {
		epd->status = EPD_DC_FAILED;
		dbg("EPD DC FAILED");
		//power_off(epd);
		//return;
	}

	// output enable to disable
	SPI_send(epd->spi, CU8(0x70, 0x02), 1);
	SPI_send(epd->spi, CU8(0x72, 0x04), 1);

	epd->COG_on = true;


	SPI_SELECT(epd->spi, SPIDEV_NONE, false);




	/*dbg("Enter loop!\n");
while(1) {
	usleep(20000);
}
dbg("Exit loop!\n");*/



}


void EPD_end(EPD_type *epd) {

	nothing_frame(epd);

	if (EPD_2_7 == epd->size) {
		dummy_line(epd);
		// only pulse border pin for 2.70" EPD
		Delay_ms(25);
		digitalWrite(epd->EPD_Pin_BORDER, LOW);
		Delay_ms(200);
		digitalWrite(epd->EPD_Pin_BORDER, HIGH);
	} else {
		border_dummy_line(epd);
		Delay_ms(200);
	}

	// ??? - not described in datasheet
	SPI_send(epd->spi, CU8(0x70, 0x0b), 1);
	SPI_send(epd->spi, CU8(0x72, 0x00), 1);

	// latch reset turn on
	SPI_send(epd->spi, CU8(0x70, 0x03), 1);
	SPI_send(epd->spi, CU8(0x72, 0x01), 1);

	// power off charge pump Vcom
	SPI_send(epd->spi, CU8(0x70, 0x05), 1);
	SPI_send(epd->spi, CU8(0x72, 0x03), 1);

	// power off charge pump neg voltage
	SPI_send(epd->spi, CU8(0x70, 0x05), 1);
	SPI_send(epd->spi, CU8(0x72, 0x01), 1);

	Delay_ms(120);

	// discharge internal
	SPI_send(epd->spi, CU8(0x70, 0x04), 1);
	SPI_send(epd->spi, CU8(0x72, 0x80), 1);

	// turn off all charge pumps
	SPI_send(epd->spi, CU8(0x70, 0x05), 1);
	SPI_send(epd->spi, CU8(0x72, 0x00), 1);

	// turn of osc
	SPI_send(epd->spi, CU8(0x70, 0x07), 1);
	SPI_send(epd->spi, CU8(0x72, 0x01), 1);

	Delay_ms(50);

	power_off(epd);

	epd->COG_on = false;
}


static void power_off(EPD_type *epd) {

	// turn of power and all signals
	gpio_set_value(epd->EPD_Pin_RESET, LOW);
	gpio_set_value(epd->EPD_Pin_PANEL_ON, LOW);
	gpio_set_value(epd->EPD_Pin_BORDER, LOW);

	// ensure SPI MOSI and CLOCK are Low before CS Low
	SPI_off(epd->spi);

	gpio_set_value(epd->EPD_Pin_DISCHARGE, HIGH);
	Delay_ms(150);
	gpio_set_value(epd->EPD_Pin_DISCHARGE, LOW);
}


void EPD_set_temperature(EPD_type *epd, int temperature) {
	epd->factored_stage_time = epd->base_stage_time * temperature_to_factor_10x(temperature) / 10;
}


// clear display (anything -> white)
void EPD_clear(EPD_type *epd) {
	frame_fixed_repeat(epd, 0xff, EPD_compensate);
	frame_fixed_repeat(epd, 0xff, EPD_white);
	frame_fixed_repeat(epd, 0xaa, EPD_inverse);
	frame_fixed_repeat(epd, 0xaa, EPD_normal);
}

// assuming a clear (white) screen output an image
void EPD_image_0(EPD_type *epd, const uint8_t *image) {
	frame_fixed_repeat(epd, 0xaa, EPD_compensate);
	frame_fixed_repeat(epd, 0xaa, EPD_white);
	frame_data_repeat(epd, image, NULL, EPD_inverse);
    //frame_data_repeat(epd, image, NULL, EPD_normal);
}

// change from old image to new image
void EPD_image(EPD_type *epd, const uint8_t *old_image, const uint8_t *new_image) {
	frame_data_repeat(epd, old_image, NULL, EPD_compensate);
	frame_data_repeat(epd, old_image, NULL, EPD_white);
	frame_data_repeat(epd, new_image, NULL, EPD_inverse);
	//frame_data_repeat(epd, new_image, NULL, EPD_normal);
}

// change from old image to new image
void EPD_partial_image(EPD_type *epd, const uint8_t *old_image, const uint8_t *new_image) {
	// Only need last stage for partial update
	// See discussion on issue #19 in the repaper/gratis repository on github
	frame_data_repeat(epd, new_image, old_image, EPD_inverse);
}


// internal functions
// ==================

// convert a temperature in Celsius to
// the scale factor for frame_*_repeat methods
static int temperature_to_factor_10x(int temperature) {
	if (temperature <= -10) {
		return 170;
	} else if (temperature <= -5) {
		return 120;
	} else if (temperature <= 5) {
		return 80;
	} else if (temperature <= 10) {
		return 40;
	} else if (temperature <= 15) {
		return 30;
	} else if (temperature <= 20) {
		return 20;
	} else if (temperature <= 40) {
		return 10;
	}
	return 7;
}


// One frame of data is the number of lines * rows. For example:
// The 1.44” frame of data is 96 lines * 128 dots.
// The 2” frame of data is 96 lines * 200 dots.
// The 2.7” frame of data is 176 lines * 264 dots.

// the image is arranged by line which matches the display size
// so smallest would have 96 * 32 bytes

static void frame_fixed(EPD_type *epd, uint8_t fixed_value, EPD_stage stage) {
    uint8_t l;
	for (l = 0; l < epd->lines_per_display ; ++l) {
		one_line(epd, l, NULL, fixed_value, NULL, stage);
	}
}


static void frame_data(EPD_type *epd, const uint8_t *image, const uint8_t *mask, EPD_stage stage) {
    uint8_t l;
	if (NULL == mask) {
		for (l = 0; l < epd->lines_per_display ; ++l) {
			one_line(epd, l, &image[l * epd->bytes_per_line], 0, NULL, stage);
		}
	} else {
		for (l = 0; l < epd->lines_per_display ; ++l) {
			size_t n = l * epd->bytes_per_line;
			one_line(epd, l, &image[n], 0, &mask[n], stage);
		}
	}
}


static void frame_fixed_repeat(EPD_type *epd, uint8_t fixed_value, EPD_stage stage) {
	struct itimerspec its;
	its.it_value.tv_sec = epd->factored_stage_time / 1000;
	its.it_value.tv_nsec = (epd->factored_stage_time % 1000) * 1000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;

	if (-1 == timer_settime(epd->timer, 0, &its, NULL)) {
		dbg("timer_settime failed");
	}
	do {
		frame_fixed(epd, fixed_value, stage);

		if (-1 == timer_gettime(epd->timer, &its)) {
			dbg("timer_gettime failed");
		}
	} while (its.it_value.tv_sec > 0 || its.it_value.tv_nsec > 0);
}


static void frame_data_repeat(EPD_type *epd, const uint8_t *image, const uint8_t *mask, EPD_stage stage) {
	struct itimerspec its;
	its.it_value.tv_sec = epd->factored_stage_time / 1000;
	its.it_value.tv_nsec = (epd->factored_stage_time % 1000) * 1000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;

	int n = 0;
	if (-1 == timer_settime(epd->timer, 0, &its, NULL)) {
		dbg("timer_settime failed");
	}
	do {
		frame_data(epd, image, mask, stage);
		if (-1 == timer_gettime(epd->timer, &its)) {
			dbg("timer_gettime failed");
		}
		++n;
	} while (its.it_value.tv_sec > 0 || its.it_value.tv_nsec > 0);
}



static void nothing_frame(EPD_type *epd) {
    int line;
	for (line = 0; line < epd->lines_per_display; ++line) {
		one_line(epd, 0x7fffu, NULL, 0x00, NULL, EPD_compensate);
	}
}


static void dummy_line(EPD_type *epd) {
	one_line(epd, 0x7fffu, NULL, 0x00, NULL, EPD_compensate);
}


static void border_dummy_line(EPD_type *epd) {
	one_line(epd, 0x7fffu, NULL, 0x00, NULL, EPD_normal);
}


// pixels on display are numbered from 1 so even is actually bits 1,3,5,...
static void even_pixels(EPD_type *epd, uint8_t **pp, const uint8_t *data, uint8_t fixed_value, const uint8_t *mask, EPD_stage stage) {
    uint16_t b;
	for (b = 0; b < epd->bytes_per_line; ++b) {
		if (NULL != data) {
			uint8_t pixels = data[b] & 0xaa;
			uint8_t pixel_mask = 0xff;
			if (NULL != mask) {
				pixel_mask = (mask[b] ^ pixels) & 0xaa;
				pixel_mask |= pixel_mask >> 1;
			}
			switch(stage) {
			case EPD_compensate:  // B -> W, W -> B (Current Image)
				pixels = 0xaa | ((pixels ^ 0xaa) >> 1);
				break;
			case EPD_white:       // B -> N, W -> W (Current Image)
				pixels = 0x55 + ((pixels ^ 0xaa) >> 1);
				break;
			case EPD_inverse:     // B -> N, W -> B (New Image)
				pixels = 0x55 | (pixels ^ 0xaa);
				break;
			case EPD_normal:       // B -> B, W -> W (New Image)
				pixels = 0xaa | (pixels >> 1);
				break;
			}
			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x55);
			uint8_t p1 = (pixels >> 6) & 0x03;
			uint8_t p2 = (pixels >> 4) & 0x03;
			uint8_t p3 = (pixels >> 2) & 0x03;
			uint8_t p4 = (pixels >> 0) & 0x03;
			pixels = (p1 << 0) | (p2 << 2) | (p3 << 4) | (p4 << 6);
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
		}
	}
}

// pixels on display are numbered from 1 so odd is actually bits 0,2,4,...
static void odd_pixels(EPD_type *epd, uint8_t **pp, const uint8_t *data, uint8_t fixed_value, const uint8_t *mask, EPD_stage stage) {
    uint16_t b;
	for (b = epd->bytes_per_line; b > 0; --b) {
		if (NULL != data) {
			uint8_t pixels = data[b - 1] & 0x55;
			uint8_t pixel_mask = 0xff;
			if (NULL != mask) {
				pixel_mask = (mask[b - 1] ^ pixels) & 0x55;
				pixel_mask |= pixel_mask << 1;
			}
			switch(stage) {
			case EPD_compensate:  // B -> W, W -> B (Current Image)
				pixels = 0xaa | (pixels ^ 0x55);
				break;
			case EPD_white:       // B -> N, W -> W (Current Image)
				pixels = 0x55 + (pixels ^ 0x55);
				break;
			case EPD_inverse:     // B -> N, W -> B (New Image)
				pixels = 0x55 | ((pixels ^ 0x55) << 1);
				break;
			case EPD_normal:       // B -> B, W -> W (New Image)
				pixels = 0xaa | pixels;
				break;
			}
			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x55);
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
		}
	}
}

// interleave bits: (byte)76543210 -> (16 bit).7.6.5.4.3.2.1
static inline uint16_t interleave_bits(uint16_t value) {
	value = (value | (value << 4)) & 0x0f0f;
	value = (value | (value << 2)) & 0x3333;
	value = (value | (value << 1)) & 0x5555;
	return value;
}

// pixels on display are numbered from 1
static void all_pixels(EPD_type *epd, uint8_t **pp, const uint8_t *data, uint8_t fixed_value, const uint8_t *mask, EPD_stage stage) {
    uint16_t b;
	for (b = epd->bytes_per_line; b > 0; --b) {
		if (NULL != data) {
			uint16_t pixels = interleave_bits(data[b - 1]);

			uint16_t pixel_mask = 0xffff;
			if (NULL != mask) {
				uint16_t pixel_mask_inside = interleave_bits(mask[b - 1]);
				pixel_mask_inside = (pixel_mask_inside ^ pixels) & 0x5555;
				pixel_mask_inside |= pixel_mask_inside << 1;
			}
			switch(stage) {
			case EPD_compensate:  // B -> W, W -> B (Current Image)
				pixels = 0xaaaa | (pixels ^ 0x5555);
				break;
			case EPD_white:       // B -> N, W -> W (Current Image)
				pixels = 0x5555 + (pixels ^ 0x5555);
				break;
			case EPD_inverse:     // B -> N, W -> B (New Image)
				pixels = 0x5555 | ((pixels ^ 0x5555) << 1);
				break;
			case EPD_normal:       // B -> B, W -> W (New Image)
				pixels = 0xaaaa | pixels;
				break;
			}
			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x5555);
			*(*pp)++ = pixels >> 8;
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
			*(*pp)++ = fixed_value;
		}
	}
}

// output one line of scan and data bytes to the display
static void one_line(EPD_type *epd, uint16_t line, const uint8_t *data, uint8_t fixed_value, const uint8_t *mask, EPD_stage stage) {

	//SPI_on(epd->spi);
	//SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	//up_mdelay(1);
	//SPI_SELECT(epd->spi, SPIDEV_NONE, true);
	// send byte date
    //SPI_send_image(epd->spi, CU8(0x00, 0x00), 1);

	// send data
	SPI_send(epd->spi, CU8(0x70, 0x0a), 1);

	// CS low
	uint8_t *p = epd->line_buffer;

	*p++ = 0x72;

	if (epd->pre_border_byte) {
		*p++ = 0x00;
	}

	uint16_t b;

	if (epd->middle_scan) {
		// data bytes
		odd_pixels(epd, &p, data, fixed_value, mask, stage);

		// scan line
		for (b = epd->bytes_per_scan; b > 0; --b) {
			if (line / 4 == b - 1) {
				*p++ = 0x03 << (2 * (line & 0x03));
			} else {
				*p++ = 0x00;
			}
		}

		// data bytes
		even_pixels(epd, &p, data, fixed_value, mask, stage);

	} else {
		// even scan line, but as lines on display are numbered from 1, line: 1,3,5,...
		for (b = 0; b < epd->bytes_per_scan; ++b) {
			if (0 != (line & 0x01) && line / 8 == b) {
				*p++ = 0xc0 >> (line & 0x06);
			} else {
				*p++ = 0x00;
			}
		}

		// data bytes
		all_pixels(epd, &p, data, fixed_value, mask, stage);

		// odd scan line, but as lines on display are numbered from 1, line: 0,2,4,6,...
		for (b = epd->bytes_per_scan; b > 0; --b) {
			if (0 == (line & 0x01) && line / 8 == b - 1) {
				*p++ = 0x03 << (line & 0x06);
			} else {
				*p++ = 0x00;
			}
		}
	}

	// post data border byte
	switch (epd->border_byte) {
	case EPD_BORDER_BYTE_NONE:  // no border byte requred
		break;

	case EPD_BORDER_BYTE_ZERO:  // border byte == 0x00 requred
		*p++ = 0x00;
		break;

	case EPD_BORDER_BYTE_SET:   // border byte needs to be set
		switch(stage) {
		case EPD_compensate:
		case EPD_white:
		case EPD_inverse:
			*p++ = 0x00;
			break;
		case EPD_normal:
			*p++ = 0xaa;
			break;
		}
		break;
	}
	// send the accumulated line buffer
	SPI_send(epd->spi, epd->line_buffer, (p - epd->line_buffer)/2);

	//Delay_ms(10);

	//SPI_SELECT(epd->spi, SPIDEV_NONE, false);

	// output data to panel
	//Delay_ms(10);

	//SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	//up_mdelay(1);
	//
	//SPI_SELECT(epd->spi, SPIDEV_NONE, true);	
	
	SPI_send(epd->spi, CU8(0x70, 0x02), 1);
	SPI_send(epd->spi, CU8(0x72, 0x07), 1);

	//Delay_ms(10);

		// send byte date
	//SPI_SELECT(epd->spi, SPIDEV_NONE, false);
	//up_mdelay(1);
	//SPI_SELECT(epd->spi, SPIDEV_NONE, true);
    //SPI_send_image(epd->spi, CU8(0x00, 0x00), 1);

	//SPI_SELECT(epd->spi, SPIDEV_NONE, false);
		
	//SPI_off(epd->spi);	
	
}
