/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#ifndef WM_TYPES_H
#define WM_TYPES_H

#include <xcb/randr.h>
#include <stdbool.h>

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

enum direction {
	NORTH,
	SOUTH,
	EAST,
	WEST,
};

enum mouse_mode {
	MOUSE_NONE,
	MOUSE_MOVE,
	MOUSE_RESIZE,
};

enum pointer_action {
	POINTER_ACTION_NOTHING,
	POINTER_ACTION_FOCUS,
	POINTER_ACTION_MOVE,
	POINTER_ACTION_RESIZE_CORNER,
	POINTER_ACTION_RESIZE_SIDE,
};

enum resize_handle {
	HANDLE_LEFT,
	HANDLE_BOTTOM,
	HANDLE_TOP,
	HANDLE_RIGHT,

	HANDLE_TOP_LEFT,
	HANDLE_TOP_RIGHT,
	HANDLE_BOTTOM_LEFT,
	HANDLE_BOTTOM_RIGHT,
};

struct win_position {
	int16_t x, y;
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
	bool maxed, hmaxed, vmaxed, monocled;
	struct list_item *item;
	struct list_item *focus_item;
	struct monitor *monitor;
	uint16_t min_width, min_height;
	uint16_t max_width, max_height;
	uint16_t width_inc, height_inc;
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
	int8_t border_width, internal_border_width, grid_gap;
	int8_t gap_left, gap_down, gap_up, gap_right;
	uint32_t focus_color, unfocus_color, internal_focus_color, internal_unfocus_color;
	enum position cursor_position;
	uint32_t groups;
	bool sloppy_focus;
	bool resize_hints;
	bool sticky_windows;
	bool borders;
	bool last_window_focusing;
	bool apply_settings;
	bool replay_click_on_focus;
	enum pointer_action pointer_actions[3];
	uint16_t pointer_modifier;
	int8_t click_to_focus;
};

#endif
