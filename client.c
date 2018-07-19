/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#include <xcb/xcb.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ipc.h"
#include "common.h"
#include "types.h"

xcb_connection_t *conn;
xcb_screen_t *scr;

int opterr = 0;

static bool fn_offset(uint32_t *, int, char **);
static bool fn_naturals(uint32_t *, int, char **);
static bool fn_bool(uint32_t *, int, char **);
static bool fn_config(uint32_t *, int, char **);
static bool fn_hex(uint32_t *, int, char **);
static bool fn_position(uint32_t *, int, char **);
static bool fn_gap(uint32_t *, int, char **);
static bool fn_direction(uint32_t *, int, char **);
static bool fn_pac(uint32_t *, int, char **);
static bool fn_mod(uint32_t *, int, char **);
static bool fn_button(uint32_t *, int, char **);
static bool fn_hack(uint32_t *, int, char **);

static void usage(char *, int);
static void version(void);

struct Command {
	char *string_command;
	enum IPCCommand command;
	int argc;
	bool (*handler)(uint32_t *, int , char **);
};

struct ConfigEntry {
	char *key;
	enum IPCConfig config;
	int argc;
	bool (*handler)(uint32_t *, int, char **);
};

/* vim-tabularize is cool, i swear */
static struct Command c[] = {
	{ "window_move"               , IPCWindowMove            ,  2 , fn_offset   } ,
	{ "window_move_absolute"      , IPCWindowMoveAbsolute    ,  2 , fn_offset   } ,
	{ "window_resize"             , IPCWindowResize          ,  2 , fn_offset   } ,
	{ "window_resize_absolute"    , IPCWindowResizeAbsolute  ,  2 , fn_naturals } ,
	{ "window_maximize"           , IPCWindowMaximize        ,  0 , NULL        } ,
	{ "window_unmaximize"         , IPCWindowUnmaximize      ,  0 , NULL        } ,
	{ "window_hor_maximize"       , IPCWindowHorMaximize     ,  0 , NULL        } ,
	{ "window_ver_maximize"       , IPCWindowVerMaximize     ,  0 , NULL        } ,
	{ "window_monocle"            , IPCWindowMonocle         ,  0 , NULL        } ,
	{ "window_close"              , IPCWindowClose           ,  0 , NULL        } ,
	{ "window_put_in_grid"        , IPCWindowPutInGrid       ,  6 , fn_hack     } ,
	{ "window_snap"               , IPCWindowSnap            ,  1 , fn_position } ,
	{ "window_cycle"              , IPCWindowCycle           ,  0 , NULL        } ,
	{ "window_rev_cycle"          , IPCWindowRevCycle        ,  0 , NULL        } ,
	{ "window_cycle_in_group"     , IPCWindowCycleInGroup    ,  0 , NULL        } ,
	{ "window_rev_cycle_in_group" , IPCWindowRevCycleInGroup ,  0 , NULL        } ,
	{ "window_cardinal_focus"     , IPCWindowCardinalFocus   ,  1 , fn_direction} ,
	{ "window_focus"              , IPCWindowFocus           ,  1 , fn_hex      } ,
	{ "window_focus_last"         , IPCWindowFocusLast       ,  0 , NULL        } ,
	{ "group_add_window"          , IPCGroupAddWindow        ,  1 , fn_naturals } ,
	{ "group_remove_window"       , IPCGroupRemoveWindow     ,  0 , NULL        } ,
	{ "group_remove_all_windows"  , IPCGroupRemoveAllWindows ,  1 , fn_naturals } ,
	{ "group_activate"            , IPCGroupActivate         ,  1 , fn_naturals } ,
	{ "group_deactivate"          , IPCGroupDeactivate       ,  1 , fn_naturals } ,
	{ "group_toggle"              , IPCGroupToggle           ,  1 , fn_naturals } ,
	{ "group_activate_specific"   , IPCGroupActivateSpecific ,  1 , fn_naturals } ,
	{ "wm_quit"                   , IPCWMQuit                ,  1 , fn_naturals } ,
	{ "wm_config"                 , IPCWMConfig              , -1 , fn_config   },
};

