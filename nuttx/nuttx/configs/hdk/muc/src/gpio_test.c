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
#include <unistd.h>
//#include <err.h>

#include <nuttx/gpio.h>


//#include "epd.h"
#include <arch/board/gpio_test.h>
#include <arch/board/mods.h>

// the platform specific I/O pins
//#include EPD_IO


// use redefine the EPD pins as LEDs
#define LED_1_PIN    reset_pin
#define LED_2_PIN    discharge_pin
#define LED_3_PIN    panel_on_pin
#define LED_4_PIN    border_pin

#define LED_PWM_PIN  pwm_pin

#define GPIO_write(pin, value) gpio_set_value(pin, value)
#define GPIO_pwm_write(pin, value) gpio_set_value(pin, value)



void scan_leds(void) {
	static int state = 0;
	switch (state) {
	default:
		state = 0;
	case 0:
		GPIO_write(LED_1_PIN, 1);
		GPIO_write(LED_2_PIN, 0);
		GPIO_write(LED_3_PIN, 0);
		GPIO_write(LED_4_PIN, 0);
		break;
	case 1:
		GPIO_write(LED_1_PIN, 0);
		GPIO_write(LED_2_PIN, 1);
		GPIO_write(LED_3_PIN, 0);
		GPIO_write(LED_4_PIN, 0);
		break;
	case 2:
		GPIO_write(LED_1_PIN, 0);
		GPIO_write(LED_2_PIN, 0);
		GPIO_write(LED_3_PIN, 1);
		GPIO_write(LED_4_PIN, 0);
		break;
	case 3:
		GPIO_write(LED_1_PIN, 0);
		GPIO_write(LED_2_PIN, 0);
		GPIO_write(LED_3_PIN, 0);
		GPIO_write(LED_4_PIN, 1);
		break;
	}
	++state;
}


int gpio_test() {

/*
	if (!GPIO_setup()) {
		err(1, "GPIO_setup failed");
	}
*/
	gpio_direction_out(LED_1_PIN, 1);
	gpio_direction_out(LED_2_PIN, 1);
	gpio_direction_out(LED_3_PIN, 1);
	gpio_direction_out(LED_4_PIN, 1);
	gpio_direction_out(LED_PWM_PIN, 1);

	GPIO_write(LED_1_PIN, 0);
	GPIO_write(LED_2_PIN, 0);
	GPIO_write(LED_3_PIN, 0);
	GPIO_write(LED_4_PIN, 0);

	GPIO_pwm_write(LED_PWM_PIN, 0);

	usleep(200000);

	int i;
	// simple non-pwm test
	for (i = 0; i < 50; ++i) {
		scan_leds();
		usleep(200000);
	}


	GPIO_pwm_write(LED_PWM_PIN, 0);

	GPIO_write(LED_1_PIN, 0);
	GPIO_write(LED_2_PIN, 0);
	GPIO_write(LED_3_PIN, 0);
	GPIO_write(LED_4_PIN, 0);

//	GPIO_teardown();
	return 0;
}
