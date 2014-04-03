/*****************************************************************************
 * Copyright (c) 2014 Ted John
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * This file is part of OpenRCT2.
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#ifndef _WIDGET_H_
#define _WIDGET_H_

typedef enum {
	WWT_EMPTY = 0,
	WWT_FRAME = 1,
	WWT_RESIZE = 2,
	WWT_IMGBTN = 3,
	WWT_4 = 4,
	WWT_TRNBTN = 7,
	WWT_TAB = 8,
	WWT_FLATBTN = 9,
	WWT_DROPDOWN_BUTTON = 10,
	WWT_12 = 12,
	WWT_DROPDOWN = 16,
	WWT_VIEWPORT = 17,
	WWT_CAPTION = 20,
	WWT_CLOSEBOX = 21,
	WWT_SCROLL = 22,
	WWT_25 = 25,
	WWT_LAST = 26,
} WINDOW_WIDGET_TYPES;
#define WIDGETS_END		WWT_LAST, 0, 0, 0, 0, 0, 0, 0

#endif