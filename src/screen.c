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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, 
 * USA.
 */
/** screen.c
 * Screen is a vitrual display of Apple I 
 */
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <sys/time.h>

#include "screen.h"

static unsigned char screenTbl[40 * 24];
static int indexX, indexY;
static WINDOW *screen;
static long long interval_start; /* interval start time in u sec */

char getch_screen(void)
{
	return (char)wgetch(screen);
}

void init_screen(void)
{
	/* Determine main screen window size.
	 * To reserve bottom 1 line for the message buffer, 
	 * -1 from $LINES 
	 */
	if ((nrow = (LINES - 1)) > 24 || nrow < 1)
		nrow = 24;
	if ((ncol = COLS) > 40 || ncol < 1)
		ncol = 40;
	
	/* Create 'screen' window */
	screen = newwin(nrow, ncol, 0, 0); 

	/* Set screen window color as Green On Black */
	if (has_colors())
	{
		start_color();
		init_pair(1, COLOR_GREEN, COLOR_BLACK);
		wattron(screen, COLOR_PAIR(1));
	}
}	

void updateScreen(void)
{
	int i, j;
	unsigned char c;

	werase(screen);

	for (j = 0; j < nrow; ++j)
	{
		for (i = 0; i < ncol; ++i)
		{
		  	wmove(screen, j, i);
			c = screenTbl[j * ncol + i];
			if (c < '!') 
				c = ' ';
			wprintw(screen, "%c", c);
		}
	}

	wmove(screen, indexY, indexX); /* put cursor */
	wrefresh(screen);
}

void resetScreen(void)
{
	indexX = indexY = 0;
	memset(screenTbl, 0, nrow * ncol);
	updateScreen();
}

static void synchronizeOutput(void)
{
	int processed; /* processed real time in u sec */
	int delay; /* delay u sec to be added to real time */
	struct timeval t;
      
	gettimeofday(&t, NULL);
	processed = (int)(t.tv_usec + t.tv_sec * 1000000 
			  - interval_start);
	if (processed < 0)
		processed = 0;

	/* Video output refreshes screen by 60 Hz. 
	 * In real time, it takes 1 sec / 60 hz. 
	 * So, 1000000 usec / 60 hz. 
	 */  
	delay = 1000000 / 60 - processed;
	if (delay < 0)
		delay = 0;
	usleep((unsigned int)delay); 

	gettimeofday(&t, NULL);
	interval_start = (long long)(t.tv_usec + t.tv_sec * 1000000);
}

static void newLine(void)
{
	int i;

	for (i = 0; i < (nrow-1) ; ++i)
		memcpy(&screenTbl[i * ncol], 
		       &screenTbl[(i + 1) * ncol], 
		       ncol);

	memset(&screenTbl[ncol * (nrow-1)], 0, ncol);
}

void outputDsp(unsigned char dsp)
{
	switch (dsp)
	{
	case 0x5F:
		if (indexX == 0)
		{
			indexY--;
			indexX = ncol-1;
		}
		else
			indexX--;

		screenTbl[indexY * ncol + indexX] = 0;
		break;
	case 0x0A:
	case 0x0D:
		indexX = 0;
		indexY++;
		break;
	case 0x00:
	case 0x7F:
		break;
	default:
		screenTbl[indexY * ncol + indexX] = dsp;
		indexX++;
		break;
	}

	if (indexX == ncol)
	{
		indexX = 0;
		indexY++;
	}
	if (indexY == nrow)
	{
		newLine();
		indexY--;
	}

	updateScreen();

	synchronizeOutput();
}

void select_screen(void)
{
	touchwin(screen);
	wrefresh(screen);
}
