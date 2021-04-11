/*
 * This file is part of the frser-avr project.
 *
 * Copyright (C) 2009,2015 Urja Rannikko <urjaman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "main.h"
#include "flash.h"
#include "uart.h"

static uint8_t flash_databus_read(void) {
	uint8_t rv;
	rv = (PINB & 0x0F);
	rv |= ((PINC & 0x0F) << 4);
	return rv;
}

static void flash_databus_tristate(void) {
	// Lower nibble of databus - as input
	DDRB &= ~(0x0F);
	// Higher nibble of databus - as input
	DDRC &= ~(0x0F);

	// Lower nibble of databus - no pullup
	PORTB &= ~(0x0F);
	// Higher nibble of databus - no pullup
	PORTC &= ~(0x0F);
}

static void flash_databus_output(unsigned char data) {
	// Lower nibble of databus
	PORTB = ((PORTB & 0xF0) | (data & 0x0F));
	// Higher nibble of databus
	PORTC = ((PORTC & 0xF0) | ((data >> 4) & 0x0F));
	// Lower nibble of databus
	DDRB |= (0x0F);
	// Higher nibble of databus
	DDRC |= (0x0F);
}

static void flash_chip_enable(void) {
	PORTD &= ~_BV(3);
}

static void flash_chip_disable(void) {
	PORTD |= _BV(3);
}

void flash_init(void) {
	// Shift, latch and set
	PORTD &= ~ (_BV(5) | _BV(6) | _BV(7));
	DDRD |= _BV(5) | _BV(6) | _BV(7);
	// a16 is out of shift registers
	PORTB &= ~_BV(5);
	DDRB |= _BV(5);
	// ADDR unit init done
	
	// Powercontrol
	PORTB |= _BV(4);
	DDRB |= _BV(4);
	
	// ~WE, ~CE, ~OE
	PORTD |= (_BV(2) | _BV(3) | _BV(4));
	DDRD |= (_BV(2) | _BV(3) | _BV(4));
	// control bus init done
	
	// hmm, i should probably tristate the data bus by default...
	flash_databus_tristate();
	// CE control is not absolutely necessary...
	flash_chip_enable();
}

static void flash_output_enable(void) {
	PORTD &= ~_BV(4);
}

static void flash_output_disable(void) {
	PORTD |= _BV(4);
}

static void flash_setaddr(uint32_t addr) {
	uint8_t a16 = (addr >> 16) & 1;
	
	uint8_t part = addr >> 8;
	for (uint8_t i = 8; i > 0; i--) {
		uint8_t bit = part & 0x80;
		if (bit)
			PORTD |= _BV(7);
		else
			PORTD &= ~_BV(7);
		
		// Shift pulse
		_delay_us(1);
		PORTD |= _BV(5);
		_delay_us(1);
		PORTD &= ~_BV(5);
		
		part <<= 1;
	}
	
	part = addr;
	for (int i = 8; i > 0; i--) {
		uint8_t bit = part & 0x80;
		if (bit)
			PORTD |= _BV(7);
		else
			PORTD &= ~_BV(7);
		
		// Shift pulse
		_delay_us(1);
		PORTD |= _BV(5);
		_delay_us(1);
		PORTD &= ~_BV(5);
		
		part <<= 1;
	}
	
	if (a16)
		PORTB |= _BV(5);
	else
		PORTB &= ~_BV(5);
	
	// Pulse latch
	_delay_us(1);
	PORTD |= _BV(6);
	_delay_us(1);
	PORTD &= ~_BV(6);
}

static void flash_pulse_we(void) {
	PORTD &= ~(_BV(2));
	_delay_us(100);
	PORTD |= _BV(2);
}

static void flash_read_init(void) {
	flash_databus_tristate();
	flash_output_enable();
}

// assume chip enabled & output enabled & databus tristate
static uint8_t flash_readcycle(uint32_t addr) {
	_delay_us(1);
	flash_setaddr(addr);
	_delay_us(1);
	return flash_databus_read();
}

// assume only CE, and perform single cycle
uint8_t flash_read(uint32_t addr) {
	uint8_t data;
	flash_read_init();
	flash_setaddr(addr);
	data = flash_databus_read();
	flash_output_disable();
	return data;
}

uint8_t data_polling(const uint8_t val) {
	uint8_t ret = 0;
	_delay_us(1);
	flash_databus_tristate();
	for (uint16_t i = 0; i < 500 && ret == 0; i++) {
		flash_output_enable();
		_delay_us(1);
		uint8_t valid = val ^ flash_databus_read();
		if (!(valid & 0x80))
			ret = 1;
		flash_output_disable();
		_delay_us(1);
	}
	return ret;
}

// assume only CE, perform single cycle
void flash_write(uint32_t addr, uint8_t data) {
	flash_output_disable();
	flash_databus_output(data);
	flash_setaddr(addr);
	flash_pulse_we();
	data_polling(~data);
}

void flash_readn(uint32_t addr, uint32_t len) {
	flash_read_init();
	do {
		SEND(flash_readcycle(addr++));
	} while(--len);
	// safety features
	flash_output_disable();
}

void flash_select_protocol(uint8_t allowed_protocols) {
	(void)allowed_protocols;
	flash_init();

	// Turn on power supply
	PORTB &= ~_BV(4);
}

void flash_set_safe(void) {
	// Turn off power supply
	// PORTB |= _BV(4);
}
