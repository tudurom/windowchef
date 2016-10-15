/* See the LICENSE file for copyright and license details. */

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/randr.h>

#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "common.h"
#include "ipc.h"
#include "list.h"
#include "types.h"
#include "config.h"

#define EVENT_MASK(ev) ((ev & ~0x80))
/* XCB event with the biggest values */
#define LAST_XCB_EVENT XCB_GET_MODIFIER_MAPPING
#define NULL_GROUP 0xffffffff

/* atoms identifiers */
enum { WM_DELETE_WINDOW, _IPC_ATOM_COMMAND, NR_ATOMS };

/* connection to the X server */
static xcb_connection_t *conn;
static xcb_ewmh_connection_t *ewmh;
static xcb_screen_t *scr;
static struct client *focused_win;
static struct conf conf;
/* number of the screen we're using */
static int scrno;
/* base for checking randr events */
static int  randr_base;
static bool halt;
static int  exit_code;
static bool *group_in_use = NULL;
static enum mouse_mode current_mouse_mode = MOUSE_NONE;
static struct client * hovered_client = NULL;
static bool hovering_mouse = false;
/* list of all windows. NULL is the empty list */
static struct list_item *win_list   = NULL;
static struct list_item *mon_list   = NULL;
static char *atom_names[NR_ATOMS] = {
	"WM_DELETE_WINDOW",
	ATOM_COMMAND,
};
static xcb_atom_t ATOMS[NR_ATOMS];
/* function handlers for ipc commands */
static void (*ipc_handlers[NR_IPC_COMMANDS])(uint32_t *);
/* function handlers for events received from the X server */
static void (*events[LAST_XCB_EVENT + 1])(xcb_generic_event_t *);

static void cleanup(void);
static int  setup(void);
static int  setup_randr(void);
static void get_randr(void);
static void get_outputs(xcb_randr_output_t *, int len, xcb_timestamp_t);
static struct monitor * find_monitor(xcb_randr_output_t);
static struct monitor * find_monitor_by_coord(int16_t, int16_t);
static struct monitor * find_clones(xcb_randr_output_t, int16_t, int16_t);
static struct monitor * add_monitor(xcb_randr_output_t, char *, int16_t, int16_t, uint16_t, uint16_t);
static void free_monitor(struct monitor *);
static void get_monitor_size(struct client *, int16_t *, int16_t *, uint16_t *, uint16_t *);
static void arrange_by_monitor(struct monitor *);
static void handle_events(void);
static struct client * setup_window(xcb_window_t);
static void set_focused_no_raise(struct client *);
static void set_focused(struct client *);
static void raise_window(xcb_window_t);
static void close_window(struct client *);
static void delete_window(xcb_window_t);
static void teleport_window(xcb_window_t, int16_t, int16_t);
static void move_window(xcb_window_t , int16_t, int16_t);
static void resize_window_absolute(xcb_window_t, uint16_t, uint16_t);
static void resize_window(xcb_window_t, int16_t, int16_t);
static void fit_on_screen(struct client *);
static void maximize_window(struct client *, int16_t, int16_t, uint16_t, uint16_t);
static void hmaximize_window(struct client *, int16_t, uint16_t);
static void vmaximize_window(struct client *, int16_t, uint16_t);
static void monocle_window(struct client *, int16_t, int16_t, uint16_t, uint16_t);
static void unmaximize_window(struct client *);
static void cycle_window(struct client *);
static void rcycle_window(struct client *);
static void cycle_window_in_group(struct client *);
static void rcycle_window_in_group(struct client *);
static void save_original_size(struct client *);
static xcb_atom_t get_atom(char *);
static bool get_pointer_location(xcb_window_t *, int16_t *, int16_t *);
static void center_pointer(struct client *);
static struct client * find_client(xcb_window_t *);
static bool get_geometry(xcb_window_t *, int16_t *, int16_t *, uint16_t *, uint16_t *);
static void set_borders(struct client *client, uint32_t);
static bool is_mapped(xcb_window_t);
static void free_window(struct client *);
static void add_to_client_list(xcb_window_t);
static void update_client_list(void);
static void update_wm_desktop(struct client *);
static void group_add_window(struct client *, uint32_t);
static void group_remove_window(struct client *);
static void group_activate(uint32_t);
static void group_deactivate(uint32_t);
static void group_toggle(uint32_t);
static void change_nr_of_groups(uint32_t);
static void mouse_start(enum mouse_mode);
static void mouse_stop(void);
static void mouse_toggle(enum mouse_mode);
static void register_event_handlers(void);
static void event_configure_request(xcb_generic_event_t *);
static void event_destroy_notify(xcb_generic_event_t *);
static void event_enter_notify(xcb_generic_event_t *);
static void event_map_request(xcb_generic_event_t *);
static void event_map_notify(xcb_generic_event_t *);
static void event_unmap_notify(xcb_generic_event_t *);
static void event_configure_notify(xcb_generic_event_t *);
static void event_circulate_request(xcb_generic_event_t *);
static void event_client_message(xcb_generic_event_t *);
static void event_focus_out(xcb_generic_event_t *);
static void event_motion_notify(xcb_generic_event_t *);
static void register_ipc_handlers(void);
static void ipc_window_move(uint32_t *);
static void ipc_window_move_absolute(uint32_t *);
static void ipc_window_resize(uint32_t *);
static void ipc_window_resize_absolute(uint32_t *);
static void ipc_window_maximize(uint32_t *);
static void ipc_window_hor_maximize(uint32_t *);
static void ipc_window_ver_maximize(uint32_t *);
static void ipc_window_monocle(uint32_t *);
static void ipc_window_unmaximize(uint32_t *);
static void ipc_window_close(uint32_t *);
static void ipc_window_put_in_grid(uint32_t *);
static void ipc_window_snap(uint32_t *);
static void ipc_window_cycle(uint32_t *);
static void ipc_window_rev_cycle(uint32_t *);
static void ipc_window_cycle_in_group(uint32_t *);
static void ipc_window_rev_cycle_in_group(uint32_t *);
static void ipc_window_focus(uint32_t *);
static void ipc_group_add_window(uint32_t *);
static void ipc_group_remove_window(uint32_t *);
static void ipc_group_activate(uint32_t *);
static void ipc_group_deactivate(uint32_t *);
static void ipc_group_toggle(uint32_t *);
static void ipc_mouse_start(uint32_t *);
static void ipc_mouse_stop(uint32_t *);
static void ipc_mouse_toggle(uint32_t *);
static void ipc_wm_quit(uint32_t *);
static void ipc_wm_config(uint32_t *);

static void usage(char *);
static void load_defaults(void);
static void load_config(char *);

/*
 * Gracefully disconnect.
 */

static void
cleanup(void)
{
	xcb_set_input_focus(conn, XCB_NONE, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_CURRENT_TIME);
	if (ewmh != NULL)
		xcb_ewmh_connection_wipe(ewmh);
	if (win_list != NULL)
		list_delete_all_items(&win_list);
	if (conn != NULL)
		xcb_disconnect(conn);
}

/*
 * Connect to the X server and initialize some things.
 */

