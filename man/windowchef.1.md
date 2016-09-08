windowchef(1) -- A static window cooker
=====================================

## SYNOPSIS

`windowchef` [-v]

## DESCRIPTION

`windowchef` is a stacking window manager that doesn't handle keyboard or
pointer inputs. It is controlled by `waitron` and configured by editing the
`config.h` file and recompiling.

## OPTIONS

* `-v`:
	Print version information.

## COMMANDS

* `window_move` <x> <y>:
	Move the focused window <y> pixels horizontally and <y> pixels
	vertically.

* `window_move_absolute` <x> <y>:
	Move the focused window to the position given by <x> and <y>.

* `window_resize` <x> <y>:
	Resize the focused window based on the relative pixel values <x> and <y>.

* `window_resize_absolute` <width> <height>:
	Resize the focused window up to <width> and <height>.

* `window_maximize`:
	Hide the border and maximize the focused window on the current monitor.
	Execute it again after maximizing to revert the state of the window.

* `window_hor_maximize`:
	Horizontally maximize the focused window on the current monitor, preserving
	its <x> component. Leaves a gap at the left and right of the monitor that
	can be configured. Execute it again after maximizing to revert the state of the window.

* `window_ver_maximize`:
	Vertically maximize the focused window on the current monitor, preserving
	its <y> component. Leaves a gap at the top and bottom of the monitor whose
	width can be configured. Execute it again after maximizing to revert the state of the window.

* `window_close`:
	Closes the focused window.

* `window_put_in_grid` <grid_width> <grid_height> <cell_x> <cell_y>:
	Moves and resizes the focused windows accordingly to fit in a cell defined
	by the <cell_x> and <cell_y> coordinates in a virtual grid with width
	<grid_width> and height <grid_height> on the current monitor.

* `window_snap` <pos>:
	Snap the window on the screen in a position defined by <pos>. <pos> can be:

	`0` for top-left.

	`1` for to-right.

	`2` for bottom-left.

	`3` for bottom-right.

	`4` for the middle.

* `window_cycle`:
	Cycle through mapped windows.

* `window_rev_cycle`:
	Reverse cycle through mapped windows.

* `group_add_window` <group_nr>:
	Add the focused window to the <group_nr> group.

	Group numbers start from 0 and end at <GROUPS_NO> - 1.

* `group_remove_window`:
	Remove the focused window from its current group.

* `group_activate` <group_nr>:
	Map all windows that belong to the <group_nr> group.

* `group_deactivate` <group_nr>:
	Unmap all windows that belong to the <group_nr> group.

* `group_toggle` <group_nr>:
	Toggle the <group_nr> group.

* `wm_quit` <exit_status>:
	Quit windowchef with exit_status <exit_status>.

* `wm_change_number_of_groups` <number_of_groups>:
	Change the number of maximum groups to <number_of_groups>.

	Windows that belong to a group that has a <group_nr> greater or equal to
	<number_of_groups> will become orphaned.

## AUTHOR

Tudor Roman `<tudurom at gmail dot com>`

## DISCLAIMER

I deleted all the manpages from my system by mistake while writing this thing.