static struct ConfigEntry configs[] = {
	{ "border_width"        , IPCConfigBorderWidth       , 1 , fn_naturals },
	{ "color_focused"       , IPCConfigColorFocused      , 1 , fn_hex      },
	{ "color_unfocused"     , IPCConfigColorUnfocused    , 1 , fn_hex      },
	{ "internal_border_width", IPCConfigInternalBorderWidth, 1 , fn_naturals },
	{ "internal_color_focused", IPCConfigInternalColorFocused, 1 , fn_hex },
	{ "internal_color_unfocused", IPCConfigInternalColorUnfocused, 1 , fn_hex },
	{ "gap_width"           , IPCConfigGapWidth          , 2 , fn_gap      },
	{ "grid_gap_width"      , IPCConfigGridGapWidth      , 1 , fn_naturals },
	{ "cursor_position"     , IPCConfigCursorPosition    , 1 , fn_position },
	{ "groups_nr"           , IPCConfigGroupsNr          , 1 , fn_naturals },
	{ "enable_sloppy_focus" , IPCConfigEnableSloppyFocus , 1 , fn_bool     },
	{ "enable_resize_hints" , IPCConfigEnableResizeHints , 1 , fn_bool     },
	{ "sticky_windows"      , IPCConfigStickyWindows     , 1 , fn_bool     },
	{ "enable_borders"      , IPCConfigEnableBorders     , 1 , fn_bool     },
	{ "enable_last_window_focusing", IPCConfigEnableLastWindowFocusing, 1 , fn_bool },
	{ "apply_settings"      , IPCConfigApplySettings     , 1 , fn_bool     },
	{ "replay_click_on_focus" , IPCConfigReplayClickOnFocus, 1, fn_bool    },
	{ "pointer_actions"     , IPCConfigPointerActions    , 3 , fn_pac      },
	{ "pointer_modifier"    , IPCConfigPointerModifier   , 1 , fn_mod      },
	{ "click_to_focus"      , IPCConfigClickToFocus      , 1 , fn_button   },
};

/*
 * An offset is a pair of two signed integers.
 *
 * data[0], data[1] - if 1, then the number in negative
 * data[2], data[3] - the actual numbers, unsigned
 */
static bool
fn_offset(uint32_t *data, int argc, char **argv)
{
	int i = 0;
	do {
		errno = 0;
		int c = strtol(argv[i], NULL, 10);
		if (c >= 0)
			data[i] = IPC_MUL_PLUS;
		else
			data[i] = IPC_MUL_MINUS;
		data[i + 2] = abs(c);
		i++;
	} while (i < argc && errno == 0);

	if (errno != 0)
		return false;
	else
		return true;
}

static bool
fn_naturals(uint32_t *data, int argc, char **argv)
{
	int i = 0;
	do {
		errno = 0;
		data[i] = strtol(argv[i], NULL, 10);
		i++;
	} while (i < argc && errno == 0);

	if (errno != 0)
		return false;
	return true;
}

static bool
fn_bool(uint32_t *data, int argc, char **argv) {
	int i = 0;
	char *arg;
	do {
		arg = argv[i];
		if (strcasecmp(argv[i], "true")       == 0
					|| strcasecmp(arg, "yes") == 0
					|| strcasecmp(arg, "t")   == 0
					|| strcasecmp(arg, "y")   == 0
					|| strcasecmp(arg, "1")   == 0)
				data[i] = true;
		else
			data[i] = false;
		i++;
	} while (i < argc);

	return true;
}

static bool
fn_config(uint32_t *data, int argc, char **argv) {
	char *key;
	bool status;
	int i;

	key = argv[0];

	i = 0;
	while (i < NR_IPC_CONFIGS && strcmp(key, configs[i].key) != 0)
		i++;

	if (i < NR_IPC_CONFIGS) {
		if (configs[i].argc != argc - 1)
			errx(EXIT_FAILURE, "too many or not enough arguments. Want: %d", configs[i].argc);
		data[0] = configs[i].config;
		status = (configs[i].handler)(data + 1, argc - 1, argv + 1);

		if (status == false)
			errx(EXIT_FAILURE, "malformed input");
	} else {
		errx(EXIT_FAILURE, "no such config key");
	}
	return true;
}

