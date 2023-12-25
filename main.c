/**
USB to CAN interface, using STM32F103, SLCAN compat.

@file		main.c
@author		Matej Kogovsek
@copyright	GPL v2
*/

#include <stm32f10x.h>
#include <stm32f10x_can.h>

#include <string.h>

#include "mat/serialq.h"
#include "mat/circbuf8.h"
#include "mat/can.h"

//-----------------------------------------------------------------------------
//  Global variables
//-----------------------------------------------------------------------------

#define CMD_SER 1

volatile uint32_t msTicks;	// counts 1ms timeTicks

static uint8_t uart1rxbuf[64];
static uint8_t uart1txbuf[128];

#define CANRXBUFSIZE 32
static CanRxMsg canrxmbuf[CANRXBUFSIZE];
static uint8_t canrxibuf[CANRXBUFSIZE];
static struct cbuf8_t canrxidx;

const uint16_t CAN_SPEED[][3] = {
{225,11,4}, // CAN_BR_10
{90,14,5}, // CAN_BR_20
{45,11,4}, // CAN_BR_50
{18,14,5}, // CAN_BR_100
{18,11,4}, // CAN_BR_125
{9,11,4}, // CAN_BR_250
{4,13,4}, // CAN_BR_500
{3,10,4},	// 800k not supported by CAN peripheral
{2,13,4} // CAN_BR_1000
};

void can_init_(uint8_t cs, uint8_t md)
{
	can_init(CAN_SPEED[cs][0], CAN_SPEED[cs][1], CAN_SPEED[cs][2], md);
}

static uint32_t canrxovf;
static uint8_t canopnd;
static uint8_t canspd;
static uint8_t cants;

//-----------------------------------------------------------------------------
// newlib required functions
//-----------------------------------------------------------------------------

void _exit(int status)
{
	while(1) {}
}

//-----------------------------------------------------------------------------
//  SysTick_Handler
//-----------------------------------------------------------------------------

void SysTick_Handler(void)
{
	msTicks++;			// increment counter necessary in Delay()
}

//-----------------------------------------------------------------------------
//  delays number of tick Systicks (happens every 1 ms)
//-----------------------------------------------------------------------------

void _delay_ms (uint32_t dlyTicks)
{
	uint32_t curTicks = msTicks;
	while ((msTicks - curTicks) < dlyTicks);
}

//-----------------------------------------------------------------------------
// fast hex send
//-----------------------------------------------------------------------------

