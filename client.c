/* See the LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <xcb/xcb.h>
#include <err.h>
#include <errno.h>

#include "ipc.h"

#ifndef __NAME_CLIENT__
#define __NAME_CLIENT__ "client"
#endif

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

struct Command {
	char *string_command;
	enum IPCCommand command;
	int argc;
	bool (*handler)(uint32_t *, int , char **);
};

/* vim-tabularize is cool, i swear */
static struct Command c[] = {
	{ "window_move"            , IPCWindowMove           , 2 , fn_offset   }  ,
	{ "window_move_absolute"   , IPCWindowMoveAbsolute   , 2 , fn_offset   }  ,
	{ "window_resize"          , IPCWindowResize         , 2 , fn_offset   }  ,
	{ "window_resize_absolute" , IPCWindowResizeAbsolute , 2 , fn_naturals }  ,
	{ "window_maximize"        , IPCWindowMaximize       , 0 , NULL        } ,
	{ "window_hor_maximize"    , IPCWindowHorMaximize    , 0 , NULL        } ,
	{ "window_ver_maximize"    , IPCWindowVerMaximize    , 0 , NULL        } ,
	{ "window_close"           , IPCWindowClose          , 0 , NULL        } ,
	{ "window_put_in_grid"     , IPCWindowPutInGrid      , 4 , fn_naturals } ,
	{ "window_snap"            , IPCWindowSnap           , 1 , fn_naturals } ,
	{ "window_cycle"           , IPCWindowCycle          , 0 , NULL        } ,
	{ "window_rev_cycle"       , IPCWindowRevCycle       , 0 , NULL        } ,
	{ "group_add_window"       , IPCGroupAddWindow       , 1 , fn_naturals } ,
	{ "group_remove_window"    , IPCGroupRemoveWindow    , 0 , NULL        } ,
	{ "group_activate"         , IPCGroupActivate        , 1 , fn_naturals } ,
	{ "group_deactivate"       , IPCGroupDeactivate      , 1 , fn_naturals } ,
	{ "group_toggle"           , IPCGroupToggle          , 1 , fn_naturals } ,
	{ "wm_quit"                , IPCWMQuit               , 1 , fn_naturals } ,
	{ "wm_change_nr_of_groups" , IPCWMChangeNrOfGroups   , 1 , fn_naturals } ,

};

xcb_connection_t *conn;
xcb_screen_t *scr;

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
usage()
{
	fprintf(stderr, "Usage: %s <command> [args]...\n", __NAME_CLIENT__);

	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	int command_argc;
	char **command_argv;
	init_xcb(&conn);

	if (argc == 1)
		usage();

	/* argc - program name - command to send */
	command_argc = argc - 2;
	command_argv = argv + 2;

	i = 0;
	while (i < NR_IPC_COMMANDS && strcmp(argv[1], c[i].string_command) != 0)
		i++;

	if (i < NR_IPC_COMMANDS) {
		if (command_argc < c[i].argc)
			errx(EXIT_FAILURE, "not enough argmuents");
		else if (command_argc > c[i].argc)
			warnx("too many arguments");
		else
			send_command(&c[i], command_argc, command_argv);

	} else {
		errx(EXIT_FAILURE, "no such command");
	}

	if (conn != NULL)
		xcb_disconnect(conn);

	return 0;
}
