windowchef(1) -- A stacking window cooker
=====================================

## SYNOPSIS

`windowchef` [-h|-c <config_path>]

## DESCRIPTION

`windowchef` is a stacking window manager that doesn't handle keyboard or
pointer inputs. It is controlled and configured by `waitron`.

At startup,
`windowchef` runs a script located at `$XDG_CONFIG_HOME/windowchef/windowchefrc`
(`$XDG_CONFIG_HOME` is usually `~/.config`). The path of the configuration file can be
overridden with the `-c` flag.

## OPTIONS

* `-h`:
	Print usage and version information.

* `-c` <config_path>:
	Load script from <config_path> instead of
	`$XDG_CONFIG_HOME/windowchef/windowchefrc`.

## SEE ALSO

waitron(1), sxhkd(1), xinit(1), xmmv(1), xmrs(1)

## AUTHOR

Tudor Roman `<tudurom at gmail dot com>`

The default color scheme that comes with `windowchef` is [5725](https://github.com/dkeg/crayolo#5725) by dkeg.

## DERP

I deleted all the manpages from my system by mistake while writing this manual
page.
