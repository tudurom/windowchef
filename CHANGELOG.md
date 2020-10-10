# Change log

## v0.5.0

* [window_put_in_grid now has two additional params to specify how many cells to
	occupy](https://github.com/tudurom/windowchef/issues/52). This is not
	optional. Sorry.
* Each window managed by the WM has a property named `WINDOWCHEF_STATUS` that
	contains data about it. It is updated every time the data is updated, such
	as when you move or resize the window. JSON format.
* Windows can be focused by others (_NET_ACTIVE_WINDOW)
* Windowchef respects chwbb, again. (#51)
* Borders render correctly in some programs (#49)

## v0.5.1

* Fixed [#49 Internal border causes black/transparent border on termite](https://github.com/tudurom/windowchef/issues/49).
* Windowchef will print the cause of error when loading of the config file fails.
* Last window focusing works correctly
* Fixed [#62 'waitron window_rev_cycle' crash report](https://github.com/tudurom/windowchef/issues/62)
* [Added grid movement and resizing
  functions](https://github.com/tudurom/windowchef/pull/64) thanks to @vxid.

## v0.5.2

* Fixed uninstall makefile rule thanks to @xero
