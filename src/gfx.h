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

#ifndef _GFX_H_
#define _GFX_H_

typedef struct {
	char* bits;			// 0x00
	short x;			// 0x04
	short y;			// 0x06
	short width;		// 0x08
	short height;		// 0x0A
	short pitch;		// 0x0C
	char pad_0E;		// 0x0E
	char var_0F;		// 0x0F
} rct_drawpixelinfo;

void gfx_clear(rct_drawpixelinfo *dpi, int colour);
void gfx_fill_rect(rct_drawpixelinfo *dpi, int left, int top, int right, int bottom, int colour);
void gfx_draw_sprite(rct_drawpixelinfo *dpi, int image_id, int x, int y);
void gfx_draw_string(rct_drawpixelinfo *dpi, char *text, int colour, int x, int y);
void gfx_transpose_palette(int pal, unsigned char product);

void gfx_draw_string_left(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y);
void gfx_draw_string_left_clipped(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y, int width);
void gfx_draw_string_centred_clipped(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y, int width);
void gfx_draw_string_right(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y);
void gfx_draw_string_centred(rct_drawpixelinfo *dpi, int format, int x, int y, int colour, void *args);
int gfx_draw_string_centred_wrapped(rct_drawpixelinfo *dpi, void *args, int x, int y, int width, int format, int colour);
int gfx_draw_string_left_wrapped(rct_drawpixelinfo *dpi, void *format, int x, int y, int width, int colour, int unknown);

int gfx_get_string_width(char *buffer);
int clip_text(char *buffer, int width);
void gfx_fill_rect_inset(rct_drawpixelinfo *dpi, short left, short top, short right, short bottom, int colour, short _si);

void gfx_set_dirty_blocks(short left, short top, short right, short bottom);
void gfx_draw_dirty_blocks();
void gfx_redraw_screen_rect(short left, short top, short right, short bottom);
void gfx_invalidate_screen();

#endif