static bool
fn_hex(uint32_t *data, int argc, char **argv)
{
	int i = 0;
	do {
		errno = 0;
		data[i] = strtol(argv[i], NULL, 16);
		i++;
	} while (i < argc && errno == 0);

	if (errno != 0)
		return false;
	else
		return true;
}

static bool
fn_direction(uint32_t *data, int argc, char **argv)
{
	char *pos = argv[0];
	enum direction dir_sel;

	if (strcasecmp(pos, "up") == 0 || strcasecmp(pos, "north") == 0)
		dir_sel = NORTH;
	else if (strcasecmp(pos, "down") == 0 || strcasecmp(pos, "south") == 0)
		dir_sel = SOUTH;
	else if (strcasecmp(pos, "left") == 0 || strcasecmp(pos, "west") == 0)
		dir_sel = WEST;
	else if (strcasecmp(pos, "right") == 0 || strcasecmp(pos, "east") == 0)
		dir_sel = EAST;
	else
		return false;

	(void)(argc);
	data[0] = dir_sel;

	return true;
}

static bool
fn_pac(uint32_t *data, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		char *pac = argv[i];
		if (strcasecmp(pac, "nothing") == 0)
			data[i] = POINTER_ACTION_NOTHING;
		else if (strcasecmp(pac, "focus") == 0)
			data[i] = POINTER_ACTION_FOCUS;
		else if (strcasecmp(pac, "move") == 0)
			data[i] = POINTER_ACTION_MOVE;
		else if (strcasecmp(pac, "resize_corner") == 0)
			data[i] = POINTER_ACTION_RESIZE_CORNER;
		else if (strcasecmp(pac, "resize_side") == 0)
			data[i] = POINTER_ACTION_RESIZE_SIDE;
		else
			return false;
	}

	return true;
}
static bool
fn_mod(uint32_t *data, int argc, char **argv)
{
	(void)(argc);
	if (strcasecmp(argv[0], "alt") == 0)
		data[0] = XCB_MOD_MASK_1;
	else if (strcasecmp(argv[0], "super") == 0)
		data[0] = XCB_MOD_MASK_4;
	else
		return false;

	return true;
}
static bool
fn_button(uint32_t *data, int argc, char **argv)
{
	char *btn = argv[0];
	(void)(argc);

	if (strcasecmp(btn, "left") == 0)
		data[0] = 1;
	else if (strcasecmp(btn, "middle") == 0)
		data[0] = 2;
	else if (strcasecmp(btn, "right") == 0)
		data[0] = 3;
	else if (strcasecmp(btn, "none") == 0)
		data[0] = UINT32_MAX;
	else if (strcasecmp(btn, "any") == 0)
		data[0] = 0;
	else
		return false;

	return true;
}

/*
 * Kinda like fn_naturals, but each two numbers are put as 16-bit numbers
 * in one uint32_t.
 */
static bool
fn_hack(uint32_t *data, int argc, char **argv)
{
	int i = 0, j = 0;
	unsigned long d;
	do {
		errno = 0;
		d = strtoul(argv[i], NULL, 10);
		if (i % 2 == 0) {
			data[j] = d << 16U;
		} else {
			data[j] |= d;
			j++;
		}
		i++;
	} while (i < argc && errno == 0);

	if (i % 2 == 1 || errno != 0)
		return false;
	return true;
}

