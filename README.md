Windowchef
==========

Cooking windows since 2016
--------------------------

Windowchef is a stacking window manager that doesn't handle keyboard or
pointer inputs. A third party program (like `sxhkd`) is needed in order to
translate keyboard and pointer events to `waitron` commands.

Waitron is a program that sends commands to the window manager through X client
messages. Waitron doesn't print anything on success. The commands and the
parameters are fed as program arguments and waitron delivers them to the
windowchef.

Windowchef is written in C with the help of the XCB library for
communicating with the X server. It supports randr and a subset of ewmh and
icccm.

### Dependencies

Windowchef depends on `xcb` to communicate with the X11 server, `xcb-randr` to
gather information about connected displays and `xcb-util-wm` for ewmh and icccm helper functions.

I couldn't find compiled documentation for `xcb-util-wm` so I compiled it and
put it on my website [here](http://thetudor.ddns.net/res).

Features
--------

* Move, teleport, enlarge/shrink and resize windows
* Maximize windows vertically/horizontally/fully
	* Supports maximizing via EWMH
* Close windows (either killing them or via ICCCM)
* Put windows in a virtual grid.
* Snap windows in the corners or in the middle of the screen
* Cycle windows forwards and backwards
* cwm-like window groups
	* Add or remove windows to a group
	* Activate/Deactivate/Toggle a group
* Simple and stylish solid-color border. Width can be configured
* Gaps around the monitor

### Soon to come

- [ ] Infinite number of borders
- [ ] Move windows with the mouse
- [ ] Resize windows with the mouse

### Groups

A window belongs in a maximum number of 1 groups. You can show or hide as many
groups as you wish. For example, you can have *group 1* with a terminal window
for programming and another one with a shell opened and *group 2* with a
browser window. You can either show only *group 1*, only *group 2* or both
at the same time.

Windowchef allows you to add/remove windows to/from groups, show groups, hide
groups or toggle them.

### Virtual grids

You can tell windowchef to move and resize a window so it can fit in a cell
of a virtual grid. The user specifies the size of the grid and the
coordinates of the cell the window will sit in. For example, I can define a 3x3
grid and put my window in the cell at `x = 1`, `y = 2`:

```
+-----------------------+
|       |        |      |
|-------+--------+------|
|       |        |      |
|-------+--------+------|
| xterm |        |      |
+-----------------------+
```

Bars and panels
---------------

Windowchef doesn't come with a bar/panel on its own. Windowchef ignores
windows with the `_NET_WM_WINDOW_TYPE_DOCK` type. Panels can get
information about the state of the window manager through ewmh properties.

Tested with [lemonbar](https://github.com/lemonboy/bar).

Building windowchef and installing it
-------------------------------------

First, you may need to tweak the search path for some operating systems (e.g.
OpenBSD) in `config.mk`.

Then, just `make` it:

```bash
$ make
$ sudo make install
```
The `Makefile` respects the `DESTDIR` and `PREFIX` variables.

Configuring
-----------

For now configuration is made by editing the `config.h` file. There's not much
you can modify.

Thanks
------

This software was written by tudurom.

Thanks to dcat and z3bra for writing wmutils. Their software helped me learn
the essentials of X11 development.

Thanks to venam, Michaell Cardell and baskerville for the window managers they
made: 2bwm, mcwm and bspwm (ironically they all end in wm :). Their
programs were and still are a very good source of inspirationfor anyone who
wants to write a window manager.
