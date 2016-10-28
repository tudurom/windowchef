Windowchef
==========

Disclaimer
----------

This software is a work in progress and may [break your workflow](https://xkcd.com/1172/) (yeah, it's that one xkcd comic, everyone
knows it).

Cooking windows since 2016
--------------------------

Windowchef is a stacking window manager that doesn't handle keyboard or
pointer inputs. A third party program (like `sxhkd`) is needed in order to
translate keyboard and pointer events to `waitron` commands.

Waitron is a program that sends commands to the window manager through X client
messages. It doesn't print anything on success. The commands and the
parameters are fed as program arguments and waitron delivers them to the
windowchef.

Windowchef is written in C with the help of the XCB library for
communicating with the X server. It supports randr and a subset of ewmh and
icccm.

Rationale
---------

Window managers are spreading like rats these days (I may be wrong). So what do you want to achieve with this one?

What I want to achieve by writing windowchef is a simple window manager that
provides basic functionality for a more minimal, yet comfortable workflow while
being easily extensible using the standard UNIX way: shell scripts. Cool
features like sorting windows by size on a specific monitor are not
included in windowchef. Instead, the user is encouraged to write a script that
does this using available tools like [wmutils](https://github.com/wmutils)
for window manipulation and [disputils](https://arcetera.moe/git/disputils/log.html) for getting information regarding randr outputs.

Have fun!

Usage
-----

See the bundled `.sxhkdrc` and `.windowchefrc` for an example of a basic
configuration. Additionally, you can read the manual page (`windowchef(1)`).

The default configuration file is `~/.config/windowchef/windowchefrc`. It is usually
a shell script that calls `waitron wm_config` for configuration. See the
manual page for `waitron(1)` for details regarding this topic.


Dependencies
------------

Windowchef depends on `xcb` to communicate with the X11 server, `xcb-randr` to
gather information about connected displays and `xcb-util-wm` for ewmh and icccm helper functions.

I couldn't find compiled documentation for `xcb-util-wm` so I compiled it and
put it on my website [here](http://thetudor.ddns.net/res).

Building windowchef and installing it
-------------------------------------

Available in the AUR:

* [windowchef](https://aur.archlinux.org/packages/windowchef/)
* [windowchef-git](https://aur.archlinux.org/packages/windowchef-git/)

First, you may need to tweak the search path for some operating systems (e.g.
OpenBSD) in `config.mk`.

Then, just `make` it:

```bash
$ make
$ sudo make install
```
The `Makefile` respects the `DESTDIR` and `PREFIX` variables.


Features
--------

* Move, teleport, enlarge/shrink and resize windows
* Maximize windows vertically/horizontally/fully
	* Supports maximizing via EWMH
* Close windows (either by killing them or via ICCCM)
* Put windows in a virtual grid.
* Snap windows in the corners or in the middle of the screen
* Cycle windows forwards and backwards
* cwm-like window groups
	* Add or remove windows to a group
	* Activate/Deactivate/Toggle a group
* Workspaces instead of groups if you want
* Mouse mode that can be either active or inactive. While active, the window is
	moved/resized on click to the coordinates of the mouse pointer.
* Simple and stylish solid-color border. Width can be configured
* Gaps around the monitor and around the cells of the virtual grid(s).
* Configuration script. Windowchef loads a given script at startup that can be
	used for configuration with waitron.
* 100% compatible with window management utilities (e.g. [wmutils](https://github.com/wmutils/)). Windowchef will update its internal state if programs attempt to manage windows. It also applies the correct window decorations when a program focuses a window. This makes it possible to use it as a "backend" with wm utilities.

### Soon to come

- Nothing yet.

### Groups

A window belongs in a maximum number of 1 groups. You can show or hide as many
groups as you wish. For example, you can have *group 1* with a terminal window
for programming and another one with a shell opened and *group 2* with a
browser window. You can either show only *group 1*, only *group 2* or both
at the same time.

Windowchef allows you to add/remove windows to/from groups, show groups, hide
groups or toggle them.

### Workspaces

Instead of using groups, you may want to enable workspaces. Workspaces are, in
fact, groups with the particularity that only one workspace can be active at the
same time.

### Virtual grids

You can tell windowchef to move and resize a window so it can fit in a cell
of a virtual grid. The user specifies the size of the grid and the
coordinates of the cell the window will sit in. For example, I can define a 3x3
grid and put my window in the cell at `x = 0`, `y = 2`:

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

Thanks
------

This software was written by tudurom.

Thanks to dcat and z3bra for writing wmutils. Their software helped me learn
the essentials of X11 development.

Thanks to venam, Michaell Cardell and baskerville for the window managers they
made: 2bwm, mcwm and bspwm (ironically they all end in wm :). Their
programs were and still are a very good source of inspirationfor anyone who
wants to write a window manager.
