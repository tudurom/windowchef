/* See the LICENSE file for copyright and license details. */

#ifndef _TYPES_H
#define _TYPES_H

#include <xcb/randr.h>

enum position {
	BOTTOM_LEFT,
	BOTTOM_RIGHT,
	TOP_LEFT,
	TOP_RIGHT,
	CENTER,
	LEFT,
	BOTTOM,
	TOP,
	RIGHT,
	ALL,
};

enum mouse_mode {
	MOUSE_NONE,
	MOUSE_MOVE,
	MOUSE_RESIZE,
};

struct window_geom {
	int16_t x, y;
	uint16_t width, height;
	bool set_by_user;
};

struct client {
	xcb_window_t window;
	struct window_geom geom;
	struct window_geom orig_geom;
	bool maxed, hmaxed, vmaxed;
	struct list_item *item;
	struct monitor *monitor;
	uint16_t min_width, min_height;
	uint16_t max_width, max_height;
	bool mapped;
	uint32_t group;
};

struct monitor {
	xcb_randr_output_t monitor;
	char *name;
	int16_t x, y;
	uint16_t width, height;
	struct list_item *item;
};

struct conf {
	int8_t border_width, grid_gap;
	int8_t gap_left, gap_down, gap_up, gap_right;
	uint32_t focus_color, unfocus_color;
	enum position cursor_position;
	uint32_t groups;
	bool sloppy_focus;
};

#endif
