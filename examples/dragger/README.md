# `dragger`

Simple window dragging window that listens for commands on a FIFO.

Commands that you can send to it:

* `drag` - call `xmmv` on the currently focused window, making it follow the
pointer
* `resize` - call `xmrs` on the currently focused window, making it resize
according to the pointer
* `close` - kill `xmmv` or `xmrs`
* `quit` - quit the program
