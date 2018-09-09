/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#ifndef WM_CONFIG_H
#define WM_CONFIG_H

#define BORDER_WIDTH 5
#define INTERNAL_BORDER_WIDTH 0

/* colors are HTML colors that start in "0x" instead of "#" */
#define COLOR_FOCUS 0x97a293
#define COLOR_UNFOCUS 0x393638
#define INTERNAL_COLOR_FOCUS 0x393638
#define INTERNAL_COLOR_UNFOCUS 0x97a293

/* gap between the window and the edge of the monitor
 * when snapping or vertically/horizontally maximizing window */
#define GAP 0

/* gap between windows when laid in grid */
#define GRID_GAP 0

/* where to place the cursor when moving/resizing the window */
#define CURSOR_POSITION CENTER

/* number of starting groups, can be modified with waitron at run-time */
#define GROUPS 10

/* focus windows after hovering them with the pointer */
#define SLOPPY_FOCUS true

/* respect window resize hints */
#define RESIZE_HINTS false

/* if true, new windows will be assigned to the last activated group */
#define STICKY_WINDOWS false

/* if true, paint borders */
#define BORDERS true

/* if true, focus last window when the currently focused window is unmapped */
#define LAST_WINDOW_FOCUSING true

/* if true, apply settings on windows when they are set (like border color, border width) */
#define APPLY_SETTINGS true

/* When clicking a window to focus it, send the click to it too. */
#define REPLAY_CLICK_ON_FOCUS true

/* default pointer actions */
#define DEFAULT_LEFT_BUTTON_ACTION POINTER_ACTION_MOVE
#define DEFAULT_MIDDLE_BUTTON_ACTION POINTER_ACTION_RESIZE_SIDE
#define DEFAULT_RIGHT_BUTTON_ACTION POINTER_ACTION_RESIZE_CORNER

/* default pointer modifier (super key). Set to XCB_MOD_MASK_1 for alt */
#define POINTER_MODIFIER XCB_MOD_MASK_4

/* default mouse button for click to focus. -1 for none, 0 for any
   1, 2, 3 for left-click, middle-click, right-click */
#define CLICK_TO_FOCUS_BUTTON 0

#endif