static int
setup(void)
{
	/* init xcb and grab events */
	unsigned int values[1];
	int mask;

	conn = xcb_connect(NULL, &scrno);
	if (xcb_connection_has_error(conn)) {
		return -1;
	}

	/* get the first screen. hope it's the last one too */
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	focused_win = NULL;

	mask = XCB_CW_EVENT_MASK;
	values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
	xcb_generic_error_t *e = xcb_request_check(conn,
			xcb_change_window_attributes_checked(conn, scr->root,
				mask, values));
	if (e != NULL) {
		free(e);
		errx(EXIT_FAILURE, "Another window manager is already running.");
	}

	/* initialize ewmh variables */
	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	if (!ewmh)
		warnx("couldn't set up ewmh connection");
	xcb_intern_atom_cookie_t *cookie = xcb_ewmh_init_atoms(conn, ewmh);
	xcb_ewmh_init_atoms_replies(ewmh, cookie, (void *)0);
	xcb_ewmh_set_wm_pid(ewmh, scr->root, getpid());
	xcb_ewmh_set_wm_name(ewmh, scr->root, strlen(__NAME__), __NAME__);
	xcb_ewmh_set_current_desktop(ewmh, 0, 0);
	xcb_ewmh_set_number_of_desktops(ewmh, 0, GROUPS);

	xcb_atom_t supported_atoms[] = {
		ewmh->_NET_SUPPORTED               , ewmh->_NET_WM_DESKTOP              ,
		ewmh->_NET_NUMBER_OF_DESKTOPS      , ewmh->_NET_CURRENT_DESKTOP         ,
		ewmh->_NET_ACTIVE_WINDOW           , ewmh->_NET_WM_STATE                ,
		ewmh->_NET_WM_STATE_FULLSCREEN     , ewmh->_NET_WM_STATE_MAXIMIZED_VERT ,
		ewmh->_NET_WM_STATE_MAXIMIZED_HORZ , ewmh->_NET_WM_NAME                 ,
		ewmh->_NET_WM_ICON_NAME            , ewmh->_NET_WM_WINDOW_TYPE          ,
		ewmh->_NET_WM_WINDOW_TYPE_DOCK     , ewmh->_NET_WM_PID                  ,
		ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR  , ewmh->_NET_WM_WINDOW_TYPE_DESKTOP  ,
	};
	xcb_ewmh_set_supported(ewmh, scrno, sizeof(supported_atoms) / sizeof(xcb_atom_t), supported_atoms);

	/* send requests */
	xcb_flush(conn);

	/* get various atoms for icccm and ewmh */
	for (int i = 0; i < NR_ATOMS; i++)
		ATOMS[i] = get_atom(atom_names[i]);

	randr_base = setup_randr();

	group_in_use = malloc(conf.groups * sizeof(bool));
	for (uint32_t i = 0; i < conf.groups; i++)
		group_in_use[i] = false;
	return 0;
}

/*
 * Tells the server we want to use randr.
 */

static int
setup_randr(void)
{
	int base;
	const xcb_query_extension_reply_t *r = xcb_get_extension_data(conn, &xcb_randr_id);

	if (!r->present)
		return -1;
	else
		get_randr();

	base = r->first_event;
	xcb_randr_select_input(conn, scr->root,
			XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
			| XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE
			| XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
			| XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

	return base;
}

/*
 * Get information regarding randr.
 */

static void
get_randr(void)
{
	int len;
	xcb_randr_get_screen_resources_current_cookie_t c
		= xcb_randr_get_screen_resources_current(conn, scr->root);
	xcb_randr_get_screen_resources_current_reply_t *r
		= xcb_randr_get_screen_resources_current_reply(conn, c, NULL);

	if (r == NULL)
		return;

	xcb_timestamp_t timestamp = r->config_timestamp;
	len = xcb_randr_get_screen_resources_current_outputs_length(r);
	xcb_randr_output_t *outputs
		= xcb_randr_get_screen_resources_current_outputs(r);

	/* Request information for all outputs */
	get_outputs(outputs, len, timestamp);
	free(r);
}

/*
 * Gets information about connected outputs.
 */

static void
get_outputs(xcb_randr_output_t *outputs, int len, xcb_timestamp_t timestamp)
{
	int name_len;
	char *name;
	xcb_randr_get_crtc_info_cookie_t info_c;
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_reply_t *output;
	struct monitor *mon, *clonemon;
	struct list_item *item;
	xcb_randr_get_output_info_cookie_t out_cookie[len];

	for (int i = 0; i < len; i++)
		out_cookie[i] = xcb_randr_get_output_info(conn, outputs[i],
				timestamp);

	for (int i = 0; i < len; i++) {
		output = xcb_randr_get_output_info_reply(conn, out_cookie[i], NULL);
		if (output == NULL)
			continue;

		name_len = xcb_randr_get_output_info_name_length(output);
		if (16 < name_len)
			name_len = 16;

		/* +1 for the null character */
		name = malloc(name_len + 1);
		/* make sure the name is at most name_len + 1 length
		 * or we may run into problems. */
		snprintf(name, name_len + 1, "%.*s", name_len,
				xcb_randr_get_output_info_name(output));

		if (output->crtc != XCB_NONE) {
			info_c = xcb_randr_get_crtc_info(conn, output->crtc,
					timestamp);
			crtc = xcb_randr_get_crtc_info_reply(conn, info_c, NULL);

			if (crtc == NULL)
				return;

			clonemon = find_clones(outputs[i], crtc->x, crtc->y);
			if (clonemon != NULL)
				continue;

			mon = find_monitor(outputs[i]);
			if (mon == NULL) {
				add_monitor(outputs[i], name, crtc->x, crtc->y,
						crtc->width, crtc->height);
			} else {
				mon->x = crtc->x;
				mon->y = crtc->y;
				mon->width = crtc->width;
				mon->height = crtc->height;

				arrange_by_monitor(mon);
			}

			free(crtc);
		} else {
			/* Check if the monitor was used before
			 * becoming disabled. */
			mon = find_monitor(outputs[i]);
			if (mon) {
				struct client *client;
				for (item = win_list; item != NULL; item = item->next) {
					/* Move window from this monitor to
					 * either the next one or the first one. */
					client = item->data;

					if (client->monitor == mon) {
						if (client->monitor->item->next)
							// If at end, take from the beginning
							if (mon_list == NULL)
								client->monitor = NULL;
							else
								client->monitor = mon_list->data;
						else
							client->monitor = client->monitor->item->next->data;
						fit_on_screen(client);
					}
				}

				/* Monitor not active. Delete it. */
				free_monitor(mon);
			}
		}

		if (output != NULL)
			free(output);
		free(name);
	}
}

/*
 * Finds a monitor in the list.
 */

struct monitor *
find_monitor(xcb_randr_output_t mon)
{
	struct list_item *item;
	struct monitor *m;

	item = mon_list;
	while (item != NULL && (m = item->data)->monitor != mon)
		item = item->next;

	if (item == NULL)
		return NULL;
	else
		return item->data;
}

/*
 * Find a monitor in the list by its coordinates.
 */

static struct monitor *
find_monitor_by_coord(int16_t x, int16_t y)
{
	struct list_item *item;
	struct monitor *m = NULL;

	for (item = mon_list; item != NULL; item = item->next) {
		m = item->data;

		if (x >= m->x && x <= m->x + m->width
			&& y >= m->y && y <= m->y + m->height)
			break;
	}

	return m;
}

/*
 * Find cloned (mirrored) outputs.
 */

struct monitor *
find_clones(xcb_randr_output_t mon, int16_t x, int16_t y)
{
	struct monitor *clonemon;
	struct list_item *item;

	item = mon_list;
	while (item != NULL && ((clonemon = item->data)->monitor == mon
						|| clonemon->x != x
						|| clonemon->y != y)) {
		item = item->next;
	}

	if (item == NULL)
		return NULL;
	else
		return clonemon;
}

/*
 * Add a monitor to the global monitor list.
 */

static struct monitor *
add_monitor(xcb_randr_output_t mon, char *name, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
	struct list_item *item;
	struct monitor *monitor = malloc(sizeof(struct monitor));

	if (monitor == NULL)
		return NULL;

	item = list_add_item(&mon_list);
	if (item == NULL) {
		free(monitor);
		return NULL;
	}

	item->data = monitor;
	monitor->item = item;
	monitor->monitor = mon;
	monitor->name = name;
	monitor->x = x;
	monitor->y = y;
	monitor->width = width;
	monitor->height = height;

	return monitor;
}

/*
 * Free a monitor from the global monitor list.
 */

static void
free_monitor(struct monitor *mon)
{
	struct list_item *item = mon->item;

	free(mon);
	list_delete_item(&mon_list, item);
}

/*
 * Get information about a certain monitor situated in a window: coordinates and size.
 */

static void
get_monitor_size(struct client *client, int16_t *mon_x, int16_t *mon_y, uint16_t *mon_width, uint16_t *mon_height)
{
	if (client == NULL || client->monitor == NULL) {
		if (mon_x != NULL && mon_y != NULL)
			*mon_x = *mon_y = 0;
		if (mon_width != NULL)
			*mon_width = scr->width_in_pixels;
		if (mon_height != NULL)
			*mon_height = scr->height_in_pixels;
	} else {
		if (mon_x != NULL)
			*mon_x = client->monitor->x;
		if (mon_y != NULL)
			*mon_y = client->monitor->y;
		if (mon_width != NULL)
			*mon_width = client->monitor->width;
		if (mon_height != NULL)
			*mon_height = client->monitor->height;
	}
}

/*
 * Arrange clients on a monitor.
 */

static void
arrange_by_monitor(struct monitor *mon)
{
	struct client *client;
	struct list_item *item;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;

		if (client->monitor == mon)
			fit_on_screen(client);
	}
}

