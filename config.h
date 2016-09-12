/* See the LICENSE file for copyright and license details. */

#ifndef _CONFIG_H
#define _CONFIG_H

#define BORDER_WIDTH 3
/* colors are HTML colors that start in "0x" instead of "#" */
#define COLOR_FOCUS 0x3d637d
#define COLOR_UNFOCUS 0x003b4f
/* gap between the window and the edge of the monitor
 * when snapping or vertically/horizontally maximizing window */
#define GAP 20
/* where to place the cursor when moving/resizing the window */
#define CURSOR_POSITION CENTER
/* number of starting groups, can be modified with waitron at run-time */
#define GROUPS 4
/* focus windows after hovering them with the pointer */
#define SLOPPY_FOCUS true

#endif
