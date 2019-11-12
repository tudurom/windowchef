/* Copyright (c) 2016-2019 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#ifndef WM_IPC_H
#define WM_IPC_H

#define ATOM_COMMAND "__WM_IPC_COMMAND"

#define IPC_MUL_PLUS 0
#define IPC_MUL_MINUS 1

enum IPCCommand {
	IPCWindowMove,
	IPCWindowMoveAbsolute,
	IPCWindowResize,
	IPCWindowResizeAbsolute,
	IPCWindowMaximize,
	IPCWindowUnmaximize,
	IPCWindowHorMaximize,
	IPCWindowVerMaximize,
	IPCWindowMonocle,
	IPCWindowClose,
	IPCWindowPutInGrid,
	IPCWindowMoveInGrid,
	IPCWindowResizeInGrid,
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
	IPCConfigInternalBorderWidth,
	IPCConfigInternalColorFocused,
	IPCConfigInternalColorUnfocused,
	IPCConfigGapWidth,
	IPCConfigGridGapWidth,
	IPCConfigCursorPosition,
	IPCConfigGroupsNr,
	IPCConfigEnableSloppyFocus,
	IPCConfigEnableResizeHints,
	IPCConfigStickyWindows,
	IPCConfigEnableBorders,
	IPCConfigEnableLastWindowFocusing,
	IPCConfigApplySettings,
	IPCConfigReplayClickOnFocus,
	IPCConfigPointerActions,
	IPCConfigPointerModifier,
	IPCConfigClickToFocus,
	NR_IPC_CONFIGS
};

#endif