/*
 * Wait for events and handle them.
 */

static void
run(void)
{
	xcb_generic_event_t *ev;

	halt = false;
	exit_code = 0;
	while (!halt) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (ev) {
			DMSG("%d\n", ev->response_type & ~0x80);
			if (ev->response_type == randr_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
				get_randr();
				DMSG("Screen layout changed\n");
			}
			if (events[EVENT_MASK(ev->response_type)] != NULL)
				(events[EVENT_MASK(ev->response_type)])(ev);
			free(ev);
		}
	}
}

/*
 * Initialize a window for further work.
 */

static struct client *
setup_window(xcb_window_t win)
{
	uint32_t values[2];
	xcb_ewmh_get_atoms_reply_t win_type;
	xcb_atom_t atom;
	struct client *client;
	struct list_item *item;
	xcb_size_hints_t hints;

	if (xcb_ewmh_get_wm_window_type_reply(ewmh,
				xcb_ewmh_get_wm_window_type(ewmh, win),
				&win_type, NULL) == 1) {
		unsigned int i = 0;
		/* if the window is a toolbar or a dock, map it and ignore it */
		while (i < win_type.atoms_len &&
			(atom = win_type.atoms[i]) != ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR
			&& atom != ewmh->_NET_WM_WINDOW_TYPE_DOCK
			&& atom != ewmh->_NET_WM_WINDOW_TYPE_DESKTOP)
			i++;

		if (i < win_type.atoms_len) {
			xcb_ewmh_get_atoms_reply_wipe(&win_type);
			xcb_map_window(conn, win);
			return NULL;
		}
	}

	/* subscribe to events */
	values[0] = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE;
	xcb_change_window_attributes(conn, win, XCB_CW_EVENT_MASK, values);

	/* in case of fire */
	xcb_change_save_set(conn, XCB_SET_MODE_INSERT, win);

	/* assign to the null group */
	xcb_ewmh_set_wm_desktop(ewmh, win, NULL_GROUP);

	item = list_add_item(&win_list);
	if (item == NULL)
		return NULL;

	client = malloc(sizeof(struct client));
	if (client == NULL)
		return NULL;

	/* initialize variables */
	item->data = client;
	client->item = item;
	client->window = win;
	client->geom.x = client->geom.y = client->geom.width
				   = client->geom.height
				   = client->min_width = client->min_height;
	client->maxed  = client->hmaxed = client->vmaxed
		= client->monocled = client->geom.set_by_user = false;
	client->monitor = NULL;
	client->mapped  = false;
	client->group   = NULL_GROUP;
	get_geometry(&client->window, &client->geom.x, &client->geom.y,
			&client->geom.width, &client->geom.height);

	xcb_icccm_get_wm_normal_hints_reply(conn,
			xcb_icccm_get_wm_normal_hints_unchecked(conn, win),
			&hints, NULL);

	if (hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
		client->geom.set_by_user = true;

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		client->min_width = hints.min_width;
		client->min_height = hints.min_height;
	}

	return client;
}

/*
 * Set focus state to active or inactive without raising the window.
 */

static void
set_focused_no_raise(struct client *client)
{
	long data[] = {
		XCB_ICCCM_WM_STATE_NORMAL,
		XCB_NONE,
	};
	if (client == NULL)
		return;

	if (!client->maxed)
		set_borders(client, conf.focus_color);

	/* revert state to normal */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
			ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

	/* focus the window */
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
			client->window, XCB_CURRENT_TIME);

	/* set ewmh property */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
			ewmh->_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &client->window);

	/* set the focus state to inactive on the previously focused window */
	if (client != focused_win) {
		if (focused_win != NULL && !focused_win->maxed)
			set_borders(focused_win, conf.unfocus_color);
		focused_win = client;
	}
}

/*
 * Focus and raise.
 */

static void
set_focused(struct client *client)
{
	set_focused_no_raise(client);
	raise_window(client->window);
}

/*
 * Put window at the top of the window stack.
 */

static void
raise_window(xcb_window_t win)
{
	uint32_t values[1] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

/*
 * Ask window to close gracefully. If the window doesn't respond, kill it.
 */

static void
close_window(struct client *client)
{
	if (client == NULL)
		return;

	xcb_window_t win = client->window;
	xcb_get_property_cookie_t cookie =
		xcb_icccm_get_wm_protocols_unchecked(conn,
				win, ewmh->WM_PROTOCOLS);
	xcb_icccm_get_wm_protocols_reply_t reply;
	unsigned int i = 0;
	bool got = false;

	if (xcb_icccm_get_wm_protocols_reply(conn, cookie, &reply, NULL)) {
		for (i = 0; i < reply.atoms_len; i++) {
			got = (reply.atoms[i] = ATOMS[WM_DELETE_WINDOW]);
			if (got)
				break;
		}

		xcb_icccm_get_wm_protocols_reply_wipe(&reply);
	}

	if (got)
		delete_window(win);
	else
		xcb_kill_client(conn, win);
}

/*
 * Gracefully ask a window to close.
 */

static void
delete_window(xcb_window_t win)
{
	xcb_client_message_event_t ev;

	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.sequence = 0;
	ev.format = 32;
	ev.window = win;
	ev.type = ewmh->WM_PROTOCOLS;
	ev.data.data32[0] = ATOMS[WM_DELETE_WINDOW];
	ev.data.data32[1] = XCB_CURRENT_TIME;

	xcb_send_event(conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (char *)&ev);
}

/*
 * Teleports window absolutely to the given coordinates.
 */

static void
teleport_window(xcb_window_t win, int16_t x, int16_t y)
{
	uint32_t values[2] = {x, y};

	if (win == scr->root || win == 0)
		return;

	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);

	xcb_flush(conn);
}

/*
 * Moves the window by a certain amount.
 */

static void
move_window(xcb_window_t win, int16_t x, int16_t y)
{
	int16_t win_x, win_y;
	uint16_t win_w, win_h;

	if (!is_mapped(win) || win == scr->root)
		return;

	get_geometry(&win, &win_x, &win_y, &win_w, &win_h);

	win_x += x;
	win_y += y;

	teleport_window(win, win_x, win_y);
}

/*
 * Resizes window to the given size.
 */

static void
resize_window_absolute(xcb_window_t win, uint16_t w, uint16_t h)
{
	uint32_t val[2];
	uint32_t mask = XCB_CONFIG_WINDOW_WIDTH
				  | XCB_CONFIG_WINDOW_HEIGHT;

	val[0] = w;
	val[1] = h;

	xcb_configure_window(conn, win, mask, val);
}

/*
 * Resizes window by a certain amount.
 */

static void
resize_window(xcb_window_t win, int16_t w, int16_t h)
{
	uint16_t win_w, win_h;

	get_geometry(&win, NULL, NULL, &win_w, &win_h);
	resize_window_absolute(win, win_w + w, win_h + h);
}

/*
 * Fit window on screen if too big.
 */

