#!/bin/sh
#
# By dialuplama, modified by tudurom
#
# Focus previously focused window. If called with -l and the previously
# focused window is unmapped, map the window.
#
# Requires: wmutils core

WLIST="${WLIST:-/tmp/wlist}"
PFW="$(pfw)"

focus_last="false"
test "$1" = "-l" && focus_last="true"

for id in $(tac "$WLIST"); do
	if [ "$PFW" != "$id" ]; then
		if [ "$focus_last" = "true" ] && !wattr m "$id"; then
			mapw -m "$id"
		fi
		wattr m "$id" && waitron window_focus $id
		break
	fi
done
