/* See the LICENSE file for copyright and license details. */

#ifndef _CONFIG_H
#define _CONFIG_H

#define BORDER_WIDTH 5

/* colors are HTML colors that start in "0x" instead of "#" */
#define COLOR_FOCUS 0x97a293
#define COLOR_UNFOCUS 0x393638

/* gap between the window and the edge of the monitor
 * when snapping or vertically/horizontally maximizing window */
#define GAP 0

#define GRID_GAP 0

/* where to place the cursor when moving/resizing the window */
#define CURSOR_POSITION CENTER

/* number of starting groups, can be modified with waitron at run-time */
#define GROUPS 10

/* focus windows after hovering them with the pointer */
#define SLOPPY_FOCUS true

/* if true, new windows will be assigned to the last activated group */
#define STICKY_WINDOWS false

/* if true, paint borders */
#define BORDERS true

#endif