static void
fit_on_screen(struct client *client)
{
	int16_t mon_x, mon_y;
	uint16_t mon_width, mon_height;
	bool will_resize, will_move;

	will_resize = will_move = false;
	client->hmaxed = client->vmaxed = false;
	get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height);
	if (client->maxed) {
		client->maxed = false;
	} else if (client->geom.width == mon_width && client->geom.height == mon_height) {
		client->geom.x = mon_x;
		client->geom.y = mon_y;
		client->geom.width -= 2 * conf.border_width;
		client->geom.height -= 2 * conf.border_width;
		maximize_window(client, mon_x, mon_y, mon_width, mon_height);
		return;
	}

	/* Is it outside the display? */
	if (client->geom.x > mon_x + mon_width || client->geom.y > mon_y + mon_height
			|| client->geom.x < mon_x || client->geom.y < mon_y) {
		will_move = true;
		if (client->geom.x > mon_x + mon_width)
			client->geom.x = mon_x + mon_width - client->geom.width - 2 * conf.border_width;
		else if (client->geom.x < mon_x)
			client->geom.x = mon_x;
		if (client->geom.y > mon_y + mon_height)
			client->geom.y = mon_y + mon_height - client->geom.height - 2 * conf.border_width;
		else if (client->geom.y < mon_y)
			client->geom.y = mon_y;
	}

	/* Is it smaller than it wants to be? */
	if (client->min_width != 0 && client->geom.width < client->min_width) {
		client->geom.width = client->min_width;
		will_resize = true;
	}
	if (client->min_height != 0 && client->geom.height < client->min_height) {
		client->geom.height = client->min_height;

		will_resize = true;
	}

	/* If the window is larger than the screen or is a bit in the outside,
	 * move it to the corner and resize it accordingly. */
	if (client->geom.width + 2 * conf.border_width > mon_width) {
		client->geom.x = mon_x;
		client->geom.width = mon_width - 2 * conf.border_width;
		will_move = will_resize = true;
	} else if (client->geom.x + client->geom.width + 2 * conf.border_width
			> mon_x + mon_width) {
		client->geom.x = mon_x + mon_width - client->geom.width - 2 * conf.border_width;
		will_move = true;
	}

	if (client->geom.height + 2 * conf.border_width > mon_height) {
		client->geom.y = mon_y;
		client->geom.height = mon_height - 2 * conf.border_width;
		will_move = will_resize = true;
	} else if (client->geom.y + client->geom.height + 2 * conf.border_width
			> mon_y + mon_height) {
		client->geom.y = mon_y + mon_height - client->geom.height - 2 * conf.border_width;
		will_move = true;
	}

	if (will_move)
		teleport_window(client->window, client->geom.x, client->geom.y);
	if (will_resize)
		resize_window_absolute(client->window, client->geom.width, client->geom.height);
}

static void
maximize_window(struct client *client, int16_t mon_x, int16_t mon_y, uint16_t mon_width, uint16_t mon_height)
{
	uint32_t values[1];
	xcb_atom_t state[] = {
		ewmh->_NET_WM_STATE_FULLSCREEN,
		XCB_NONE
	};

	if (client == NULL)
		return;

	if (client->vmaxed || client->hmaxed || client->monocled)
		unmaximize_window(client);

	/* maximized windows don't have borders */
	values[0] = 0;
	if (client->geom.width != mon_width || client->geom.height != mon_height)
		save_original_size(client);
	xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
			values);

	client->geom.x = mon_x;
	client->geom.y = mon_y;
	client->geom.width = mon_width;
	client->geom.height = mon_height;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->maxed = true;
	xcb_ewmh_set_wm_state(ewmh, client->window, 2, state);
	set_focused_no_raise(client);
}

static void
hmaximize_window(struct client *client, int16_t mon_x, uint16_t mon_width)
{
	if (client == NULL)
		return;

	xcb_atom_t state[] = {
		ewmh->_NET_WM_STATE_MAXIMIZED_HORZ,
		XCB_NONE
	};

	if (client->maxed || client->vmaxed || client->monocled)
		unmaximize_window(client);

	if (client->geom.width != mon_width)
		save_original_size(client);
	client->geom.x = mon_x + conf.gap_left;
	client->geom.width = mon_width - conf.gap_left - conf.gap_right - 2 * conf.border_width;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->hmaxed = true;

	xcb_ewmh_set_wm_state(ewmh, client->window, 2, state);
}

static void
vmaximize_window(struct client *client, int16_t mon_y, uint16_t mon_height)
{
	if (client == NULL)
		return;

	xcb_atom_t state[] = {
		ewmh->_NET_WM_STATE_MAXIMIZED_VERT,
		XCB_NONE
	};

	if (client->maxed || client->hmaxed || client->monocled)
		unmaximize_window(client);

	if (client->geom.height != mon_height)
		save_original_size(client);

	client->geom.y = mon_y + conf.gap_up;
	client->geom.height = mon_height - conf.gap_up - conf.gap_down - 2 * conf.border_width;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->vmaxed = true;
	xcb_ewmh_set_wm_state(ewmh, client->window, 2, state);
}

static void
monocle_window(struct client *client, int16_t mon_x, int16_t mon_y, uint16_t mon_width, uint16_t mon_height)
{
	if (client == NULL)
		return;

	if (client->maxed || client->vmaxed || client->monocled)
		unmaximize_window(client);

	save_original_size(client);

	client->geom.x = mon_x + conf.gap_left;
	client->geom.y = mon_y + conf.gap_up;
	client->geom.width = mon_width - 2 * conf.border_width
		- conf.gap_left - conf.gap_right;
	client->geom.height = mon_height - 2 * conf.border_width
		- conf.gap_up - conf.gap_down;
	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->monocled = true;
	set_focused_no_raise(client);
}

static void
unmaximize_window(struct client *client)
{
	xcb_atom_t state[] = {
		XCB_ICCCM_WM_STATE_NORMAL,
		XCB_NONE
	};
	client->geom.x = client->orig_geom.x;
	client->geom.y = client->orig_geom.y;
	client->geom.width = client->orig_geom.width;
	client->geom.height = client->orig_geom.height;
	client->maxed = client->maxed = client->hmaxed
		= client->vmaxed = client->monocled = false;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	xcb_ewmh_set_wm_state(ewmh, client->window, 2, state);
	set_borders(client, conf.unfocus_color);
}

static void
cycle_window(struct client *client)
{
	struct list_item *item;
	struct client *data;

	item = win_list;
	if (client != NULL)
			while (item != NULL && item->data != client)
				item = item->next;

	/* if item is not found item will be null
	 * and we'll get a nice segmentation fault. may the debugger be with you */
	if (item != NULL)
		do {
			item = item->next;
			if (item == NULL)
				item = win_list;
			data = item->data;
		} while (!data->mapped);

	if (item != NULL && item->data != client)
		set_focused(item->data);
}

static void
rcycle_window(struct client *client)
{
	struct list_item *item = NULL;
	struct list_item *last_item;
	struct list_item *client_item;
	struct client *data;

	if (win_list == NULL)
		return;

	/* find last window */
	item = win_list;
	while (item != NULL) {
		last_item = item;
		item = item->next;
	}

	/* find item of client */
	item = win_list;
	while (item != NULL && item->data != client)
		item = item->next;

	if (item == NULL)
		item = last_item;

	client_item = item;

	item = client_item;
	do {
		item = item->prev;
		if (item == NULL)
			item = last_item;
		data = item->data;
	} while (!data->mapped);

	if (item != NULL && item->data != client)
		set_focused(item->data);
}

static void
cycle_window_in_group(struct client *client)
{
	struct list_item *item;
	struct client *data;

	if (client == NULL)
		return;

	item = win_list;
	while (item != NULL && item->data != client)
		item = item->next;
	if (item != NULL)
		do {
			item = item->next;
			if (item == NULL)
				item = win_list;
			data = item->data;
		} while (!data->mapped || data->group != client->group);

	if (item != NULL && data != client && data->group == client->group)
		set_focused(item->data);
}

static void
rcycle_window_in_group(struct client *client)
{
	struct list_item *item = NULL;
	struct list_item *last_item;
	struct list_item *client_item;
	struct client *data;

	if (win_list == NULL || client == NULL)
		return;

	/* find item of client */
	item = win_list;
	while (item != NULL && item->data != client)
		item = item->next;

	if (item == NULL)
		return;

	client_item = item;

	/* find last window */
	item = win_list;
	while (item != NULL) {
		last_item = item;
		item = item->next;
	}

	item = client_item;
	do {
		item = item->prev;
		if (item == NULL)
			item = last_item;
		data = item->data;
	} while (!data->mapped || data->group != client->group);

	if (item != NULL && data != client && data->group == client->group)
		set_focused(item->data);
}

static void
save_original_size(struct client *client)
{
	client->orig_geom.x = client->geom.x;
	client->orig_geom.y = client->geom.y;
	client->orig_geom.width = client->geom.width;
	client->orig_geom.height = client->geom.height;
}

/*
 * Get atom by name.
 */

static xcb_atom_t
get_atom(char *name)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);

	if (!reply)
		return XCB_ATOM_STRING;

	return reply->atom;
}

