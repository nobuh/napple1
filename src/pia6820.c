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
#include "pia6820.h"
#include "screen.h"

static unsigned char _dspCr = 0, _dsp = 0, _kbdCr = 0, _kbd = 0;
static int kbdInterrups = 0, dspOutput = 0;

void resetPia6820(void)
{
	kbdInterrups = dspOutput = 0;
	_kbdCr = _dspCr = 0;
}

void setKdbInterrups(int b)
{
	kbdInterrups = b;
}

int getKbdInterrups(void)
{
	return kbdInterrups;
}

int getDspOutput(void)
{
	return dspOutput;
}

void writeDspCr(unsigned char dspCr)
{
	if (!dspOutput && dspCr >= 0x80)
		dspOutput = 1;
	else
		_dspCr = dspCr;
}

void writeDsp(unsigned char dsp)
{
	if (dsp >= 0x80)
		dsp -= 0x80;

	outputDsp(dsp);
	_dsp = dsp;
}

void writeKbdCr(unsigned char kbdCr)
{
	if (!kbdInterrups && kbdCr >= 0x80)
		kbdInterrups = 1;
	else
		_kbdCr = kbdCr;
}

void writeKbd(unsigned char kbd)
{
	_kbd = kbd;
}

unsigned char readDspCr(void)
{
	return _dspCr;
}

unsigned char readDsp(void)
{
	return _dsp;
}

unsigned char readKbdCr(void)
{
	if (kbdInterrups && _kbdCr >= 0x80)
	{
		_kbdCr = 0;

		return 0xA7;
	}

	return _kbdCr;
}

unsigned char readKbd(void)
{
	return _kbd;
}
