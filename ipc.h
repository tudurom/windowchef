/* See the LICENSE file for copyright and license details. */

#ifndef _WM_IPC_H
#define _WM_IPC_H

#define ATOM_COMMAND "__WM_IPC_COMMAND"

#define IPC_MUL_PLUS 0
#define IPC_MUL_MINUS 1

enum IPCCommand {
	IPCWindowMove,
	IPCWindowMoveAbsolute,
	IPCWindowResize,
	IPCWindowResizeAbsolute,
	IPCWindowMaximize,
	IPCWindowHorMaximize,
	IPCWindowVerMaximize,
	IPCWindowMonocle,
	IPCWindowClose,
	IPCWindowPutInGrid,
	IPCWindowSnap,
	IPCWindowCycle,
	IPCWindowRevCycle,
	IPCWindowCycleInGroup,
	IPCWindowRevCycleInGroup,
	IPCWindowCardinalFocus,
	IPCWindowFocus,
	IPCWindowFocusLast,
	IPCGroupAddWindow,
	IPCGroupRemoveWindow,
	IPCGroupRemoveAllWindows,
	IPCGroupActivate,
	IPCGroupDeactivate,
	IPCGroupToggle,
	IPCGroupActivateSpecific,
	IPCWMQuit,
	IPCWMConfig,
	NR_IPC_COMMANDS
};

enum IPCConfig {
	IPCConfigBorderWidth,
	IPCConfigColorFocused,
	IPCConfigColorUnfocused,
	IPCConfigGapWidth,
	IPCConfigGridGapWidth,
	IPCConfigCursorPosition,
	IPCConfigGroupsNr,
	IPCConfigEnableSloppyFocus,
	IPCConfigStickyWindows,
	IPCConfigEnableBorders,
	NR_IPC_CONFIGS
};

#endif