/*
 * Get the mouse pointer's coordinates.
 */

static bool
get_pointer_location(xcb_window_t *win, int16_t *x, int16_t *y)
{
	xcb_query_pointer_reply_t *pointer;

	pointer = xcb_query_pointer_reply(conn,
			xcb_query_pointer(conn, *win), 0);

	*x = pointer->win_x;
	*y = pointer->win_y;

	free(pointer);

	return pointer != NULL;
}

static void
center_pointer(struct client *client)
{
	int16_t cur_x, cur_y;

	cur_x = cur_y = 0;

	switch (conf.cursor_position) {
	case TOP_LEFT:
		cur_x = -conf.border_width;
		cur_y = -conf.border_width;
		break;
	case TOP_RIGHT:
		cur_x = client->geom.width + conf.border_width;
		cur_y = 0 - conf.border_width;
		break;
	case BOTTOM_LEFT:
		cur_x = 0 - conf.border_width;
		cur_y = client->geom.height + conf.border_width;
		break;
	case BOTTOM_RIGHT:
		cur_x = client->geom.width + conf.border_width;
		cur_y = client->geom.height + conf.border_width;
		break;
	case CENTER:
		cur_x = client->geom.width / 2;
		cur_y = client->geom.height / 2;
		break;
	default: break;
	}

	xcb_warp_pointer(conn, XCB_NONE, client->window, 0, 0, 0, 0, cur_x, cur_y);
	xcb_flush(conn);
}

/*
 * Get the client instance with a given window id.
 */

static struct client*
find_client(xcb_window_t *win)
{
	struct list_item *item;

	item = win_list;
	while (item != NULL && ((struct client *)item->data)->window != *win)
		item = item ->next;

	if (item == NULL)
		return NULL;
	else
		return item->data;
}

/*
 * Get a window's geometry.
 */

static bool
get_geometry(xcb_window_t *win, int16_t *x, int16_t *y, uint16_t *width, uint16_t *height)
{
	xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, *win), NULL);

	if (reply == NULL)
		return false;
	if (x != NULL)
		*x = reply->x;
	if (y != NULL)
		*y = reply->y;
	if (width != NULL)
		*width = reply->width;
	if (height != NULL)
		*height = reply->height;

	free(reply);
	return true;
}

/*
 * Set the color of the border.
 */

static void
set_borders(struct client *client, uint32_t color)
{
	if (client == NULL)
		return;
	uint32_t values[1];
	values[0] = conf.border_width;
	xcb_configure_window(conn, client->window,
			XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
	values[0] = color;
	xcb_change_window_attributes(conn, client->window, XCB_CW_BORDER_PIXEL, values);
}

/*
 * Returns true if window is mapped.
 */

static bool
is_mapped(xcb_window_t win)
{
	bool yes;
	xcb_get_window_attributes_reply_t *r =
		xcb_get_window_attributes_reply(conn,
				xcb_get_window_attributes(conn, win),
				NULL);
	if (r == NULL)
		return false;

	yes = r->map_state == XCB_MAP_STATE_VIEWABLE;
	free(r);

	return yes;
}

/*
 * Deletes and frees a client from the list.
 */

static void
free_window(struct client *client)
{
	struct list_item *item;

	DMSG("freeing 0x%08x\n", client->window);
	item = client->item;

	free(client);
	list_delete_item(&win_list, item);
}

/*
 * Add window to the ewmh client list.
 */

static void
add_to_client_list(xcb_window_t win)
{
	xcb_change_property(conn, XCB_PROP_MODE_APPEND, scr->root,
			ewmh->_NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, 1, &win);
	xcb_change_property(conn, XCB_PROP_MODE_APPEND, scr->root, ewmh->_NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, 32, 1, &win);
}

/*
 * Adds all windows to the ewmh client list.
 */

static void
update_client_list(void)
{
	xcb_window_t *children;
	struct client *client;
	uint32_t len;

	xcb_query_tree_reply_t *reply = xcb_query_tree_reply(conn,
			xcb_query_tree(conn, scr->root), NULL);
	xcb_delete_property(conn, scr->root, ewmh->_NET_CLIENT_LIST);
	xcb_delete_property(conn, scr->root, ewmh->_NET_CLIENT_LIST_STACKING);

	if (reply == NULL) {
		add_to_client_list(0);
		return;
	}

	len = xcb_query_tree_children_length(reply);
	children = xcb_query_tree_children(reply);

	for (unsigned int i = 0; i < len; i++) {
		client = find_client(&children[i]);
		if (client != NULL)
			add_to_client_list(client->window);
	}

	free(reply);
}

static void
update_wm_desktop(struct client *client)
{
	if (client != NULL)
		xcb_ewmh_set_wm_desktop(ewmh, client->window, client->group);
}

static void
group_add_window(struct client *client, uint32_t group)
{
	if (client != NULL && group < conf.groups) {
		client->group = group;
		update_wm_desktop(client);
		group_in_use[group] = true;
	}
}

static void
group_remove_window(struct client *client)
{
	if (client != NULL) {
		client->group = NULL_GROUP;
		update_wm_desktop(client);
	}
}

static void
group_activate(uint32_t group) {
	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		if (client->group == group) {
			xcb_map_window(conn, client->window);
			set_focused(client);
		}
	}
	group_in_use[group] = true;
}

static void
group_deactivate(uint32_t group)
{
	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		if (client->group == group)
			xcb_unmap_window(conn, client->window);
	}
	group_in_use[group] = false;
}

static void
group_toggle(uint32_t group)
{
	if (group_in_use[group])
		group_deactivate(group);
	else
		group_activate(group);
}

static void
change_nr_of_groups(uint32_t groups)
{
	bool *copy = malloc(groups * sizeof(bool));
	uint32_t until = groups < conf.groups ? groups : conf.groups;
	struct list_item *item;
	struct client *client;

	for (uint32_t i = 0; i < until; i++)
		copy[i] = group_in_use[i];

	if (groups < conf.groups)
		for (item = win_list; item != NULL; item = item->next) {
			client = item->data;
			if (client->group != NULL_GROUP && client->group >= groups) {
				group_activate(client->group);
				client->group = NULL_GROUP;
				update_wm_desktop(client);
			}
		}

	conf.groups = groups;
	free(group_in_use);
	group_in_use = copy;
}

static void
mouse_start(enum mouse_mode mode)
{
	hovering_mouse = true;
	xcb_query_pointer_reply_t *ptr;
	ptr = xcb_query_pointer_reply(conn,
			xcb_query_pointer(conn, scr->root), NULL);
	hovered_client = find_client(&ptr->child);
	if (hovered_client != NULL && ptr != NULL) {
		current_mouse_mode = mode;
		xcb_grab_pointer(conn, false, scr->root,
				XCB_EVENT_MASK_BUTTON_RELEASE
				| XCB_EVENT_MASK_BUTTON_MOTION
				| XCB_EVENT_MASK_POINTER_MOTION_HINT,
				XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
	}
}

static void
mouse_stop(void)
{
	hovering_mouse = false;
	current_mouse_mode = MOUSE_NONE;
	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
	hovered_client = NULL;
}

static void
mouse_toggle(enum mouse_mode mode)
{
	if (hovering_mouse)
		mouse_stop();
	else
		mouse_start(mode);
}

/*
 * Adds X event handlers to the array.
 */

static void
register_event_handlers(void)
{
	for (int i = 0; i <= LAST_XCB_EVENT; i++)
		events[i] = NULL;

	events[XCB_CONFIGURE_REQUEST] = event_configure_request;
	events[XCB_DESTROY_NOTIFY]    = event_destroy_notify;
	events[XCB_ENTER_NOTIFY]      = event_enter_notify;
	events[XCB_MAP_REQUEST]       = event_map_request;
	events[XCB_MAP_NOTIFY]        = event_map_notify;
	events[XCB_UNMAP_NOTIFY]      = event_unmap_notify;
	events[XCB_CLIENT_MESSAGE]    = event_client_message;
	events[XCB_CONFIGURE_NOTIFY]  = event_configure_notify;
	events[XCB_CIRCULATE_REQUEST] = event_circulate_request;
	events[XCB_FOCUS_OUT]         = event_focus_out;
	events[XCB_MOTION_NOTIFY]     = event_motion_notify;
}

/*
 * A window wants to be configured.
 */

static void
event_configure_request(xcb_generic_event_t *ev)
{
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;
	struct client *client;
	uint32_t values[7];
	int i = 0;

	client = find_client(&e->window);
	if (client != NULL) {

		if (e->value_mask & XCB_CONFIG_WINDOW_X
				&& !client->maxed && !client->monocled && !client->hmaxed)
			client->geom.x = e->x;

		if (e->value_mask & XCB_CONFIG_WINDOW_Y
				&& !client->maxed && !client->monocled && !client->vmaxed)
			client->geom.y = e->y;

		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH
				&& !client->maxed && !client->monocled && !client->hmaxed)
			client->geom.width= e->width;

		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT
				&& !client->maxed && !client->monocled && !client->vmaxed)
			client->geom.height = e->height;

		if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
			values[0] = e->stack_mode;
			xcb_configure_window(conn, e->window,
					XCB_CONFIG_WINDOW_STACK_MODE, values);
		}

		if (!client->maxed) {
			fit_on_screen(client);
		}

		teleport_window(client->window, client->geom.x, client->geom.y);
		resize_window_absolute(client->window, client->geom.width, client->geom.height);
		if (!client->maxed)
			set_borders(client, conf.focus_color);
	} else {
		if (e->value_mask & XCB_CONFIG_WINDOW_X) {
			values[i] = e->x;
			i++;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
			values[i] = e->y;
			i++;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
			values[i] = e->width;
			i++;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
			values[i] = e->height;
			i++;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
			values[i] = e->sibling;
			i++;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
			values[i] = e->stack_mode;
			i++;
		}

		if (i == 0)
			return;
		xcb_configure_window(conn, e->window, e->value_mask, values);
	}
}

