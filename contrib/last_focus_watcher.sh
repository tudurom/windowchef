#! /bin/sh
#
# By dialuplama, modified by tudurom
#
# Focus previously focused window when the currently focused window is
# unmapped.
#
# Requires: wmutils core, wmutils opt

WLIST="${WLIST:-/tmp/wlist}"
[ -e "$WLIST" ] && rm "$WLIST"

# Initially we have only one window
pfw > "$WLIST"

wew -m 2097152 | while IFS=: read ev wid; do
	case $ev in
		# Focus in event. Write wid to the window list
		# and remove the previous entry
		9)
			sed -i "/${wid}/d" "$WLIST"
			echo $wid >> "$WLIST"
			;;
		# Destroy event. Delete wid from the window list
		17)
			sed -i "/${wid}/d" "$WLIST"
			;;
		# Unmap event. Focus last wid from the window list.
		# Ignore unmapped windows.
		18)
			for id in $(tac $WLIST); do
				if wattr m $id && [ "$wid" != "$id" ]; then
					waitron window_focus $id
					break
				fi
			done
			;;
	esac
done
