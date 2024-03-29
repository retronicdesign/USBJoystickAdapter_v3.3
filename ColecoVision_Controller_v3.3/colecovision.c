/* ColecoVision Controller to USB
 * Copyright (C) 2024 Francis-Olivier Gradel, B.Eng.
 *
 * Using V-USB code from OBJECTIVE DEVELOPMENT Software GmbH. http://www.obdev.at/products/vusb/
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The author may be contacted at info@retronicdesign.com
 */
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "usbconfig.h"
#include "colecovision.h"

#define MULT 32	// Spinner sensitivity

static char colecovisionInit(void);
static void colecovisionUpdate(void);
static char colecovisionChanged(char id);
static char colecovisionBuildReport(unsigned char *reportBuffer, char id);

static unsigned char last_update_state[2]={0,0};
static unsigned char last_reported_state[2]={0,0};

volatile int wheel_pos;
static unsigned char spinner, old_spinner;

int QEM [16] = {0,1,-1,2,-1,0,2,1,1,2,0,-1,2,-1,1,0};               // Quadrature Encoder Matrix
/* QEM explanation:
 *
 * Quadrature from a Coleco spinner is made of two 90 degree out of phase signals that corresponds to
 * two bumped wheels driven by the spinner shaft. The bumped wheels triggering micro switches that are 
 * generating those signals. 
 *
 * Here is an example of a signal of the spinner going left:
 *          ________            ________            ________            ____
 *         /        \          /        \          /        \          /
 * A  ____/          \________/          \________/          \________/
 *              ________            ________            ________
 *             /        \          /        \          /        \
 * B  ________/          \________/          \________/          \__________
 *
 * Here is an example of a signal of the spinner going right:
 *
 *          ________            ________            ________            ____
 *         /        \          /        \          /        \          /
 * A  ____/          \________/          \________/          \________/
 * 
 * B  ________            ________            ________            ________
 *            \          /        \          /        \          /
 *             \________/          \________/          \________/
 *
 * Note on these two example the difference in phase between A and B for left and right.
 *
 * Using these generated waves, we can determine by software the delta displacement of the spinner.
 * The following table is generated by combining these signals in two 2-bit value, A, B, A' and B'.
 *
 *        Actual read value (A-B 2-bit combination)
 *        0   1   2   3
 *     ----------------
 *   0 |  0   1  -1   X
 *   
 *   1 | -1   0   X   1
 *
 *   2 |  1   X   0  -1
 *  
 *   3 |  X  -1   1   0
 *
 *   Previous read value (A-B 2-bit combination)
 *
 */


static char colecovisionInit(void)
{

	/* PB0   = PIN1 = up / keypad column 1 (IN,w/Pull-up)
	 * PB1   = PIN2 = down / keypad column 2 (IN,w/Pull-up)
	 * PB2   = PIN3 = left / keypad column 3 (IN,w/Pull-up)
	 * PB3   = PIN4 = right / keypad column 4 (IN,w/Pull-up)
	 * PC1&3 = PIN5 = GND (keypad & arm button) select sub controller 2 (OUT)
	 * PB4   = PIN6 = fire button / arm button (IN, w/Pull-up)
	 * PB5   = PIN7 = roller/spinner/steering quadrature input A (IN, w/Pull-up)
	 * PD7   = PIN8 = GND (joystick & fire button) select sub controller 1 (OUT)
	 * PC0&2 = PIN9 = roller/spinner/steering quadrature input B (IN, w/Pull-up)
	 */

	 // Sub controller 1 selected by default
	
	DDRB &= ~((1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3)|(1<<PB4)|(1<<PB5));
	PORTB |= ((1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3)|(1<<PB4)|(1<<PB5));

	DDRC &= ~((1<<PC0)|(1<<PC2));
	DDRC |= ((1<<PC1)|(1<<PC3));
	PORTC |= ((1<<PC1)|(1<<PC2)|(1<<PC3)); 
	
	DDRD |= (1<<PD7);
	PORTD &= ~((1<<PD7));

	/* Spinner, initial condition */
	colecovisionUpdate();
	wheel_pos=0x80;

	return 0;
}

static void colecovisionUpdate(void)
{
	int delta;

	// Sub controller 1 selected
	PORTC |= ((1<<PC1)|(1<<PC3)); 
	PORTD &= ~((1<<PD7));

	_delay_ms(2);

	//Reading of Joystick, Left Fire and Spinner Quadrature A
	last_update_state[0] = (PINB&0x3F);

	// Sub controller 2 selected
	PORTC &= ~((1<<PC1)|(1<<PC3));
	PORTD |= ((1<<PD7));

	_delay_ms(2);

	//Reading of Key Pad, Right Fire and Spinner Quadrature B
	last_update_state[1] = ((PINB&0x1F)|((PINC&(1<<PC2))<<3));

	//Spinner calculation
	spinner = ((~last_update_state[0]&(1<<PB5))>>5) | ((~last_update_state[1]&(1<<PB5))>>4);
	
	// Apply delta displacement from quadrature generated by the spinner.
	// Quad Format (4 bits): MSB OldB OldA ActualB ActualA LSB
	delta = (QEM[(spinner|(old_spinner<<2))]*MULT);
	wheel_pos += delta;

	// Clipping min and max position
	if(wheel_pos>(int)255)
		wheel_pos=255;

	if(wheel_pos<(int)0)
		wheel_pos=0;

	old_spinner=spinner; // Old position = new position for next iteration.

}

