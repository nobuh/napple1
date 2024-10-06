/* napple1 ncurses Apple 1 emulator
 * Copyright (C) 2008 Nobu Hatano
 *
 * Pom1 Apple 1 Emulator
 * Copyright (C) 2000 Verhille Arnaud
 * Copyright (C) 2006 John D. Corrado
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
/*
 * Memory maap of napple1
 * 
 * Apple I 8K mode & napple1 64K mode, common usage
 * -----------------------------------------------------------------------
 * $0000           Ststem & User Space 
 * $0200 - $027F   Input Buffer used by monitor
 * $0FFF
 * -----------------------------------------------------------------------
 * $D010	KBD	Keyboard input register. b7 is always 1 by hardware.
 * 		Read KBD will automatcically clear KBDCR's b7.
 * $D011	KBDCR	When key is pressed, b7 is set by hardware.
 * $D012	DSP	Bit b6..b0 is output character for the terminal.
 *	        Writing to DSP will set b7 by hardware.
 *              The termianl clear b7 after the character is accepted.
 * $D013	DSPCR	Unused.
 * -----------------------------------------------------------------------
 * $E000        Apple I Integer BASIC
 * $E2B3        Re-entry address
 * $EFFF
 * -----------------------------------------------------------------------
 * $FF00        Monitor
 * $FFEF	Echo
 * $FFFF	
 * ----------------------------------------------------------------------- 
 */
/* Apple I 8K mode memory map
 * --------------------------------- 
 * Start Type
 * addr
 * --------------------------------- 
 * $0000 4KB RAM
 * $1000 unused
 * $D010 Display and Keyboard I/O
 * $D014 unused
 * $E000 4KB RAM
 * $F000 unused
 * $FF00 256B ROM^ (Woz Monitor)
 * ---------------------------------
 * ^ ROM can be written by Load core  
 */
/* napple I 32K mode memory map
 * --------------------------------- 
 * Start Type
 * addr
 * --------------------------------- 
 * $0000 32K RAM
 * $8000 unused
 * $D010 Display and Keyboard I/O
 * $D014 unused
 * $E000 8K ROM^ 
 * ---------------------------------
 * ^ ROM can be written by Load core  
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pia6820.h"
#include "memory.h"
#include "screen.h"
#include "msgbuf.h"
#include "keyboard.h"

#define MEMMAX 0xFFFF
#define FNAME_LEN_MAX 1024

static unsigned char mem[65536];
static int mode = 8; /* 8 = Apple I 8K mode, 32 = napple1 32K mode */

char rombasic[FNAME_LEN_MAX];
char rommonitor[FNAME_LEN_MAX];

void flipMode(void)
{
	if (mode == 8)
		mode = 32;
	else 
		mode = 8;

	/* update message buffer */
	print_msgbuf("");
}

int memMode(void) 
{
	return mode;
}

void loadBasic(void)
{
	FILE *fd = fopen(rombasic, "rb");
	char input[MSG_LEN_MAX +1];
	
	if (!fd) {
		gets_msgbuf("Failed to open basic.rom", input);
		return;
	}

	gets_msgbuf("Load basic.rom to ram? y/n: ", input);
	if (input[0] == 'y') {
		size_t s = fread(&mem[0xE000], 1, 4096, fd);
		if (s) {
			gets_msgbuf("Load completed: ", input);
		} else {
			gets_msgbuf("Load failed: ", input);
		}
	}

	fclose(fd);
	return;
}

int loadMonitor(void)
{
	FILE *fd = fopen(rommonitor, "rb");

	if (fd) {
		fread(&mem[0xFF00], 1, 256, fd);
		fclose(fd);
	}
	else{
		return 0;
	}

	return 1;
}

void resetMemory(void)
{
	if (memMode() > 8)
		memset(mem, 0, 0xE000); /* rom is starting from 0xE000 */
	else
		memset(mem, 0, 0x10000 - 256); /* rom is within tail 256b */
}

unsigned char memRead(unsigned short address)
{
	if (address == 0xD013)
		return readDspCr();
	if (address == 0xD012)
		return readDsp();
	if (address == 0xD011)
	{
		unsigned char v = readKbdCr();
		if (!(v & 0x80))
			nextAutotyping();
		return v;
	}
	if (address == 0xD010)
		return readKbd();

	return mem[address];
}

void memWrite(unsigned short address, unsigned char value)
{
	if (address < 0x1000)	
		mem[address] = value;
	else if (address < 0x8000 && (memMode() > 8) )
		mem[address] = value;
	else if (address == 0xD013)
		writeDspCr(value);
	else if (address == 0xD012)
		writeDsp(value);
	else if (address == 0xD011)
		writeKbdCr(value);
	else if (address == 0xD010)
		writeKbd(value);
	else if (address >= 0xE000 && address < 0xF000 && memMode() == 8)
		mem[address] = value;
	else
		;

	return;
}

void dumpCore(void)
{
	int i;
	FILE *fd;
	char input[MSG_LEN_MAX +1];
	char corename[5 + MSG_LEN_MAX +1]; /* 'core/' + input string */

	gets_msgbuf("Dump core. Filename: ", input);
	sprintf(corename, "core/%s", input);

	fd = fopen(corename, "w");
	for (i = 0; i <= MEMMAX; i++)
		fputc(mem[i], fd);
	fclose(fd);
	gets_msgbuf("Dump core completed: ", input);
}

int loadCore(void)
{
	FILE *fd;
	char input[MSG_LEN_MAX +1];
	char corename[5 + MSG_LEN_MAX +1]; /* 'core/' + input string */
	size_t s = 0;
	unsigned char buf[65536];
	int i;

	gets_msgbuf("Load core. Filename: ", input);
	sprintf(corename, "core/%s", input);

	fd = fopen(corename, "r");
	if (fd) {
		s = fread(&buf[0], 1, MEMMAX+1, fd);
		fclose(fd);
	}
	if (s) { 
		gets_msgbuf("Load core completed: ", input);
	} else {
		gets_msgbuf("Failed to open core file: ", input);
		return 0;
	}

	/* 0xF000 is unused area of 8K mode or
	 * ROM area of 32K mode. So,  if 0xF000 has a value,
	 * The mode should better change to 32K mode.
	 */
	if ((buf[0xF000] != 0) && (memMode() == 8)) {
		flipMode();
	}

	if (memMode() == 8) {
		for (i = 0;      i <= 0x0FFF; i++) mem[i] = buf[i];
		for (i = 0xE000; i <= 0xEFFF; i++) mem[i] = buf[i];
		for (i = 0xFF00; i <= 0xFFFF; i++) mem[i] = buf[i];
	} else {
		for (i = 0;      i <= 0x7FFF; i++) mem[i] = buf[i];
		for (i = 0xE000; i <= 0xFFFF; i++) mem[i] = buf[i];
	}
	return 1;
}

/* set ROM file name using ROMDIR env variable
 * default path is ./rom 
 * need to be called before loadBasic() and loadMonitor()
 */
void setRomFiles(void)
{
    char env[FNAME_LEN_MAX];
    char *p;

    strcpy(rombasic, "rom/basic.rom");
    strcpy(rommonitor, "rom/monitor.rom");

    p = env;
    if (getenv("ROMDIR")) {
        p = getenv("ROMDIR");
        sprintf(rombasic, "%s/basic.rom", p);
        sprintf(rommonitor, "%s/monitor.rom", p);
    } 
}


