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
void resetPia6820(void);
void setKdbInterrups(int b);
int getKbdInterrups(void);
int getDspOutput(void);
void writeDspCr(unsigned char dspCr);
void writeDsp(unsigned char dsp);
void writeKbdCr(unsigned char kbdCr);
void writeKbd(unsigned char kbd);
unsigned char readDspCr(void);
unsigned char readDsp(void);
unsigned char readKbdCr(void);
unsigned char readKbd(void);
