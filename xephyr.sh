#!/bin/sh

D=${D:-80}

Xephyr -screen 1280x720 :$D&
sleep 1

export DISPLAY=:$D
sxhkd -c examples/sxhkdrc &
exec ./windowchef -c examples/windowchefrc