/*
 * Window has been destroyed.
 */

static void
event_destroy_notify(xcb_generic_event_t *ev)
{
	struct client *client;
	xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

	client = find_client(&e->window);
	if (focused_win != NULL && focused_win == client)
		focused_win = NULL;

	if (client != NULL) {
		free_window(client);
	}

	update_client_list();
}

/*
 * The mouse pointer has entered the window.
 */

static void
event_enter_notify(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
	struct client *client;

	if (conf.sloppy_focus == false)
		return;

	if (focused_win != NULL && e->event == focused_win->window)
		return;

	client = find_client(&e->event);

	if (client != NULL)
		set_focused_no_raise(client);
}

/*
 * A window wants to show up on the screen.
 */

static void
event_map_request(xcb_generic_event_t *ev)
{
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
	struct client *client;
	long data[] = {
		XCB_ICCCM_WM_STATE_NORMAL,
		XCB_NONE,
	};

	/* window wants to magically show up. we prohibit that */
	if (find_client(&e->window) != NULL)
		return;

	xcb_map_window(conn, e->window);
	client = setup_window(e->window);

	if (client == NULL)
		return;
	if (!client->geom.set_by_user) {
		if (!get_pointer_location(&scr->root, &client->geom.x, &client->geom.y))
			client->geom.x = client->geom.y = 0;

		client->geom.x -= client->geom.width / 2;
		client->geom.y -= client->geom.height / 2;
		teleport_window(client->window, client->geom.x, client->geom.y);
	}

	if (randr_base != -1) {
		client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
		if (client->monitor == NULL && mon_list != NULL)
			client->monitor = mon_list->data;
	}

	fit_on_screen(client);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
			ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

	center_pointer(client);
	update_client_list();

	if (!client->maxed)
		set_borders(client, conf.focus_color);
}

static void
event_map_notify(xcb_generic_event_t *ev)
{
	xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
	struct client *client = find_client(&e->window);

	if (client != NULL) {
		client->mapped = true;
		set_focused(client);
	}
}

/*
 * Window has been unmapped (became invisible).
 */

static void
event_unmap_notify(xcb_generic_event_t *ev)
{
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
	struct client *client = NULL;

	client = find_client(&e->window);
	if (client == NULL)
		return;

	if (focused_win != NULL && client->window == focused_win->window)
		focused_win = NULL;

	client->mapped = false;

	update_client_list();
}

/*
 * Window has been configured.
 */

static void
event_configure_notify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;
	struct client *client;
	struct list_item *item;

	/* The root window changes its geometry when the
	 * user adds/removes/tilts screens */
	if (e->window == scr->root) {
		if (e->window != scr->width_in_pixels
				|| e->height != scr->height_in_pixels) {
			scr->width_in_pixels = e->width;
			scr->height_in_pixels = e->height;

			if (randr_base != -1) {
				get_randr();
				for (item = win_list; item != NULL; item = item->next) {
					client = item->data;
					fit_on_screen(client);
				}
			}
		}
	} else {
		client = find_client(&e->window);
		if (client != NULL)
			client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
	}
}

/*
 * Window wants to change its position in the stacking order.
 */

static void
event_circulate_request(xcb_generic_event_t *ev)
{
	xcb_circulate_request_event_t *e = (xcb_circulate_request_event_t *)ev;

	xcb_circulate_window(conn, e->window, e->place);
}

/*
 * Received client message. Either ewmh/icccm thing or
 * message from the client.
 */

static void
event_client_message(xcb_generic_event_t *ev)
{
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
	uint32_t ipc_command;
	uint32_t *data;
	bool maxed, vmaxed, hmaxed;
	struct client *client;
	int16_t mon_x, mon_y;
	uint16_t mon_w, mon_h;
	xcb_atom_t action;

	if (e->type == ATOMS[_IPC_ATOM_COMMAND] && e->format == 32) {
		/* Message from the client */
		data = e->data.data32;
		ipc_command = data[0];
		if (ipc_handlers[ipc_command] != NULL)
			(ipc_handlers[ipc_command])(data + 1);
		DMSG("%u %u %u %u %u\n", data[0], data[1], data[2], data[3], data[4]);
	} else if (e->type == ewmh->_NET_WM_STATE && e->format == 32) {
		/* A window changed its state */
		client = find_client(&e->window);
		if (client == NULL)
			return;
		action = e->data.data32[0];
		maxed = client->maxed;
		vmaxed = client->vmaxed;
		hmaxed = client->hmaxed;
		get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

		/* We handle only two states at the same time */
		for (int i = 0; i < 2; i++) {
			xcb_atom_t state = e->data.data32[i + 1];
			bool *var = NULL, *orig = NULL;
			if (state == ewmh->_NET_WM_STATE_FULLSCREEN) {
				var = &maxed;
				orig = &client->maxed;
			} else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_VERT) {
				var = &vmaxed;
				orig = &client->vmaxed;
			} else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_HORZ) {
				var = &hmaxed;
				orig = &client->hmaxed;
			}
			if (var != NULL) {
				if (action == XCB_EWMH_WM_STATE_REMOVE)
					*var = false;
				else if (action == XCB_EWMH_WM_STATE_ADD)
					*var = true;
				else if (action == XCB_EWMH_WM_STATE_TOGGLE)
					*var = !(*var);

				/* if the state changed, update */
				if (*var != *orig) {
					/* state added */
					if (*var) {
						/* reset */
						unmaximize_window(client);
						if (var == &maxed)
							maximize_window(client, mon_x, mon_y, mon_w, mon_h);
						else if (var == &vmaxed)
							vmaximize_window(client, mon_y, mon_h);
						else if (var == &hmaxed)
							hmaximize_window(client, mon_x, mon_w);
						xcb_atom_t data[] = {
							state,
							XCB_NONE
						};
						DMSG("wm state set to %d\n", data[0]);
						xcb_ewmh_set_wm_state(ewmh, client->window, 2, data);
					} else {
						unmaximize_window(client);
						set_focused(client);
					}
				}
			}
		}
	}
}

static void
event_focus_out(xcb_generic_event_t *ev)
{
	(void)(ev);
	xcb_get_input_focus_reply_t *focus = xcb_get_input_focus_reply(conn,
			xcb_get_input_focus(conn), NULL);
	struct client *client = NULL;

	if (focused_win != NULL && focus->focus == focused_win->window)
		return;

	if (focus->focus == scr->root) {
		focused_win = NULL;
	} else {
		client = find_client(&focus->focus);
		if (client != NULL)
			set_focused_no_raise(client);
	}
}

