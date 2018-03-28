/*
 * carbon-private.c
 * 
 * Copyright (C) 2008 Novell, Inc (http://www.novell.com)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
 * and associated documentation files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial 
 * portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT 
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Authors:
 *   Geoff Norton  <gnorton@novell.com>
 */

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include "gdipenums.h"

void gdip_get_display_dpi_carbon (float *h_dpi, float *v_dpi) {
	*h_dpi = *v_dpi = 96.0f;

	if (getenv ("MONO_MWF_MAC_DETECT_DPI") != NULL) {
		CGSize size = CGDisplayScreenSize(kCGDirectMainDisplay);

		if (!CGSizeEqualToSize(size, CGSizeZero)) {
			const float mmpi = 25.4;
			float h_size_inch = size.width / mmpi;
			float v_size_inch = size.height / mmpi;
			*h_dpi = CGDisplayPixelsWide (kCGDirectMainDisplay) / h_size_inch;
			*v_dpi = CGDisplayPixelsHigh (kCGDirectMainDisplay) / v_size_inch;
		}
	}
}

struct tt_hhea {
	uint32_t table_version_number;
	int16_t ascender;
	int16_t descender;
	int16_t line_gap;
	uint16_t advance_width_max;
	int16_t min_left_side_bearing;
	int16_t min_right_side_bearing;
	int16_t x_max_extent;
	int16_t caret_slope_rise;
	int16_t caret_slope_run;
	int16_t caret_offset;
	int16_t reserved1;
	int16_t reserved2;
	int16_t reserved3;
	int16_t reserved4;
	int16_t metric_data_format;
	uint16_t number_of_hmetrics;
};

struct tt_os2 {
	uint16_t version;
	uint16_t __unused1[30];
	uint16_t fs_selection;
	uint16_t fs_first_char_index;
	uint16_t fs_last_char_index;
	int16_t typo_ascender;
	int16_t typo_descender;
	int16_t typo_line_gap;
	uint16_t us_win_ascent;
	uint16_t us_win_descent;
};

#define swap_short(s) (int16_t)(((uint16_t)s >> 8) | (s << 8) & 0xff00)
#define swap_ushort(s) (uint16_t)((s >> 8) | (s << 8))

void gdip_get_fontfamily_details_from_ctfont (CTFontRef ctfont, int16_t *ascent, int16_t *descent, int16_t *linespacing, int16_t *units_per_em) {
	const UInt8 *os2_data;	
	CFDataRef os2_table = CTFontCopyTable (ctfont, kCTFontTableOS2, kCTFontTableOptionNoOptions);
	CFDataRef hhea_table = CTFontCopyTable (ctfont, kCTFontTableHhea, kCTFontTableOptionNoOptions);
	const struct tt_os2 *os2 = NULL;
	const struct tt_hhea *hhea = NULL;
	static const int fsSelectionUseTypoMetrics = (1 << 7);

	if (os2_table != NULL)
		os2 = (const struct tt_os2 *)CFDataGetBytePtr (os2_table);
	if (hhea_table != NULL)
		hhea = (const struct tt_hhea *)CFDataGetBytePtr (hhea_table);

	if ((os2 && (os2->fs_selection & fsSelectionUseTypoMetrics)) || hhea) {
		if (os2 && (os2->fs_selection & fsSelectionUseTypoMetrics)) {
			/* Use the typographic Ascender, Descender, and LineGap values for everything. */
			*linespacing = swap_short(os2->typo_ascender) - swap_short(os2->typo_descender) + swap_short(os2->typo_line_gap);
			*descent = -swap_short(os2->typo_descender);
			*ascent = swap_short(os2->typo_ascender);
		} else {
			int16_t hhea_ascender = swap_short(hhea->ascender);
			int16_t hhea_descender = swap_short(hhea->descender);
			int16_t hhea_line_gap = swap_short(hhea->line_gap);

			/* Calculate the LineSpacing for both the hhea table and the OS/2 table. */
			int hhea_linespacing = hhea_ascender + abs (hhea_descender) + hhea_line_gap;
			int os2_linespacing = os2 ? swap_ushort(os2->us_win_ascent) + swap_ushort(os2->us_win_descent) : 0;
			/* The LineSpacing is the maximum of the two sumations. */
			*linespacing = hhea_linespacing > os2_linespacing ? hhea_linespacing : os2_linespacing;
			
			/* If the OS/2 table exists, use usWinDescent as the
			 * CellDescent. Otherwise use hhea's Descender value. */			
			*descent = os2 ? swap_ushort(os2->us_win_descent) : hhea_descender;
			
			/* If the OS/2 table exists, use usWinAscent as the
			 * CellAscent. Otherwise use hhea's Ascender value. */
			*ascent = os2 ? swap_ushort(os2->us_win_ascent) : hhea_ascender;
		}		
	} else {
		*ascent = CTFontGetAscent (ctfont);
		*descent = CTFontGetDescent (ctfont);
		*linespacing = *ascent + *descent + CTFontGetLeading (ctfont);
	}

	*units_per_em = CTFontGetUnitsPerEm (ctfont);

	if (os2_table != NULL)
		CFRelease (os2_table);
	if (hhea_table != NULL)
		CFRelease (hhea_table);
}

GpStatus gdip_carbon_register_font (unsigned char *path)
{
	CFURLRef url = CFURLCreateFromFileSystemRepresentation (NULL, path, strlen ((char*)path), false);
	CFErrorRef error;
	bool success = CTFontManagerRegisterFontsForURL (url, kCTFontManagerScopeProcess, &error);
	CFRelease (url);  
	if (!success) {
		if (CFErrorGetCode (error) == kCTFontManagerErrorAlreadyRegistered)
			success = TRUE;
		CFRelease (error);
	}
	return success ? Ok : NotTrueTypeFont;
}

#else
#include <glib.h>
#include "gdipenums.h"

void gdip_get_display_dpi_carbon (float *h_dpi, float *v_dpi) {
	g_assert_not_reached ();
}

void gdip_get_fontfamily_details_from_ctfont (CTFontRef ctfont, int16_t *ascent, int16_t *descent, int16_t *linespacing, int16_t *units_per_em) {
	g_assert_not_reached ();
}

GpStatus gdip_carbon_register_font (unsigned char *path) {
	g_assert_not_reached ();
	return NotImplemented;
}
#endif