static bool
fn_position(uint32_t *data, int argc, char **argv)
{
	char *pos = argv[0];
	enum position snap_pos;

	if (strcasecmp(pos, "topleft") == 0)
		snap_pos = TOP_LEFT;
	else if (strcasecmp(pos, "topright") == 0)
		snap_pos = TOP_RIGHT;
	else if (strcasecmp(pos, "bottomleft") == 0)
		snap_pos = BOTTOM_LEFT;
	else if (strcasecmp(pos, "bottomright") == 0)
		snap_pos = BOTTOM_RIGHT;
	else if (strcasecmp(pos, "middle") == 0)
		snap_pos = CENTER;
	else if (strcasecmp(pos, "left") == 0)
		snap_pos = LEFT;
	else if (strcasecmp(pos, "bottom") == 0)
		snap_pos = BOTTOM;
	else if (strcasecmp(pos, "top") == 0)
		snap_pos = TOP;
	else if (strcasecmp(pos, "right") == 0)
		snap_pos = RIGHT;
	else if (strcasecmp(pos, "all") == 0)
		snap_pos = ALL;
	else
		return false;

	(void)(argc);
	data[0] = snap_pos;

	return true;
}

static bool
fn_gap(uint32_t *data, int argc, char **argv)
{
	(void)(argc);
	bool status = true;

	status = status && fn_position(data, 1, argv);
	status = status && fn_naturals(data + 1, 1, argv + 1);

	return status;
}

static void
init_xcb(xcb_connection_t **conn)
{
	*conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(*conn))
		errx(EXIT_FAILURE, "unable to connect to X server");
	scr = xcb_setup_roots_iterator(xcb_get_setup(*conn)).data;
}

static xcb_atom_t
get_atom(char *name)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);

	if (!reply)
		return XCB_ATOM_STRING;

	return reply->atom;
}

static void
send_command(struct Command *c, int argc, char **argv)
{
	xcb_client_message_event_t msg;
	xcb_client_message_data_t data;
	xcb_generic_error_t *err;
	xcb_void_cookie_t cookie;
	bool status = true;

	msg.response_type = XCB_CLIENT_MESSAGE;
	msg.type = get_atom(ATOM_COMMAND);
	msg.format = 32;
	data.data32[0] = c->command;
	if (c->handler != NULL)
		status = (c->handler)(data.data32 + 1, argc, argv);
	if (status == false)
		errx(EXIT_FAILURE, "malformed input");

	msg.data = data;

	cookie = xcb_send_event_checked(conn, false, scr->root,
			XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char *)&msg);

	err = xcb_request_check(conn, cookie);
	if (err)
		fprintf(stderr, "oops %d\n", err->error_code);
	xcb_flush(conn);
}

static void
usage(char *name, int status)
{
	fprintf(stderr, "Usage: %s [-h|-v] <command> [args...]\n", name);
	exit(status);
}

static void
version(void)
{

	fprintf(stderr, "%s %s\n", __NAME_CLIENT__, __THIS_VERSION__);
	fprintf(stderr, "Copyright (c) 2016-2017 Tudor Ioan Roman\n");
	fprintf(stderr, "Released under the ISC License\n");

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int i;
	int command_argc;
	char **command_argv;

	if (argc == 1) {
		usage(argv[0], EXIT_FAILURE);
	} else if (argc > 1) {
		if (strcmp(argv[1], "-h") == 0)
			usage(argv[0], EXIT_SUCCESS);
		else if (strcmp(argv[1], "-v") == 0)
			version();
	}

	init_xcb(&conn);

	/* argc - program name - command to send */
	command_argc = argc - 2;
	command_argv = argv + 2;

	i = 0;
	while (i < NR_IPC_COMMANDS && strcmp(argv[1], c[i].string_command) != 0)
		i++;

	if (i < NR_IPC_COMMANDS) {
		if (c[i].argc != -1) {
			if (command_argc < c[i].argc)
				errx(EXIT_FAILURE, "not enough arguments");
			else if (command_argc > c[i].argc)
				warnx("too many arguments");
		}
		if (c[i].argc == -1 || command_argc == c[i].argc)
			send_command(&c[i], command_argc, command_argv);

	} else {
		errx(EXIT_FAILURE, "no such command");
	}

	if (conn != NULL)
		xcb_disconnect(conn);

	return 0;
}