static void
event_motion_notify(xcb_generic_event_t *ev)
{
	int16_t to_x, to_y;

	(void)(ev);
	xcb_query_pointer_reply_t *ptr;
	ptr = xcb_query_pointer_reply(conn,
			xcb_query_pointer(conn, scr->root), NULL);

	if (hovered_client == NULL || hovered_client->maxed || ptr == NULL)
		return;

	switch (current_mouse_mode) {
		case MOUSE_MOVE:
			if (ptr->root_x + hovered_client->geom.width / 2
					> scr->width_in_pixels - 2 * conf.border_width)
				to_x = scr->width_in_pixels - hovered_client->geom.width
					- 2 * conf.border_width;
			else
				to_x = ptr->root_x - hovered_client->geom.width / 2;

			if (ptr->root_y + hovered_client->geom.height / 2
					> scr->height_in_pixels - 2 * conf.border_width)
				to_y = scr->height_in_pixels - hovered_client->geom.height
					- 2 * conf.border_width;
			else
				to_y = ptr->root_y - hovered_client->geom.height / 2;

			if (ptr->root_x < hovered_client->geom.width / 2)
				to_x = 0;
			if (ptr->root_y < hovered_client->geom.height / 2)
				to_y = 0;

			teleport_window(hovered_client->window, to_x, to_y);
			hovered_client->geom.x = to_x;
			hovered_client->geom.y = to_y;
			break;

		case MOUSE_RESIZE:
			resize_window_absolute(hovered_client->window,
					ptr->root_x - hovered_client->geom.x,
					ptr->root_y - hovered_client->geom.y);
		default:
			break;
	}
}

/*
 * Populates array with functions for handling IPC commands.
 */

static void
register_ipc_handlers(void)
{
	ipc_handlers[IPCWindowMove]            = ipc_window_move;
	ipc_handlers[IPCWindowMoveAbsolute]    = ipc_window_move_absolute;
	ipc_handlers[IPCWindowResize]          = ipc_window_resize;
	ipc_handlers[IPCWindowResizeAbsolute]  = ipc_window_resize_absolute;
	ipc_handlers[IPCWindowMaximize]        = ipc_window_maximize;
	ipc_handlers[IPCWindowHorMaximize]     = ipc_window_hor_maximize;
	ipc_handlers[IPCWindowVerMaximize]     = ipc_window_ver_maximize;
	ipc_handlers[IPCWindowMonocle]         = ipc_window_monocle;
	ipc_handlers[IPCWindowClose]           = ipc_window_close;
	ipc_handlers[IPCWindowPutInGrid]       = ipc_window_put_in_grid;
	ipc_handlers[IPCWindowSnap]            = ipc_window_snap;
	ipc_handlers[IPCWindowCycle]           = ipc_window_cycle;
	ipc_handlers[IPCWindowRevCycle]        = ipc_window_rev_cycle;
	ipc_handlers[IPCWindowCycleInGroup]    = ipc_window_cycle_in_group;
	ipc_handlers[IPCWindowRevCycleInGroup] = ipc_window_rev_cycle_in_group;
	ipc_handlers[IPCWindowFocus]           = ipc_window_focus;
	ipc_handlers[IPCGroupAddWindow]        = ipc_group_add_window;
	ipc_handlers[IPCGroupRemoveWindow]     = ipc_group_remove_window;
	ipc_handlers[IPCGroupActivate]         = ipc_group_activate;
	ipc_handlers[IPCGroupDeactivate]       = ipc_group_deactivate;
	ipc_handlers[IPCGroupToggle]           = ipc_group_toggle;
	ipc_handlers[IPCMouseStart]            = ipc_mouse_start;
	ipc_handlers[IPCMouseStop]             = ipc_mouse_stop;
	ipc_handlers[IPCMouseToggle]           = ipc_mouse_toggle;
	ipc_handlers[IPCWMQuit]                = ipc_wm_quit;
	ipc_handlers[IPCWMConfig]              = ipc_wm_config;
}

static void
ipc_window_move(uint32_t *d)
{
	int16_t x, y;

	if (focused_win == NULL)
		return;

	x = d[2];
	y = d[3];
if (d[0])
		x = -x;
	if (d[1])
		y = -y;

	focused_win->geom.x += x;
	focused_win->geom.y += y;

	move_window(focused_win->window, x, y);
	center_pointer(focused_win);
}

static void
ipc_window_move_absolute(uint32_t *d)
{
	int16_t x, y;

	if (focused_win == NULL)
		return;

	x = d[2];
	y = d[3];

	if (d[0])
		x = -x;
	if (d[1])
		y = -y;

	focused_win->geom.x = x;
	focused_win->geom.y = y;

	teleport_window(focused_win->window, x, y);
	center_pointer(focused_win);
}

static void
ipc_window_resize(uint32_t *d)
{
	int16_t w, h;
	int32_t aw, ah;

	if (focused_win == NULL)
		return;

	w = d[2];
	h = d[3];

	if (d[0])
		w = -w;
	if (d[1])
		h = -h;

	aw = focused_win->geom.width;
	ah = focused_win->geom.height;
	if (aw + w > 0)
		aw += w;
	if (ah + h > 0)
		ah += h;

	if (aw < 0)
		aw = 0;
	if (ah < 0)
		ah = 0;
	DMSG("aw: %d\tah: %d\n", aw, ah);

	if (focused_win->min_width != 0 && aw < focused_win->min_width)
		aw = focused_win->min_width;

	if (focused_win->min_height != 0 && ah < focused_win->min_height)
		ah = focused_win->min_height;

	focused_win->geom.width  = aw;
	focused_win->geom.height = ah;

	resize_window_absolute(focused_win->window, aw, ah);
	center_pointer(focused_win);
}

static void
ipc_window_resize_absolute(uint32_t *d)
{
	int16_t w, h;

	if (focused_win == NULL)
		return;

	w = d[0];
	h = d[1];

	if (focused_win->min_width != 0 && w < focused_win->min_width)
		w = focused_win->min_width;

	if (focused_win->min_height != 0 && h < focused_win->min_height)
		h = focused_win->min_height;

	focused_win->geom.width = w;
	focused_win->geom.height = h;

	resize_window_absolute(focused_win->window, w, h);
	center_pointer(focused_win);
}

static void
ipc_window_maximize(uint32_t *d)
{
	(void)(d);
	int16_t mon_x, mon_y;
	uint16_t mon_w, mon_h;

	if (focused_win == NULL)
		return;

	if (focused_win->maxed) {
		unmaximize_window(focused_win);
		set_focused(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);
		maximize_window(focused_win, mon_x, mon_y, mon_w, mon_h);
	}

	xcb_flush(conn);
}

static void
ipc_window_hor_maximize(uint32_t *d)
{
	(void)(d);
	int16_t mon_x, mon_y;
	uint16_t mon_w;

	if (focused_win == NULL)
		return;

	if (focused_win->hmaxed) {
		unmaximize_window(focused_win);
		set_focused(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, NULL);
		hmaximize_window(focused_win, mon_x, mon_w);
	}

	xcb_flush(conn);
}

static void
ipc_window_ver_maximize(uint32_t *d)
{
	(void)(d);
	int16_t mon_x, mon_y;
	uint16_t mon_h;

	if (focused_win == NULL)
		return;

	if (focused_win->vmaxed) {
		unmaximize_window(focused_win);
		set_focused(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, NULL, &mon_h);
		vmaximize_window(focused_win, mon_y, mon_h);
	}

	xcb_flush(conn);
}

static void
ipc_window_monocle(uint32_t *d)
{
	(void)(d);
	int16_t mon_x, mon_y;
	uint16_t mon_w, mon_h;

	if (focused_win == NULL)
		return;

	if (focused_win->monocled) {
		unmaximize_window(focused_win);
		set_focused(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);
		monocle_window(focused_win, mon_x, mon_y, mon_w, mon_h);
	}

	xcb_flush(conn);
}

static void
ipc_window_close(uint32_t *d)
{
	(void)(d);
	close_window(focused_win);
	focused_win = NULL;
}