char hexmap[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

void ser_puth(uint8_t n, uint8_t d)
{
	ser_putc(n, hexmap[d >> 4]);
	ser_putc(n, hexmap[d & 0xf]);
}

//-----------------------------------------------------------------------------
//  sends CanRxMsg over serial in slcan format
//-----------------------------------------------------------------------------

void ser_putcanrx(uint8_t n, CanRxMsg* msg)
{
/*
	uint8_t idl = 3;
	char c = 't';
	if( msg->RTR == CAN_RTR_Remote ) c = 'r';
	if( msg->IDE == CAN_Id_Extended ) {
		idl = 8;
		c -= 'a' - 'A';
		msg->StdId = msg->ExtId;
	}
	ser_putc(n, c);
	ser_puti_lc(n, msg->StdId, 16, idl, '0');
*/
	if( msg->IDE == CAN_Id_Standard ) {
		if( msg->RTR == CAN_RTR_Data ) {
			ser_putc(n, 't');
		} else {
			ser_putc(n, 'r');
		}
		ser_puti_lc(n, msg->StdId, 16, 3, '0');
	} else {
		if( msg->RTR == CAN_RTR_Data ) {
			ser_putc(n, 'T');
		} else {
			ser_putc(n, 'R');
		}
		ser_puti_lc(n, msg->ExtId, 16, 8, '0');
	}

	ser_putc(n, '0' + msg->DLC);
	uint8_t i;
	for( i = 0; i < msg->DLC; ++i ) {
		ser_puth(n, msg->Data[i]);
	}
	ser_puts(n, "\r");
}

//-----------------------------------------------------------------------------
// naive hex char to int
//-----------------------------------------------------------------------------

uint8_t hctoi(char c)
{
	if( c >= 'a' ) return c - 'a' + 10;
	if( c >= 'A' ) return c - 'A' + 10;
	return c - '0';
}

//-----------------------------------------------------------------------------
// process command from client
//-----------------------------------------------------------------------------

void proc_cmd(char* s)
{
	if( s[0] == 'v' ) {	// sw ver
		ser_puts(CMD_SER, "v0100\r");
	} else
	if( s[0] == 'V' ) {	// hw ver
		ser_puts(CMD_SER, "V0100\r");
	} else
	if( s[0] == 'C' ) {	// close
		can_shutdown();
		canopnd = 0;
		ser_putc(CMD_SER, 7);
	} else
	if( s[0] == 'S' ) {	// baudrate
		if( s[1] - '0' < sizeof(CAN_SPEED) ) {
			canspd = s[1] - '0';
			ser_putc(CMD_SER, 13);
		}
	} else
	if( s[0] == 'Z' ) {	// timestamping on/off
		cants = (s[1] == '1');
		ser_putc(CMD_SER, 13);
	} else
	if( s[0] == 'L' ) {	// open ro (listen)
		can_init_(canspd, CAN_Mode_Silent);
		canopnd = 1;
		ser_putc(CMD_SER, 13);
	} else
	if( s[0] == 'O' ) {	// open rw
		can_init_(canspd, CAN_Mode_Normal);
		canopnd = 1;
		ser_putc(CMD_SER, 13);
	} else
	if( s[0] == 'I' ) {	// open loopback
		can_init_(canspd, CAN_Mode_LoopBack);
		canopnd = 1;
		ser_putc(CMD_SER, 13);
	} else
	if( (s[0] == 't') || (s[0] == 'T') || (s[0] == 'r') || (s[0] == 'R') ) { // tx
		CanTxMsg m;
		uint8_t i;

		m.IDE = CAN_Id_Standard;	// default = std frame
		uint8_t idl = 3;	// std id length = 3 nibbles (11 bits)
		if( s[0] < 'a' ) {	// if uppercase
			m.IDE = CAN_Id_Extended;	// change to ext frame
			idl = 8;	// ext id length = 8 nibbles (29 bits)
		}

		m.DLC = hctoi(s[1+idl]);
		if( m.DLC > 8 ) return;
		if( strlen(s) != 1+idl+1+2*m.DLC) return;

		m.ExtId = 0;
		for( i = 0; i < idl; ++i ) m.ExtId = m.ExtId * 16 + hctoi(s[1+i]);
		m.StdId = m.ExtId;	// dunno why there are separate fields for std and ext ID

		for( i = 0; i < m.DLC; ++i ) m.Data[i] = hctoi(s[1+idl+1+2*i]) * 16 + hctoi(s[1+idl+1+2*i+1]);

		m.RTR = CAN_RTR_Data;
		if( (s[0] == 'r') || (s[0] == 'R' ) ) m.RTR = CAN_RTR_Remote;

		can_tx(&m);
		if( m.IDE == CAN_Id_Standard ) ser_putc(CMD_SER, 'z'); else ser_putc(CMD_SER, 'Z');
	}
}

//-----------------------------------------------------------------------------
//  MAIN function
//-----------------------------------------------------------------------------

int main(void)
{
	if( SysTick_Config(SystemCoreClock / 1000) ) { // setup SysTick Timer for 1 msec interrupts
		while( 1 );                                  // capture error
	}

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0); // disable preemption

	ser_init(CMD_SER, 115200, uart1txbuf, sizeof(uart1txbuf), uart1rxbuf, sizeof(uart1rxbuf));

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

	cbuf8_clear(&canrxidx, canrxibuf, CANRXBUFSIZE);

	canrxovf = 0;
	canopnd = 0;
	canspd = 0;
	cants = 0;

	char cmdbuf[32];
	uint8_t cmdlen = 0;

	while( 1 ) {
		uint8_t c;
		if( ser_getc(CMD_SER, &c) ) {
			// buffer overflow guard
			if( cmdlen >= sizeof(cmdbuf) ) { cmdlen = 0; }

			// execute on enter
			if( (c == '\r') || (c == '\n') ) {
				if( cmdlen ) {
					cmdbuf[cmdlen] = 0;
					cmdlen = 0;
					proc_cmd(cmdbuf);
				}
			} else {	// store character
				cmdbuf[cmdlen++] = c;
			}
		}

		uint8_t i;
		if( cbuf8_get(&canrxidx, &i) ) {
			ser_putcanrx(CMD_SER, &canrxmbuf[i]);
		}
	}
}

//-----------------------------------------------------------------------------
//  INTERRUPTS
//-----------------------------------------------------------------------------

void can_rx_callback(CanRxMsg* msg)
{
	static uint8_t i = 0;

	if( !cbuf8_put(&canrxidx, i) ) {
		canrxovf++;
		return;
	}

	memcpy(&canrxmbuf[i], msg, sizeof(CanRxMsg));

	i = (i + 1) % CANRXBUFSIZE;
}