static char colecovisionChanged(char id)
{
	return (last_update_state[0] != last_reported_state[0] || last_update_state[1] != last_reported_state[1]);
}

#define REPORT_SIZE 5

static char colecovisionBuildReport(unsigned char *reportBuffer, char id)
{
	int x,y;
	unsigned char tmp,but;
	
	if (reportBuffer)
	{
		tmp = (last_update_state[0] ^ 0xff);
		but = (last_update_state[1] ^ 0xff);
		
		y = x = 0x80;
		
		if (tmp&(1<<PB3)) { x = 0xff; }
		if (tmp&(1<<PB2)) { x = 0x00; }
		if (tmp&(1<<PB1)) { y = 0xff; }
		if (tmp&(1<<PB0)) { y = 0x00; }

	   /*
 		* [0] X
 		* [1] Y
		* [2] Z
 		* [3] Fire,Arm
		* [4] 
 		*/
		
		reportBuffer[0] = x;
		reportBuffer[1] = y;
		reportBuffer[2] = (unsigned char)wheel_pos;

		reportBuffer[3] = 0;
		reportBuffer[4] = 0;

		if (tmp&(1<<PB4)) reportBuffer[3] |= 0b00000001;	//Yellow or Left fire
		if (but&(1<<PB4)) reportBuffer[3] |= 0b00000010;	//Red or Right fire


		switch(but&0x0f)
		{
			case 0b1000: reportBuffer[3] |= 0b00000100; break; //Button 1
			case 0b0100: reportBuffer[3] |= 0b00001000; break; //Button 2
			case 0b1001: reportBuffer[3] |= 0b00010000; break; //Button 3

			case 0b0111: reportBuffer[3] |= 0b00100000; break; //Button 4
			case 0b0110: reportBuffer[3] |= 0b01000000; break; //Button 5
			case 0b0001: reportBuffer[3] |= 0b10000000; break; //Button 6

			case 0b1100: reportBuffer[4] |= 0b00000001; break; //Button 7
			case 0b1110: reportBuffer[4] |= 0b00000010; break; //Button 8
			case 0b0010: reportBuffer[4] |= 0b00000100; break; //Button 9

			case 0b1010: reportBuffer[4] |= 0b00001000; break; //Button *
			case 0b0011: reportBuffer[4] |= 0b00010000; break; //Button 0
			case 0b0101: reportBuffer[4] |= 0b00100000; break; //Button #

			case 0b1101: reportBuffer[4] |= 0b01000000; break; //Purple fire
			case 0b1011: reportBuffer[4] |= 0b10000000; break; //Blue fire
		}

	}

	last_reported_state[0] = last_update_state[0];
	last_reported_state[1] = last_update_state[1];

	return REPORT_SIZE;
}

const char colecovision_usbHidReportDescriptor[] PROGMEM = {

    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
	0x09, 0x32,                    //     USAGE (Z)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x05, 0x09,                    //	  USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 16,               	   //     USAGE_MAXIMUM (Button 16)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 16,                      //     REPORT_COUNT (16)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
	0x09, 0x00,					   //     USAGE (Undefined) // Used to trig bootloader when SET FEATURE
    0x15, 0x00,					   //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,			   //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,					   //     REPORT_SIZE (8)
    0x95, 0x01,					   //     REPORT_COUNT (1)
    0xb2, 0x02, 0x01,			   //     FEATURE (Data,Var,Abs,Buf)
	0xc0,                          //   END_COLLECTION	
    0xc0                           // END_COLLECTION
};

#define USBDESCR_DEVICE         1

// This is the same descriptor as in devdesc.c, but the product id is 0x0a99 

const char colecovision_usbDescrDevice[] PROGMEM = {    /* USB device descriptor */
    18,         /* sizeof(usbDescrDevice): length of descriptor in bytes */
    USBDESCR_DEVICE,    /* descriptor type */
    0x01, 0x01, /* USB version supported */
    USB_CFG_DEVICE_CLASS,
    USB_CFG_DEVICE_SUBCLASS,
    0,          /* protocol */
    8,          /* max packet size */
    USB_CFG_VENDOR_ID,  /* 2 bytes */
    USB_CFG_DEVICE_ID,  /* 2 bytes */
    USB_CFG_DEVICE_VERSION, /* 2 bytes */
    USB_CFG_VENDOR_NAME_LEN != 0 ? 1 : 0,         /* manufacturer string index */
    USB_CFG_DEVICE_NAME_LEN != 0 ? 2 : 0,        /* product string index */
    USB_CFG_SERIAL_NUMBER_LEN != 0 ? 3 : 0,  /* serial number string index */
    1,          /* number of configurations */
};

Gamepad colecovisionJoy = {
	.num_reports			=	1,
	.reportDescriptorSize	=	sizeof(colecovision_usbHidReportDescriptor),
	.deviceDescriptorSize	=	sizeof(colecovision_usbDescrDevice),
	.init					=	colecovisionInit,
	.update					=	colecovisionUpdate,
	.changed				=	colecovisionChanged,
	.buildReport			=	colecovisionBuildReport,
};

Gamepad *colecovisionGetGamepad(void)
{
	colecovisionJoy.reportDescriptor = (void*)colecovision_usbHidReportDescriptor;
	colecovisionJoy.deviceDescriptor = (void*)colecovision_usbDescrDevice;

	return &colecovisionJoy;
}