static void
ipc_window_put_in_grid(uint32_t *d)
{
	uint32_t grid_width, grid_height;
	uint32_t grid_x, grid_y;
	int step_x, step_y;
	int16_t mon_x, mon_y;
	uint16_t mon_w, mon_h;

	grid_width  = d[0];
	grid_height = d[1];
	grid_x      = d[2];
	grid_y      = d[3];

	if (focused_win == NULL || grid_x >= grid_width || grid_y >= grid_height)
		return;

	if (focused_win->maxed || focused_win->vmaxed
			|| focused_win->hmaxed || focused_win->monocled) {
		unmaximize_window(focused_win);
		set_focused(focused_win);
	}

	get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);
	/* width and height of windows in the grid */
	step_x = (mon_w - (grid_width - 1) * conf.grid_gap
			- grid_width * 2 * conf.border_width - conf.gap_left - conf.gap_right) / grid_width;
	step_y = (mon_h - (grid_width - 1) * conf.grid_gap
			- grid_width * 2 * conf.border_width - conf.gap_up - conf.gap_down) / grid_height;
	DMSG("%d %d %d %d %d %d\n", grid_width, grid_height, grid_x, grid_y, step_x, step_y);

	focused_win->geom.width = step_x;
	focused_win->geom.height = step_y;

	focused_win->geom.x = mon_x + conf.gap_left
		+ grid_x * (conf.grid_gap + 2 * conf.border_width + step_x);
	focused_win->geom.y = mon_y + conf.gap_up
		+ grid_y * (conf.grid_gap + 2 * conf.border_width + step_y);

	teleport_window(focused_win->window, focused_win->geom.x, focused_win->geom.y);
	resize_window_absolute(focused_win->window, focused_win->geom.width, focused_win->geom.height);

	xcb_flush(conn);
}

static void
ipc_window_snap(uint32_t *d)
{
	uint32_t mode = d[0];
	int16_t mon_x, mon_y, win_x, win_y;
	uint16_t mon_w, mon_h, win_w, win_h;

	if (focused_win == NULL)
		return;

	fit_on_screen(focused_win);

	win_x = focused_win->geom.x;
	win_y = focused_win->geom.y;
	win_w = focused_win->geom.width + 2 * conf.border_width;
	win_h = focused_win->geom.height + 2 * conf.border_width;

	get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);

	switch (mode) {
		case TOP_LEFT:
			win_x = mon_x + conf.gap_left;
			win_y = mon_y + conf.gap_up;
			break;

		case TOP_RIGHT:
			win_x = mon_x + mon_w - conf.gap_right - win_w;
			win_y = mon_y + conf.gap_up;
			break;

		case BOTTOM_LEFT:
			win_x = mon_x + conf.gap_left;
			win_y = mon_y + mon_h - conf.gap_down - win_h;
			break;

		case BOTTOM_RIGHT:
			win_x = mon_x + mon_w - conf.gap_right - win_w;
			win_y = mon_y + mon_h - conf.gap_down - win_h;
			break;

		case CENTER:
			win_x = mon_x + (mon_w - win_w) / 2;
			win_y = mon_y + (mon_h - win_h) / 2;
			break;

		default:
			return;
	}

	focused_win->geom.x = win_x;
	focused_win->geom.y = win_y;
	teleport_window(focused_win->window, win_x, win_y);
	center_pointer(focused_win);
	xcb_flush(conn);
}

static
void ipc_window_cycle(uint32_t *d)
{
	(void)(d);

	cycle_window(focused_win);
}

static
void ipc_window_rev_cycle(uint32_t *d)
{
	(void)(d);

	rcycle_window(focused_win);
}

static void
ipc_window_cycle_in_group(uint32_t *d)
{
	(void)(d);

	if (focused_win == NULL)
		return;

	cycle_window_in_group(focused_win);
}
static void
ipc_window_rev_cycle_in_group(uint32_t *d)
{
	(void)(d);

	rcycle_window_in_group(focused_win);
}

static void
ipc_window_focus(uint32_t *d)
{
	struct client *client = find_client(&d[0]);

	if (client != NULL)
		set_focused(client);
}

static void
ipc_group_add_window(uint32_t *d)
{
	if (focused_win != NULL)
		group_add_window(focused_win, d[0] - 1);
}

static void
ipc_group_remove_window(uint32_t *d)
{
	(void)(d);
	if (focused_win != NULL)
		group_remove_window(focused_win);
}

static void
ipc_group_activate(uint32_t *d)
{
	group_activate(d[0] - 1);
}

static void
ipc_group_deactivate(uint32_t *d)
{
	group_activate(d[0] - 1);
}

static void
ipc_group_toggle(uint32_t *d)
{
	group_toggle(d[0] - 1);
}

static void
ipc_mouse_start(uint32_t *d)
{
	mouse_start(d[0]);
}

static void
ipc_mouse_stop(uint32_t *d)
{
	(void)(d);
	mouse_stop();
}

static void
ipc_mouse_toggle(uint32_t *d)
{
	mouse_toggle(d[0]);
}

static void
ipc_wm_quit(uint32_t *d)
{
	uint32_t code = d[0];
	halt = true;
	exit_code = code;
}

static void
ipc_wm_config(uint32_t *d)
{
	enum IPCConfig key;

	key = d[0];

	switch (key) {
		case IPCConfigBorderWidth:
			conf.border_width = d[1];
			break;
		case IPCConfigColorFocused:
			conf.focus_color = d[1];
			break;
		case IPCConfigColorUnfocused:
			conf.unfocus_color = d[1];
			break;
		case IPCConfigGapWidth:
			switch (d[1]) {
				case LEFT: conf.gap_left   = d[2]; break;
				case BOTTOM: conf.gap_down = d[2]; break;
				case TOP: conf.gap_up      = d[2]; break;
				case RIGHT: conf.gap_right = d[2]; break;
				case ALL: conf.gap_left = conf.gap_down
						  = conf.gap_up = conf.gap_right = d[2];
				default: break;
			}
			break;
		case IPCConfigGridGapWidth:
			conf.grid_gap = d[1];
		case IPCConfigCursorPosition:
			conf.cursor_position = d[1];
			break;
		case IPCConfigGroupsNr:
			change_nr_of_groups(d[1]);
			break;
		case IPCConfigEnableSloppyFocus:
			conf.sloppy_focus = d[1];
			break;
		default:
			break;
	}
}

static void
usage(char *name)
{
	fprintf(stderr, "Usage: %s [-h]\n", name);
	fprintf(stderr, "\n");
	fprintf(stderr, "%s %s\n", __NAME__, __THIS_VERSION__);

	exit(EXIT_FAILURE);
}

static void
load_defaults(void)
{
	conf.border_width    = BORDER_WIDTH;
	conf.focus_color     = COLOR_FOCUS;
	conf.unfocus_color   = COLOR_UNFOCUS;
	conf.gap_left = conf.gap_down
		= conf.gap_up = conf.gap_right = GAP;
	conf.grid_gap        = GRID_GAP;
	conf.cursor_position = CURSOR_POSITION;
	conf.groups          = GROUPS;
	conf.sloppy_focus    = SLOPPY_FOCUS;
}

static void
load_config(char *config_path)
{
	if (fork() == 0) {
		setsid();
		DMSG("loading %s\n", config_path);
		execl(config_path, config_path, NULL);
		errx(EXIT_FAILURE, "couldn't load config file");
	}
}

int main(int argc, char *argv[])
{
	int opt;
	char *config_path = malloc(MAXLEN * sizeof(char));
	config_path[0] = '\0';
	while ((opt = getopt(argc, argv, "hc:")) != -1) {
		switch (opt) {
			case 'h':
				usage(argv[0]);
				break;
			case 'c':
				snprintf(config_path, MAXLEN * sizeof(char), "%s", optarg);
				break;
		}
	}
	atexit(cleanup);

	register_event_handlers();
	register_ipc_handlers();
	load_defaults();

	if (setup() < 0)
		errx(EXIT_FAILURE, "error connecting to X");
	/* if not set, get path of the rc file */
	if (config_path[0] == '\0') {
		char *xdg_home = getenv("XDG_CONFIG_HOME");
		if (xdg_home != NULL)
			snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s", xdg_home, __NAME__, __CONFIG_NAME__);
		else
			snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s/%s", getenv("HOME"), ".config",
					__NAME__, __CONFIG_NAME__);
	}
	/* execute config file */
	load_config(config_path);
	run();

	free(config_path);

	return exit_code;
}
