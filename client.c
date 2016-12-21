/* See the LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <xcb/xcb.h>
#include <err.h>
#include <errno.h>

#include "ipc.h"
#include "common.h"
#include "types.h"

xcb_connection_t *conn;
xcb_screen_t *scr;

static bool fn_offset(uint32_t *, int, char **);
static bool fn_naturals(uint32_t *, int, char **);
static bool fn_bool(uint32_t *, int, char **);
static bool fn_config(uint32_t *, int, char **);
static bool fn_hex(uint32_t *, int, char **);
static bool fn_position(uint32_t *, int, char **);
static bool fn_gap(uint32_t *, int, char **);

struct Command {
	char *string_command;
	enum IPCCommand command;
	int argc;
	bool (*handler)(uint32_t *, int , char **);
};

struct ConfigEntry {
	char *key;
	enum IPCConfig config;
	bool (*handler)(uint32_t *, int, char **);
};

/* vim-tabularize is cool, i swear */
static struct Command c[] = {
	{ "window_move"               , IPCWindowMove            ,  2 , fn_offset   } ,
	{ "window_move_absolute"      , IPCWindowMoveAbsolute    ,  2 , fn_offset   } ,
	{ "window_resize"             , IPCWindowResize          ,  2 , fn_offset   } ,
	{ "window_resize_absolute"    , IPCWindowResizeAbsolute  ,  2 , fn_naturals } ,
	{ "window_maximize"           , IPCWindowMaximize        ,  0 , NULL        } ,
	{ "window_hor_maximize"       , IPCWindowHorMaximize     ,  0 , NULL        } ,
	{ "window_ver_maximize"       , IPCWindowVerMaximize     ,  0 , NULL        } ,
	{ "window_monocle"            , IPCWindowMonocle         ,  0 , NULL        } ,
	{ "window_close"              , IPCWindowClose           ,  0 , NULL        } ,
	{ "window_put_in_grid"        , IPCWindowPutInGrid       ,  4 , fn_naturals } ,
	{ "window_snap"               , IPCWindowSnap            ,  1 , fn_position } ,
	{ "window_cycle"              , IPCWindowCycle           ,  0 , NULL        } ,
	{ "window_rev_cycle"          , IPCWindowRevCycle        ,  0 , NULL        } ,
	{ "window_cycle_in_group"     , IPCWindowCycleInGroup    ,  0 , NULL        } ,
	{ "window_rev_cycle_in_group" , IPCWindowRevCycleInGroup ,  0 , NULL        } ,
	{ "window_focus"              , IPCWindowFocus           ,  1 , fn_hex      } ,
	{ "group_add_window"          , IPCGroupAddWindow        ,  1 , fn_naturals } ,
	{ "group_remove_window"       , IPCGroupRemoveWindow     ,  0 , NULL        } ,
	{ "group_activate"            , IPCGroupActivate         ,  1 , fn_naturals } ,
	{ "group_deactivate"          , IPCGroupDeactivate       ,  1 , fn_naturals } ,
	{ "group_toggle"              , IPCGroupToggle           ,  1 , fn_naturals } ,
	{ "group_activate_specific"   , IPCGroupActivateSpecific ,  1 , fn_naturals } ,
	{ "wm_quit"                   , IPCWMQuit                ,  1 , fn_naturals } ,
	{ "wm_config"                 , IPCWMConfig              , -1 , fn_config   },
};

static struct ConfigEntry configs[] = {
	{ "border_width"        , IPCConfigBorderWidth       , fn_naturals },
	{ "color_focused"       , IPCConfigColorFocused      , fn_hex      },
	{ "color_unfocused"     , IPCConfigColorUnfocused    , fn_hex      },
	{ "gap_width"           , IPCConfigGapWidth          , fn_gap      },
	{ "grid_gap_width"      , IPCConfigGridGapWidth      , fn_naturals },
	{ "cursor_position"     , IPCConfigCursorPosition    , fn_position },
	{ "groups_nr"           , IPCConfigGroupsNr          , fn_naturals },
	{ "enable_sloppy_focus" , IPCConfigEnableSloppyFocus , fn_bool     },
	{ "sticky_windows"      , IPCConfigStickyWindows     , fn_bool     },
	{ "enable_borders"      , IPCConfigEnableBorders     , fn_bool     },
};

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
	else
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
	char *key, *value;
	bool status;
	int i;

	key = argv[0];
	value = argv[1];

	i = 0;
	while (i < NR_IPC_CONFIGS && strcmp(key, configs[i].key) != 0)
		i++;

	if (i < NR_IPC_CONFIGS) {
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
	size_t str_size;

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

void
usage(char *name)
{
	fprintf(stderr, "Usage: %s <command> [args...]\n", name);
	fprintf(stderr, "\n");
	fprintf(stderr, "%s %s\n", __NAME_CLIENT__, __THIS_VERSION__);
	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	int command_argc;
	char **command_argv;

	if (argc == 1 || (argc == 2 && strcmp(argv[1], "-h") == 0))
		usage(argv[0]);

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
