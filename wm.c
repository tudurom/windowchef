/* Copyright (c) 2016-2019 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include <assert.h>
#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <unistd.h>

#include <sys/wait.h>

#include "common.h"
#include "config.h"
#include "ipc.h"
#include "helpers.h"
#include "types.h"

#define EVENT_MASK(ev) (((ev) & ~0x80))
/* XCB event with the biggest value */
#define LAST_XCB_EVENT XCB_GET_MODIFIER_MAPPING
#define NULL_GROUP 0xffffffff
#define PI 3.14159265

/* atoms identifiers */
enum { WM_DELETE_WINDOW, WINDOWCHEF_ACTIVE_GROUPS, _IPC_ATOM_COMMAND, WINDOWCHEF_STATUS, NR_ATOMS };

/* button identifiers */
enum { BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT, NR_BUTTONS };

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
static int  last_group = 0;
/* keyboard modifiers (for mouse support) */
static uint16_t num_lock, caps_lock, scroll_lock;
static const xcb_button_index_t mouse_buttons[] = {
	XCB_BUTTON_INDEX_1,
	XCB_BUTTON_INDEX_2,
	XCB_BUTTON_INDEX_3,
};
/* list of all windows. NULL is the empty list */
static struct list_item *win_list   = NULL;
static struct list_item *mon_list   = NULL;
static struct list_item *focus_list = NULL;
static char *atom_names[NR_ATOMS] = {
	"WM_DELETE_WINDOW",
	"WINDOWCHEF_ACTIVE_GROUPS",
	ATOM_COMMAND,
	"WINDOWCHEF_STATUS",
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
static struct client * setup_window(xcb_window_t);
static void set_focused_no_raise(struct client *);
static void set_focused(struct client *);
static void set_focused_last_best();
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
static void reset_window(struct client *);
static bool is_special(struct client *);
static void cycle_window(struct client *);
static void rcycle_window(struct client *);
static void cycle_window_in_group(struct client *);
static void rcycle_window_in_group(struct client *);
static void cardinal_focus(uint32_t);
static float get_distance_between_windows(struct client *, struct client *);
static float get_angle_between_windows(struct client *, struct client *);
static struct win_position get_window_position(uint32_t, struct client *);
static bool is_overlapping(struct client *, struct client *);
static bool is_in_valid_direction(uint32_t, float, float);
static bool is_in_cardinal_direction(uint32_t , struct client *, struct client *);
static xcb_atom_t get_atom(char *);
static void update_desktop_viewport(void);
static bool get_pointer_location(xcb_window_t *, int16_t *, int16_t *);
static void center_pointer(struct client *);
static struct client * find_client(xcb_window_t *);
static bool get_geometry(xcb_window_t *, int16_t *, int16_t *, uint16_t *, uint16_t *, uint8_t *);
static void set_borders(struct client *client, uint32_t, uint32_t);
static bool is_mapped(xcb_window_t);
static void free_window(struct client *);

static void add_to_client_list(xcb_window_t);
static void update_client_list(void);
static void update_wm_desktop(struct client *);
static void update_current_desktop(struct client *);
static void update_window_status(struct client *);

static void group_add_window(struct client *, uint32_t);
static void group_remove_window(struct client *);
static void group_remove_all_windows(uint32_t);
static void group_activate(uint32_t);
static void group_deactivate(uint32_t);
static void group_toggle(uint32_t);
static void group_activate_specific(uint32_t);

static void update_group_list(void);
static void change_nr_of_groups(uint32_t);
static void refresh_borders(void);
static void update_ewmh_wm_state(struct client *);
static void handle_wm_state(struct client *, xcb_atom_t, unsigned int);

static void snap_window(struct client *, enum position);
static void grid_window(struct client *, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
static void move_grid_window(struct client *, uint16_t, uint16_t);
static void resize_grid_window(struct client *, uint16_t, uint16_t);

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
static void event_focus_in(xcb_generic_event_t *);
static void event_focus_out(xcb_generic_event_t *);
static void event_button_press(xcb_generic_event_t *);

static void register_ipc_handlers(void);
static void ipc_window_move(uint32_t *);
static void ipc_window_move_absolute(uint32_t *);
static void ipc_window_resize(uint32_t *);
static void ipc_window_resize_absolute(uint32_t *);
static void ipc_window_maximize(uint32_t *);
static void ipc_window_unmaximize(uint32_t *);
static void ipc_window_hor_maximize(uint32_t *);
static void ipc_window_ver_maximize(uint32_t *);
static void ipc_window_monocle(uint32_t *);
static void ipc_window_close(uint32_t *);
static void ipc_window_put_in_grid(uint32_t *);
static void ipc_window_move_in_grid(uint32_t *);
static void ipc_window_resize_in_grid(uint32_t *);
static void ipc_window_snap(uint32_t *);
static void ipc_window_cycle(uint32_t *);
static void ipc_window_rev_cycle(uint32_t *);
static void ipc_window_cycle_in_group(uint32_t *);
static void ipc_window_rev_cycle_in_group(uint32_t *);
static void ipc_window_cardinal_focus(uint32_t *);
static void ipc_window_focus(uint32_t *);
static void ipc_window_focus_last(uint32_t *);
static void ipc_group_add_window(uint32_t *);
static void ipc_group_remove_window(uint32_t *);
static void ipc_group_remove_all_windows(uint32_t *);
static void ipc_group_activate(uint32_t *);
static void ipc_group_deactivate(uint32_t *);
static void ipc_group_toggle(uint32_t *);
static void ipc_group_activate_specific(uint32_t *);
static void ipc_wm_quit(uint32_t *);
static void ipc_wm_config(uint32_t *);

static void pointer_init(void);
static int16_t pointer_modfield_from_keysym(xcb_keysym_t);
static void window_grab_buttons(xcb_window_t);
static void window_grab_button(xcb_window_t, uint8_t, uint16_t);
static bool pointer_grab(enum pointer_action);
static enum resize_handle get_handle(struct client *, xcb_point_t, enum pointer_action);
static void track_pointer(struct client *, enum pointer_action, xcb_point_t);
static void grab_buttons(void);
static void ungrab_buttons(void);

static void usage(char *);
static void version(void);
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
	ungrab_buttons();
	if (ewmh != NULL)
		xcb_ewmh_connection_wipe(ewmh);
	if (win_list != NULL)
		list_delete_all_items(&win_list, true);
	if (focus_list != NULL)
	    list_delete_all_items(&focus_list, true);
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
	update_desktop_viewport();

	xcb_atom_t supported_atoms[] = {
		ewmh->_NET_SUPPORTED               , ewmh->_NET_WM_DESKTOP              ,
		ewmh->_NET_NUMBER_OF_DESKTOPS      , ewmh->_NET_CURRENT_DESKTOP         ,
		ewmh->_NET_ACTIVE_WINDOW           , ewmh->_NET_WM_STATE                ,
		ewmh->_NET_WM_STATE_FULLSCREEN     , ewmh->_NET_WM_STATE_MAXIMIZED_VERT ,
		ewmh->_NET_WM_STATE_MAXIMIZED_HORZ , ewmh->_NET_WM_NAME                 ,
		ewmh->_NET_WM_ICON_NAME            , ewmh->_NET_WM_WINDOW_TYPE          ,
		ewmh->_NET_WM_WINDOW_TYPE_DOCK     , ewmh->_NET_WM_PID                  ,
		ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR  , ewmh->_NET_WM_WINDOW_TYPE_DESKTOP  ,
		ewmh->_NET_SUPPORTING_WM_CHECK     , ewmh->_NET_DESKTOP_VIEWPORT        ,
	};
	xcb_ewmh_set_supported(ewmh, scrno, sizeof(supported_atoms) / sizeof(xcb_atom_t), supported_atoms);

	xcb_ewmh_set_supporting_wm_check(ewmh, scr->root, scr->root);

	pointer_init();

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
							/* If at end, take from the beginning */
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
	struct monitor *m, *ret;

	m = ret = NULL;
	item = mon_list;
	while (item != NULL) {
		m = item->data;
		if (x >= m->x && x <= m->x + m->width
			&& y >= m->y && y <= m->y + m->height)
			ret = m;

		item = item->next;
	}

	return ret;
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

	update_group_list();
	halt = false;
	exit_code = EXIT_SUCCESS;
	while (!halt) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (ev) {
			DMSG("X Event %d\n", ev->response_type & ~0x80);
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
	struct list_item *focus_item;
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

	focus_item = list_add_item(&focus_list);
	if (focus_item == NULL)
	    return NULL;

	client = malloc(sizeof(struct client));
	if (client == NULL)
		return NULL;

	/* initialize variables */
	focus_item->data = client;
	client->focus_item = focus_item;
	item->data = client;
	client->item = item;
	client->window = win;
	client->geom.x = client->geom.y = client->geom.width
				   = client->geom.height
				   = client->min_width = client->min_height = 0;
	client->grid.gx = client->grid.gy = client->grid.px
		= client->grid.py = client->grid.sx = client->grid.sy = 0;
	client->width_inc = client->height_inc = 1;
	client->maxed  = client->hmaxed = client->vmaxed
		= client->monocled = client->gridded = client->geom.set_by_user = false;
	client->monitor = NULL;
	client->mapped  = false;
	client->group   = NULL_GROUP;
	get_geometry(&client->window, &client->geom.x, &client->geom.y,
			&client->geom.width, &client->geom.height, &client->depth);

	xcb_icccm_get_wm_normal_hints_reply(conn,
			xcb_icccm_get_wm_normal_hints_unchecked(conn, win),
			&hints, NULL);

	if (hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
		client->geom.set_by_user = true;

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		client->min_width = hints.min_width;
		client->min_height = hints.min_height;
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
		client->width_inc  = hints.width_inc;
		client->height_inc = hints.height_inc;
	}

	update_window_status(client);
	DMSG("new window was born 0x%08x\n", client->window);

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

	/* show window if hidden */
	xcb_map_window(conn, client->window);

	if (!client->maxed)
		set_borders(client, conf.focus_color, conf.internal_focus_color);

	/* focus the window */
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
			client->window, XCB_CURRENT_TIME);

	/* set ewmh property */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
			ewmh->_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &client->window);

	/* set window state */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
						ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

	/* set the focus state to inactive on the previously focused window */
	if (client != focused_win) {
		if (focused_win != NULL && !focused_win->maxed)
			set_borders(focused_win, conf.unfocus_color, conf.internal_unfocus_color);
	}

	if (client->focus_item != NULL)
		list_move_to_head(&focus_list, client->focus_item);

	focused_win = client;

	window_grab_buttons(focused_win->window);
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
 * Focus last best focus (in a valid group, mapped, etc)
 */

static void
set_focused_last_best()
{
	struct list_item *focused_item;
	struct client *client;

	focused_item = focus_list->next;
	if (focused_item == NULL)
		focused_item = focus_list;

	while (focused_item != NULL) {
		client = focused_item->data;

		if (client != NULL && client->mapped) {
			set_focused(client);
			return;
		}

		focused_item = focused_item->next;
	}
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

	if (conf.last_window_focusing && client != NULL && client == focused_win)
	    set_focused_last_best();

	if (focused_win == client)
		focused_win = NULL;

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
	update_window_status(find_client(&win));

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

	get_geometry(&win, &win_x, &win_y, &win_w, &win_h, NULL);

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
	update_window_status(find_client(&win));
	refresh_borders();
}

/*
 * Resizes window by a certain amount.
 */

static void
resize_window(xcb_window_t win, int16_t w, int16_t h)
{
	struct client *client;
	int32_t aw, ah;

	client = find_client(&win);
	if (client == NULL)
		return;

	aw = client->geom.width;
	ah = client->geom.height;

	if (aw + w > 0)
		aw += w;
	if (ah + h > 0)
		ah += h;

	/* avoid weird stuff */
	if (aw < 0)
		aw = 0;
	if (ah < 0)
		ah = 0;

	if (client->min_width != 0 && aw < client->min_width)
		aw = client->min_width;

	if (client->min_height != 0 && ah < client->min_height)
		ah = client->min_height;

	client->geom.width  = aw - conf.resize_hints * (aw % client->width_inc);
	client->geom.height = ah - conf.resize_hints * (ah % client->height_inc);

	resize_window_absolute(win, client->geom.width, client->geom.height);
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
	if (client == NULL)
		return;

	if (is_special(client))
		reset_window(client);

	client->maxed = true;

	/* maximized windows don't have borders */
	values[0] = 0;
	if (client->geom.width != mon_width || client->geom.height != mon_height)
		client->orig_geom = client->geom;
	xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
			values);

	client->geom.x = mon_x;
	client->geom.y = mon_y;
	client->geom.width = mon_width;
	client->geom.height = mon_height;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	set_focused_no_raise(client);

	update_ewmh_wm_state(client);
	update_window_status(client);
}

static void
hmaximize_window(struct client *client, int16_t mon_x, uint16_t mon_width)
{
	if (client == NULL)
		return;

	if (is_special(client))
		reset_window(client);

	if (client->geom.width != mon_width)
		client->orig_geom = client->geom;
	client->geom.x = mon_x + conf.gap_left;
	client->geom.width = mon_width - conf.gap_left - conf.gap_right - 2 * conf.border_width;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->hmaxed = true;

	update_ewmh_wm_state(client);
	update_window_status(client);
}

static void
vmaximize_window(struct client *client, int16_t mon_y, uint16_t mon_height)
{
	if (client == NULL)
		return;

	if (is_special(client))
		reset_window(client);

	if (client->geom.height != mon_height)
		client->orig_geom = client->geom;

	client->geom.y = mon_y + conf.gap_up;
	client->geom.height = mon_height - conf.gap_up - conf.gap_down - 2 * conf.border_width;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	client->vmaxed = true;

	update_ewmh_wm_state(client);
	update_window_status(client);
}

static void
monocle_window(struct client *client, int16_t mon_x, int16_t mon_y, uint16_t mon_width, uint16_t mon_height)
{
	if (client == NULL)
		return;

	if (is_special(client))
		reset_window(client);

	client->orig_geom = client->geom;

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

	update_ewmh_wm_state(client);
	update_window_status(client);
}

static void
reset_window(struct client *client)
{
	xcb_atom_t state[] = {
		XCB_ICCCM_WM_STATE_NORMAL,
		XCB_NONE
	};
	client->geom.x = client->orig_geom.x;
	client->geom.y = client->orig_geom.y;
	client->geom.width = client->orig_geom.width;
	client->geom.height = client->orig_geom.height;
	client->maxed = client->hmaxed
		= client->vmaxed = client->monocled = client->gridded = false;

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);
	set_borders(client, conf.unfocus_color, conf.internal_unfocus_color);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
			ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, state);
	update_window_status(client);
}

static bool
is_special(struct client *client)
{
	if (client == NULL)
		return false;

	return client->maxed
		|| client->vmaxed
		|| client->hmaxed
		|| client->monocled
		|| client->gridded;
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
cardinal_focus(uint32_t dir)
{
	/* Don't focus if we don't have a current focus! */
	if (focused_win == NULL)
		return;

	struct list_item *valid_windows = NULL;
	struct list_item *desired_window = NULL;
	struct list_item *valid_window;
	struct list_item *win;

	struct win_position focus_win_pos = get_window_position(CENTER, focused_win);

	float closest_distance = -1;

	win = win_list;

	while(win != NULL) {
		/* Skip focused window */
		if (((struct client *)win->data)->window == focused_win->window) {
			win = win->next;
			continue;
		}

		/* Skip unmapped windows */
		if (!((struct client *)win->data)->mapped) {
			win = win->next;
			continue;
		}

		struct win_position win_pos = get_window_position(CENTER, (struct client *)win->data);

		valid_window = NULL;

		switch (dir) {
			case NORTH:
				if (win_pos.y < focus_win_pos.y)
					valid_window = list_add_item(&valid_windows);
				break;
			case SOUTH:
				if (win_pos.y >= focus_win_pos.y)
					valid_window = list_add_item(&valid_windows);
				break;
			case WEST:
				if (win_pos.x < focus_win_pos.x)
					valid_window = list_add_item(&valid_windows);
				break;
			case EAST:
				if (win_pos.x >= focus_win_pos.x)
					valid_window = list_add_item(&valid_windows);
				break;
		}

		if (valid_window != NULL)
			valid_window->data = win->data;

		win = win->next;
	}

	win = valid_windows;
	while(win != NULL) {
		float cur_distance;
		float cur_angle;

		cur_distance = get_distance_between_windows(focused_win, (struct client *)win->data);
		cur_angle = get_angle_between_windows(focused_win, (struct client *)win->data);

		if (is_in_valid_direction(dir, cur_angle, 10)) {
			if (is_overlapping(focused_win, (struct client *)win->data))
				cur_distance = cur_distance * 0.1;
			cur_distance = cur_distance * 0.80;
		}
		else if (is_in_valid_direction(dir, cur_angle, 25)) {
			if (is_overlapping(focused_win, (struct client *)win->data))
				cur_distance = cur_distance * 0.1;
			cur_distance = cur_distance * 0.85;
		}
		else if (is_in_valid_direction(dir, cur_angle, 35)) {
			if (is_overlapping(focused_win, (struct client *)win->data))
				cur_distance = cur_distance * 0.1;
			cur_distance = cur_distance * 0.9;
		}
		else if (is_in_valid_direction(dir, cur_angle, 50)) {
			if (is_overlapping(focused_win, (struct client *)win->data))
				cur_distance = cur_distance * 0.1;
			cur_distance = cur_distance * 3;
		}
		else {
			win = win->next;
			continue;
		}

		if (is_in_cardinal_direction(dir, focused_win, (struct client *)win->data))
			cur_distance = cur_distance * 0.9;


		if (closest_distance == -1 || (cur_distance < closest_distance)) {
			closest_distance = cur_distance;
			desired_window = win;
		}

		win = win->next;
	}

	if (desired_window != NULL)
		set_focused(desired_window->data);

	if (valid_windows != NULL)
		list_delete_all_items(&valid_windows, false);
}

static struct win_position
get_window_position(uint32_t mode, struct client *win)
{
	struct win_position pos;
	pos.x = 0;
	pos.y = 0;

	switch (mode) {
		case CENTER:
			pos.x = win->geom.x + (win->geom.width / 2);
			pos.y = win->geom.y + (win->geom.height / 2);
			break;
		case TOP_LEFT:
			pos.x = win->geom.x;
			pos.y = win->geom.y;
			break;
		case TOP_RIGHT:
			pos.x = win->geom.x + win->geom.width;
			pos.y = win->geom.y;
			break;
		case BOTTOM_RIGHT:
			pos.x = win->geom.x + win->geom.width;
			pos.y = win->geom.y + win->geom.height;
			break;
		case BOTTOM_LEFT:
			pos.x = win->geom.x;
			pos.y = win->geom.y + win->geom.height;
			break;
	}
	return pos;
}

static bool
is_in_cardinal_direction(uint32_t direction, struct client *a, struct client *b)
{
	struct win_position pos_a_top_left = get_window_position(TOP_LEFT, a);
	struct win_position pos_a_top_right = get_window_position(TOP_RIGHT, a);
	struct win_position pos_a_bot_left = get_window_position(BOTTOM_LEFT, a);

	struct win_position pos_b_center = get_window_position(CENTER, b);

	switch(direction) {
		case NORTH:
		case SOUTH:
			return pos_a_top_left.x <= pos_b_center.x && pos_a_top_right.x >= pos_b_center.x;

		case WEST:
		case EAST:
			return pos_a_top_left.y <= pos_b_center.y && pos_a_bot_left.y >= pos_b_center.y;
	}

	return false;
}

static bool
is_in_valid_direction(uint32_t direction, float window_direction, float delta)
{
	switch((uint32_t)direction) {
		case NORTH:
			if (window_direction >= (180 - delta) || window_direction <= (-180 + delta))
				return true;
			break;
		case SOUTH:
			if (fabs(window_direction) <= ( 0 + delta))
				return true;
			break;
		case EAST:
			if (window_direction <= (90 + delta) && window_direction > (90 - delta))
				return true;
			break;
		case WEST:
			if (window_direction <= (-90 + delta) && window_direction >= (-90 - delta))
				return true;
			break;
	}

	return false;
}

static bool
is_overlapping(struct client *a, struct client *b)
{
	struct win_position pos_a_top_left = get_window_position(TOP_LEFT, a);
	struct win_position pos_a_top_right = get_window_position(TOP_RIGHT, a);
	struct win_position pos_a_bot_left = get_window_position(BOTTOM_LEFT, a);

	struct win_position pos_b_top_left = get_window_position(TOP_LEFT, b);
	struct win_position pos_b_top_right = get_window_position(TOP_RIGHT, b);
	struct win_position pos_b_bot_left = get_window_position(BOTTOM_LEFT, b);

	bool is_x_top_overlapped = pos_a_top_left.x <= pos_b_top_left.x && pos_a_top_right.x >= pos_b_top_left.x;
	bool is_x_bot_overlapped = pos_a_top_left.x <= pos_b_top_right.x && pos_a_top_right.x >= pos_b_top_right.x;

	bool is_y_top_overlapped = pos_a_top_left.y <= pos_b_top_left.y && pos_a_bot_left.y >= pos_b_top_left.y;
	bool is_y_bot_overlapped = pos_a_top_left.y <= pos_b_bot_left.y && pos_a_bot_left.y >= pos_b_bot_left.y;

	return (is_x_top_overlapped || is_x_bot_overlapped) && (is_y_top_overlapped || is_y_bot_overlapped);
}

static float
get_angle_between_windows(struct client *a, struct client *b)
{
	struct win_position a_pos = get_window_position(CENTER, a);
	struct win_position b_pos = get_window_position(CENTER, b);

	float dx = (float)(b_pos.x - a_pos.x);
	float dy = (float)(b_pos.y - a_pos.y);

	if (dx == 0.0 && dy == 0.0)
		return 0.0;

	return atan2(dx,dy) * (180 / PI);
}

static float
get_distance_between_windows(struct client *a, struct client *b)
{
	struct win_position a_pos = get_window_position(CENTER, a);
	struct win_position b_pos = get_window_position(CENTER, b);

	float distance = hypot((float)(b_pos.x - a_pos.x), (float)(b_pos.y - a_pos.y));
	return distance;
}

/*
 * Get atom by name.
 */

static xcb_atom_t
get_atom(char *name)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, false, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);

	if (!reply)
		return XCB_ATOM_STRING;

	return reply->atom;
}

/*
 * Update _NET_DESKTOP_VIEWPORT root property.
 */

static void
update_desktop_viewport(void)
{
	xcb_ewmh_coordinates_t coord = {0, 0};
	xcb_ewmh_set_desktop_viewport(ewmh, scrno, 1, &coord);
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
get_geometry(xcb_window_t *win, int16_t *x, int16_t *y, uint16_t *width, uint16_t *height, uint8_t *depth)
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
	if (depth != NULL)
		*depth = reply->depth;

	free(reply);
	return true;
}

/*
 * Set the color of the border.
 */

static void
set_borders(struct client *client, uint32_t color, uint32_t internal_color)
{
	if (client == NULL || conf.borders == false)
		return;
	uint32_t values[1];

	color = get_color_pixel(color);
	internal_color = get_color_pixel(internal_color);

	values[0] = conf.border_width;
	xcb_configure_window(conn, client->window,
			XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

	if (conf.internal_border_width == 0) {
		values[0] = color;
		xcb_change_window_attributes(conn, client->window, XCB_CW_BORDER_PIXEL, values);
	}

	if (conf.internal_border_width != 0) {
		uint32_t calc_iborder = conf.border_width - conf.internal_border_width;
		xcb_rectangle_t rect_inner[] = {
			{
				client->geom.width,
				0,
				conf.border_width - calc_iborder,
				client->geom.height + conf.border_width - calc_iborder
			},
			{
				client->geom.width + conf.border_width + calc_iborder,
				0,
				conf.border_width - calc_iborder,
				client->geom.height + conf.border_width - calc_iborder
			},
			{
				0,
				client->geom.height,
				client->geom.width + conf.border_width - calc_iborder,
				conf.border_width - calc_iborder
			},
			{
				0,
				client->geom.height + conf.border_width + calc_iborder,
				client->geom.width + conf.border_width - calc_iborder,
				conf.border_width - calc_iborder
			},
			{
				client->geom.width + conf.border_width + calc_iborder,
				conf.border_width + client->geom.height + calc_iborder,
				conf.border_width,
				conf.border_width
			}
		};

		xcb_rectangle_t rect_outer[] = {
			{
				client->geom.width + conf.border_width - calc_iborder,
				0,
				calc_iborder,
				client->geom.height + conf.border_width * 2
			},
			{
				client->geom.width + conf.border_width,
				0,
				calc_iborder,
				client->geom.height + conf.border_width * 2
			},
			{
				0,
				client->geom.height + conf.border_width - calc_iborder,
				client->geom.width + conf.border_width * 2,
				calc_iborder
			},
			{
				0,
				client->geom.height + conf.border_width,
				client->geom.width + conf.border_width * 2,
				calc_iborder
			},
			{
				1,1,1,1
			}
		};

		xcb_pixmap_t pmap = xcb_generate_id(conn);
		xcb_create_pixmap(conn, client->depth, pmap, scr->root,
			client->geom.width + (conf.border_width * 2),
			client->geom.height + (conf.border_width * 2)
		);

		xcb_gcontext_t gc = xcb_generate_id(conn);
		xcb_create_gc(conn, gc, pmap, 0, NULL);

		values[0] = color;
		xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, &values[0]);
		xcb_poly_fill_rectangle(conn, pmap, gc, 5, rect_outer);

		values[0] = internal_color;
		xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, &values[0]);
		xcb_poly_fill_rectangle(conn, pmap, gc, 5, rect_inner);

		values[0] = pmap;
		xcb_change_window_attributes(conn,client->window, XCB_CW_BORDER_PIXMAP,
				&values[0]);

		xcb_free_pixmap(conn,pmap);
		xcb_free_gc(conn,gc);
	}
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
	struct list_item *focus_item;

	DMSG("freeing 0x%08x\n", client->window);
	item = client->item;
	focus_item = client->focus_item;


	free(client);
	list_delete_item(&win_list, item);
	list_delete_item(&focus_list, focus_item);
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
update_current_desktop(struct client *client)
{
	if (client != NULL)
		xcb_ewmh_set_current_desktop(ewmh, 0, client->group);
}

static void update_window_status(struct client *client)
{
	/* it really shouldn't happen */
	if (client == NULL)
		return;
	int size = 0;
	char *str = NULL;
	char *state;
	if (client->maxed) state = "maxed";
	else if (client->hmaxed) state = "hmaxed";
	else if (client->vmaxed) state = "vmaxed";
	else if (client->monocled) state = "monocled";
	else if (client->gridded) state = "gridded";
	else state = "normal";
	/* this is going to be fun */
#define _BOOL_VALUE(value) ((value) ? "true" : "false")
	size = asprintf(&str,
	"{"
		"\"window\":\"0x%08x\","
		"\"geom\":{"
			"\"x\":%d,"
			"\"y\":%d,"
			"\"width\":%d,"
			"\"height\":%d,"
			"\"set_by_user\":%s"
		"},"
		"\"state\":\"%s\","
		"\"min_width\":%d,"
		"\"min_height\":%d,"
		"\"max_width\":%d,"
		"\"max_height\":%d,"
		"\"width_inc\":%d,"
		"\"height_inc\":%d,"
		"\"mapped\":%s,"
		"\"group\":%d"
	"}", client->window, client->geom.x, client->geom.y, client->geom.width,
	client->geom.height, _BOOL_VALUE(client->geom.set_by_user), state,
	client->min_width, client->min_height, client->max_width, client->max_height,
	client->width_inc, client->height_inc, _BOOL_VALUE(client->mapped), client->group);
#undef _BOOL_VALUE
	if (size == -1) {
		DMSG("asprintf returned -1\n");
		return;
	}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
			ATOMS[WINDOWCHEF_STATUS], XCB_ATOM_STRING, 8, size, str);
	free(str);
}

static void
group_add_window(struct client *client, uint32_t group)
{
	if (client != NULL && group < conf.groups) {
		client->group = group;
		group_in_use[group] = true;
		update_wm_desktop(client);
		update_group_list();
		update_current_desktop(client);
		update_window_status(client);
	}
}

static void
group_remove_window(struct client *client)
{
	if (client != NULL) {
		client->group = NULL_GROUP;
		update_wm_desktop(client);
		update_group_list();
		update_current_desktop(client);
		update_window_status(client);
	}
}

static void
group_remove_all_windows(uint32_t group)
{
	if (group >= conf.groups)
		return;

	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		if (client != NULL && client->group == group) {
			group_remove_window(client);
		}
	}

	group_in_use[group] = false;
}

static void
group_activate(uint32_t group) {
	if (group >= conf.groups)
		return;

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
	last_group = group;
	update_group_list();
}

static void
group_deactivate(uint32_t group)
{
	if (group >= conf.groups)
		return;

	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		if (client->group == group)
			xcb_unmap_window(conn, client->window);
	}
	group_in_use[group] = false;
	update_group_list();
}

static void
group_toggle(uint32_t group)
{
	if (group >= conf.groups)
		return;

	if (group_in_use[group])
		group_deactivate(group);
	else
		group_activate(group);
	last_group = group;
	update_group_list();
}

static void
group_activate_specific(uint32_t group)
{
	if (group >= conf.groups)
		return;

	for (unsigned int i = 0; i < conf.groups; i++) {
		if (i == group)
			group_activate(i);
		else
			group_deactivate(i);
	}
	update_group_list();
}

static void update_group_list(void)
{
	struct list_item *item;
	struct client *client;
	bool first = true;
	uint32_t data[1];

	for (unsigned int i = 0; i < conf.groups; i++) {
		/* deactivate group if no window in group */
		item = win_list;
		while (item != NULL && (client = item->data)->group != i)
			item = item->next;
		if (item == NULL)
			group_in_use[i] = false;

		if (group_in_use[i]) {
			uint8_t mode = XCB_PROP_MODE_APPEND;
			data[0] = i + 1;
			if (first) {
				mode = XCB_PROP_MODE_REPLACE;
				first = false;
			}
			xcb_change_property(conn, mode, scr->root, ATOMS[WINDOWCHEF_ACTIVE_GROUPS], XCB_ATOM_INTEGER, 32, 1, data);
		}
	}

	if (first) {
		data[0] = 0;
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, ATOMS[WINDOWCHEF_ACTIVE_GROUPS], XCB_ATOM_INTEGER, 32, 1, data);
	}
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
refresh_borders(void)
{
	if (!conf.apply_settings)
		return;

	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		if (client->maxed)
			continue;

		if (client == focused_win)
			set_borders(client, conf.focus_color, conf.internal_focus_color);
		else
			set_borders(client, conf.unfocus_color, conf.internal_unfocus_color);
	}
}

static void
update_ewmh_wm_state(struct client *client)
{
	int i;
	uint32_t values[12];

	if (client == NULL)
		return;
#define HANDLE_WM_STATE(s) \
	values[i] = ewmh->_NET_WM_STATE_##s; \
	i++; \
	DMSG("ewmh net_wm_state %s present\n", #s);

	i = 0;
	if (client->maxed) {
		HANDLE_WM_STATE(FULLSCREEN);
	}
	if (client->vmaxed) {
		HANDLE_WM_STATE(MAXIMIZED_VERT);
	}
	if (client->hmaxed) {
		HANDLE_WM_STATE(MAXIMIZED_HORZ);
	}

	xcb_ewmh_set_wm_state(ewmh, client->window, i, values);
}

/*
 * Maximize / unmaximize windows based on ewmh requests.
 */

static void
handle_wm_state(struct client *client, xcb_atom_t state, unsigned int action)
{
	int16_t mon_x, mon_y;
	uint16_t mon_w, mon_h;

	get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

	if (state == ewmh->_NET_WM_STATE_FULLSCREEN) {
		if (action == XCB_EWMH_WM_STATE_ADD) {
			maximize_window(client, mon_x, mon_y, mon_w, mon_h);
		} else if (action == XCB_EWMH_WM_STATE_REMOVE && client->maxed) {
			reset_window(client);
			set_focused(client);
		} else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
			if (client->maxed) {
				reset_window(client);
				set_focused(client);
			} else {
				maximize_window(client, mon_x, mon_y, mon_w, mon_h);
			}
		}
	} else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_VERT) {
		if (action == XCB_EWMH_WM_STATE_ADD) {
			vmaximize_window(client, mon_y, mon_h);
		} else if (action == XCB_EWMH_WM_STATE_REMOVE) {
			if (client->vmaxed)
				reset_window(client);
		} else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
			if (client->vmaxed)
				reset_window(client);
			else
				vmaximize_window(client, mon_y, mon_h);
		}
	} else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_HORZ) {
		if (action == XCB_EWMH_WM_STATE_ADD) {
			hmaximize_window(client, mon_y, mon_h);
		} else if (action == XCB_EWMH_WM_STATE_REMOVE) {
			if (client->hmaxed)
				reset_window(client);
		} else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
			if (client->hmaxed)
				reset_window(client);
			else
				hmaximize_window(client, mon_x, mon_w);
		}
	}
}

/*
 * Snap window in corner.
 */

static void
snap_window(struct client *client, enum position pos)
{
	int16_t mon_x, mon_y, win_x, win_y;
	uint16_t mon_w, mon_h, win_w, win_h;

	if (client == NULL)
		return;

	if (is_special(client)) {
		reset_window(client);
		set_focused(client);
	}

	fit_on_screen(client);

	win_x = client->geom.x;
	win_y = client->geom.y;
	win_w = client->geom.width + 2 * conf.border_width;
	win_h = client->geom.height + 2 * conf.border_width;

	get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

	switch (pos) {
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

	client->geom.x = win_x;
	client->geom.y = win_y;
	teleport_window(client->window, win_x, win_y);
	center_pointer(client);
	xcb_flush(conn);
}


/*
 * Put window in grid.
 */

static void
grid_window(struct client *client, uint16_t grid_width, uint16_t grid_height, uint16_t grid_x, uint16_t grid_y, uint16_t occ_w, uint16_t occ_h)
{
	int16_t mon_x, mon_y;
	uint16_t base_w, base_h;
	uint16_t new_w, new_h;
	uint16_t mon_w, mon_h;

	if (client == NULL || grid_x >= grid_width || grid_y >= grid_height)
		return;

	DMSG("Gridding window in grid of size (%d, %d) pos (%d, %d) window size (%d, %d)\n", grid_width, grid_height, grid_x, grid_y, occ_w, occ_h);
	if (is_special(client)) {
		reset_window(client);
		set_focused(client);
	}

	get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

	base_w = (mon_w - conf.gap_left - conf.gap_right - (grid_width - 1) * conf.grid_gap
			- grid_width * 2 * conf.border_width) / grid_width;
	base_h = (mon_h - conf.gap_up - conf.gap_down - (grid_height - 1) * conf.grid_gap
			- grid_height * 2 * conf.border_width) / grid_height;
	/* calculate new window size */
	new_w = base_w * occ_w + (occ_w - 1) * (conf.grid_gap + 2 * conf.border_width);

	new_h = base_h * occ_h + (occ_h - 1) * (conf.grid_gap + 2 * conf.border_width);

	client->orig_geom = client->geom;

	client->geom.width = new_w;
	client->geom.height = new_h;

	client->geom.x = mon_x + conf.gap_left + grid_x
		* (conf.border_width + base_w + conf.border_width + conf.grid_gap);
	client->geom.y = mon_y + conf.gap_up + grid_y
		* (conf.border_width + base_h + conf.border_width + conf.grid_gap);

	client->gridded = true;
	client->grid.gx = grid_width;
	client->grid.gy = grid_height;
	client->grid.px = grid_x;
	client->grid.py = grid_y;
	client->grid.sx = occ_w;
	client->grid.sy = occ_h;

	DMSG("w: %d\th: %d\n", new_w, new_h);

	teleport_window(client->window, client->geom.x, client->geom.y);
	resize_window_absolute(client->window, client->geom.width, client->geom.height);

	xcb_flush(conn);
}

static void
move_grid_window(struct client *client, uint16_t x, uint16_t y)
{

	int16_t new_px, new_py;

	new_px = client->grid.px + x;
	new_py = client->grid.py + y;

	if (!client->gridded
			|| client->grid.gx < new_px + client->grid.sx
			|| client->grid.gy < new_py + client->grid.sy
			|| new_px < 0
			|| new_py < 0)
		return;

	grid_window(client, client->grid.gx, client->grid.gy, new_px, new_py, client->grid.sx, client->grid.sy);
}

static void
resize_grid_window(struct client *client, uint16_t x, uint16_t y)
{

	int16_t new_sx, new_sy;

	new_sx = client->grid.sx + x;
	new_sy = client->grid.sy + y;

	if (!client->gridded
			|| client->grid.gx < new_sx + client->grid.px
			|| client->grid.gy < new_sy + client->grid.py
			|| new_sx < 1
			|| new_sy < 1)
		return;

	grid_window(client, client->grid.gx, client->grid.gy, client->grid.px, client->grid.py, new_sx, new_sy);
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
	events[XCB_FOCUS_IN]          = event_focus_in;
	events[XCB_FOCUS_OUT]         = event_focus_out;
	events[XCB_BUTTON_PRESS]      = event_button_press;
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
			client->geom.width = e->width;

		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT
				&& !client->maxed && !client->monocled && !client->vmaxed)
			client->geom.height = e->height;

		if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
			values[0] = e->stack_mode;
			xcb_configure_window(conn, e->window,
					XCB_CONFIG_WINDOW_STACK_MODE, values);
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
			values[0] = e->border_width;
			xcb_configure_window(conn, e->window,
					XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
		}

		if (!client->maxed) {
			fit_on_screen(client);
		}

		teleport_window(client->window, client->geom.x, client->geom.y);
		resize_window_absolute(client->window, client->geom.width, client->geom.height);
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

		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
			values[i] = e->border_width;
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
	if (conf.last_window_focusing && focused_win != NULL && focused_win == client) {
	    focused_win = NULL;
		set_focused_last_best();
	}

	if (client != NULL) {
		free_window(client);
	}


	update_client_list();
	update_group_list();
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

	/* create window if new */
	client = find_client(&e->window);
	if (client == NULL) {
		client = setup_window(e->window);

		/* client is a dock or some kind of window that needs to be ignored */
		if (client == NULL)
			return;

		if (!client->geom.set_by_user) {
			if (!get_pointer_location(&scr->root, &client->geom.x, &client->geom.y))
				client->geom.x = client->geom.y = 0;

			client->geom.x -= client->geom.width / 2;
			client->geom.y -= client->geom.height / 2;
			teleport_window(client->window, client->geom.x, client->geom.y);
		}
		if (conf.sticky_windows)
			group_add_window(client, last_group);
	}

	xcb_map_window(conn, e->window);

	/* in case of fire, abort */
	if (client == NULL)
		return;

	if (randr_base != -1) {
		client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
		if (client->monitor == NULL && mon_list != NULL)
			client->monitor = mon_list->data;
	}

	fit_on_screen(client);

	/* window is normal */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
			ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

	center_pointer(client);
	update_client_list();

	if (!client->maxed)
		set_borders(client, conf.focus_color, conf.internal_focus_color);
	update_current_desktop(client);
}

static void
event_map_notify(xcb_generic_event_t *ev)
{
	xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
	struct client *client = find_client(&e->window);

	if (client != NULL) {
		client->mapped = true;
		set_focused(client);
		update_window_status(client);
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

	client->mapped = false;

	if (conf.last_window_focusing && focused_win != NULL && client->window == focused_win->window) {
		focused_win = NULL;
		set_focused_last_best();
	}

	update_client_list();
	update_window_status(client);
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
		if (e->width != scr->width_in_pixels
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
		if (client != NULL) {
			client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
			update_current_desktop(client);
		}
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
	struct client *client;

	if (e->type == ATOMS[_IPC_ATOM_COMMAND] && e->format == 32) {
		/* Message from the client */
		data = e->data.data32;
		ipc_command = data[0];
		if (ipc_handlers[ipc_command] != NULL)
			(ipc_handlers[ipc_command])(data + 1);
		DMSG("IPC Command %u with arguments %u %u %u\n", ipc_command, data[1], data[2], data[3]);
	} else {
		client = find_client(&e->window);
		if (client == NULL)
			return;
		if (e->type == ewmh->_NET_WM_STATE) {
			DMSG("got _NET_WM_STATE for 0x%08x\n", client->window);
			handle_wm_state(client, e->data.data32[1], e->data.data32[0]);
			handle_wm_state(client, e->data.data32[2], e->data.data32[0]);
		} else if (e->type == ewmh->_NET_ACTIVE_WINDOW) {
			DMSG("got _NET_ACTIVE_WINDOW for 0x%08x\n", client->window);
			set_focused(client);
		}
	}
}

static void
event_focus_in(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;
	xcb_window_t win = e->event;
	struct client *client = find_client(&win);

	if (client != NULL)
		update_current_desktop(client);
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
event_button_press(xcb_generic_event_t *ev)
{
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;
	bool replay = false;

	for (int i = 0; i < NR_BUTTONS; i++) {
		if (e->detail != mouse_buttons[i])
			continue;
		if ((conf.click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
			 conf.click_to_focus == (int8_t) mouse_buttons[i]) &&
			(e->state & ~(num_lock | scroll_lock | caps_lock)) == XCB_NONE) {

			replay = !pointer_grab(POINTER_ACTION_FOCUS);
		} else {
			pointer_grab(conf.pointer_actions[i]);
		}
	}
	xcb_allow_events(conn, replay ? XCB_ALLOW_REPLAY_POINTER : XCB_ALLOW_SYNC_POINTER, e->time);
	xcb_flush(conn);
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
	ipc_handlers[IPCWindowUnmaximize]      = ipc_window_unmaximize;
	ipc_handlers[IPCWindowHorMaximize]     = ipc_window_hor_maximize;
	ipc_handlers[IPCWindowVerMaximize]     = ipc_window_ver_maximize;
	ipc_handlers[IPCWindowMonocle]         = ipc_window_monocle;
	ipc_handlers[IPCWindowClose]           = ipc_window_close;
	ipc_handlers[IPCWindowPutInGrid]       = ipc_window_put_in_grid;
	ipc_handlers[IPCWindowMoveInGrid]      = ipc_window_move_in_grid;
	ipc_handlers[IPCWindowResizeInGrid]    = ipc_window_resize_in_grid;
	ipc_handlers[IPCWindowSnap]            = ipc_window_snap;
	ipc_handlers[IPCWindowCycle]           = ipc_window_cycle;
	ipc_handlers[IPCWindowRevCycle]        = ipc_window_rev_cycle;
	ipc_handlers[IPCWindowCycleInGroup]    = ipc_window_cycle_in_group;
	ipc_handlers[IPCWindowRevCycleInGroup] = ipc_window_rev_cycle_in_group;
	ipc_handlers[IPCWindowCardinalFocus]   = ipc_window_cardinal_focus;
	ipc_handlers[IPCWindowFocus]           = ipc_window_focus;
	ipc_handlers[IPCWindowFocusLast]       = ipc_window_focus_last;
	ipc_handlers[IPCGroupAddWindow]        = ipc_group_add_window;
	ipc_handlers[IPCGroupRemoveWindow]     = ipc_group_remove_window;
	ipc_handlers[IPCGroupRemoveAllWindows] = ipc_group_remove_all_windows;
	ipc_handlers[IPCGroupActivate]         = ipc_group_activate;
	ipc_handlers[IPCGroupDeactivate]       = ipc_group_deactivate;
	ipc_handlers[IPCGroupToggle]           = ipc_group_toggle;
	ipc_handlers[IPCGroupActivateSpecific] = ipc_group_activate_specific;
	ipc_handlers[IPCWMQuit]                = ipc_wm_quit;
	ipc_handlers[IPCWMConfig]              = ipc_wm_config;
}

static void
ipc_window_move(uint32_t *d)
{
	int16_t x, y;

	if (focused_win == NULL)
		return;

	if (is_special(focused_win)) {
		reset_window(focused_win);
		set_focused(focused_win);
	}

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

	if (is_special(focused_win)) {
		reset_window(focused_win);
		set_focused(focused_win);
	}

	x = d[2];
	y = d[3];

	if (d[0] == IPC_MUL_MINUS)
		x = -x;
	if (d[1] == IPC_MUL_MINUS)
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

	if (focused_win == NULL)
		return;

	if (is_special(focused_win)) {
		reset_window(focused_win);
		set_focused(focused_win);
	}

	w = d[2];
	h = d[3];

	if (d[0] == IPC_MUL_MINUS)
		w = -w;
	if (d[1] == IPC_MUL_MINUS)
		h = -h;

	resize_window(focused_win->window, w, h);
	center_pointer(focused_win);
}

static void
ipc_window_resize_absolute(uint32_t *d)
{
	int16_t w, h;

	if (focused_win == NULL)
		return;

	if (is_special(focused_win)) {
		reset_window(focused_win);
		set_focused(focused_win);
	}

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
		reset_window(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);
		maximize_window(focused_win, mon_x, mon_y, mon_w, mon_h);
	}

	set_focused(focused_win);

	xcb_flush(conn);
}

static void
ipc_window_unmaximize(uint32_t *d)
{
	(void)(d);

	if (focused_win == NULL)
		return;

	if (is_special(focused_win)) {
		reset_window(focused_win);
		set_focused(focused_win);
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
		reset_window(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, NULL);
		hmaximize_window(focused_win, mon_x, mon_w);
	}

	set_focused(focused_win);

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
		reset_window(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, NULL, &mon_h);
		vmaximize_window(focused_win, mon_y, mon_h);
	}

	set_focused(focused_win);

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
		reset_window(focused_win);
	} else {
		get_monitor_size(focused_win, &mon_x, &mon_y, &mon_w, &mon_h);
		monocle_window(focused_win, mon_x, mon_y, mon_w, mon_h);
	}

	set_focused(focused_win);

	xcb_flush(conn);
}

static void
ipc_window_close(uint32_t *d)
{
	(void)(d);
	close_window(focused_win);
}

static void
ipc_window_put_in_grid(uint32_t *d)
{
	uint16_t grid_width, grid_height;
	uint16_t grid_x, grid_y;
	uint16_t occ_w, occ_h;
	const uint16_t m1 = 16U, m2 = 0xffffU;

	DMSG("data[4] = %d\n", d[4]);
	grid_width  = d[0] >> m1;
	grid_height = d[0] & m2;
	grid_x      = d[1] >> m1;
	grid_y      = d[1] & m2;
	occ_w       = d[2] >> m1;
	occ_h       = d[2] & m2;

	if (focused_win == NULL || grid_x >= grid_width || grid_y >= grid_height)
		return;

	grid_window(focused_win, grid_width, grid_height, grid_x, grid_y, occ_w, occ_h);
}

static void
ipc_window_move_in_grid(uint32_t *d)
{
	uint16_t x, y;

	if (focused_win == NULL)
		return;

	x = d[2];
	y = d[3];

	if (d[0] == IPC_MUL_MINUS)
		x = -x;
	if (d[1] == IPC_MUL_MINUS)
		y = -y;

	move_grid_window(focused_win, x, y);
}

static void
ipc_window_resize_in_grid(uint32_t *d)
{
	uint16_t x, y;

	if (focused_win == NULL)
		return;

	x = d[2];
	y = d[3];

	if (d[0] == IPC_MUL_MINUS)
		x = -x;
	if (d[1] == IPC_MUL_MINUS)
		y = -y;

	resize_grid_window(focused_win, x, y);
}

static void
ipc_window_snap(uint32_t *d)
{
	enum position pos = d[0];
	snap_window(focused_win, pos);
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
ipc_window_cardinal_focus(uint32_t *d)
{
	uint32_t mode = d[0];
	cardinal_focus(mode);
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
ipc_window_focus_last(uint32_t *d)
{
	(void)(d);
	if (focused_win != NULL)
		set_focused_last_best();
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
ipc_group_remove_all_windows(uint32_t *d)
{
	group_remove_all_windows(d[0] - 1);
}

static void
ipc_group_activate(uint32_t *d)
{
	group_activate(d[0] - 1);
}

static void
ipc_group_deactivate(uint32_t *d)
{
	group_deactivate(d[0] - 1);
}

static void
ipc_group_toggle(uint32_t *d)
{
	group_toggle(d[0] - 1);
}

static void
ipc_group_activate_specific(uint32_t *d)
{
	group_activate_specific(d[0] - 1);
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
		if (conf.apply_settings)
			refresh_borders();
		break;
	case IPCConfigColorFocused:
		conf.focus_color = d[1];
		if (conf.apply_settings)
			refresh_borders();
		break;
	case IPCConfigColorUnfocused:
		conf.unfocus_color = d[1];
		if (conf.apply_settings)
			refresh_borders();
		break;
	case IPCConfigInternalBorderWidth:
		conf.internal_border_width = d[1];
		if (conf.apply_settings)
			refresh_borders();
		break;
	case IPCConfigInternalColorFocused:
		conf.internal_focus_color = d[1];
		if (conf.apply_settings)
			refresh_borders();
		break;
	case IPCConfigInternalColorUnfocused:
		conf.internal_unfocus_color = d[1];
		if (conf.apply_settings)
			refresh_borders();
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
		break;
	case IPCConfigCursorPosition:
		conf.cursor_position = d[1];
		break;
	case IPCConfigGroupsNr:
		change_nr_of_groups(d[1]);
		break;
	case IPCConfigEnableSloppyFocus:
		conf.sloppy_focus = d[1];
		break;
	case IPCConfigEnableResizeHints:
		conf.resize_hints = d[1];
		break;
	case IPCConfigStickyWindows:
		conf.sticky_windows = d[1];
		break;
	case IPCConfigEnableBorders:
		conf.borders = d[1];
		break;
	case IPCConfigEnableLastWindowFocusing:
		conf.last_window_focusing = d[1];
		break;
	case IPCConfigApplySettings:
		conf.apply_settings = d[1];
		break;
	case IPCConfigReplayClickOnFocus:
		conf.replay_click_on_focus = d[1];
		break;
	case IPCConfigPointerActions:
		for (int i = 0; i < NR_BUTTONS; i++) {
			conf.pointer_actions[i] = d[i + 1];
		}
		ungrab_buttons();
		grab_buttons();
		break;
	case IPCConfigPointerModifier:
		conf.pointer_modifier = d[1];
		ungrab_buttons();
		grab_buttons();
		break;
	case IPCConfigClickToFocus:
		if (d[1] == UINT32_MAX)
			conf.click_to_focus = -1;
		else
			conf.click_to_focus = d[1];
		ungrab_buttons();
		grab_buttons();
		break;
	default:
		DMSG("!!! unhandled config key %d\n", key);
		break;
	}
}

static void
pointer_init(void)
{
	num_lock = pointer_modfield_from_keysym(XK_Num_Lock);
	caps_lock = pointer_modfield_from_keysym(XK_Caps_Lock);
	scroll_lock = pointer_modfield_from_keysym(XK_Scroll_Lock);

	if (caps_lock == XCB_NO_SYMBOL)
		caps_lock = XCB_MOD_MASK_LOCK;
}

static int16_t
pointer_modfield_from_keysym(xcb_keysym_t keysym)
{
	uint16_t modfield = 0;
	xcb_keycode_t *keycodes = NULL, *mod_keycodes = NULL;
	xcb_get_modifier_mapping_reply_t *reply = NULL;
	xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(conn);

	/* wrapped all of them in an ugly if to prevent getting values when
	   we don't need them */
	if (!((keycodes = xcb_key_symbols_get_keycode(symbols, keysym)) == NULL ||
		  (reply = xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), NULL)) == NULL ||
		  reply->keycodes_per_modifier < 1 ||
		  (mod_keycodes = xcb_get_modifier_mapping_keycodes(reply)) == NULL)) {

		int num_mod =
			xcb_get_modifier_mapping_keycodes_length(reply) /
			reply->keycodes_per_modifier;

		for (int i = 0; i < num_mod; i++) {
			for (int j = 0; j < reply->keycodes_per_modifier; j++) {
				xcb_keycode_t mk = mod_keycodes[i * reply->keycodes_per_modifier + j];
				if (mk != XCB_NO_SYMBOL) {
					for (xcb_keycode_t *k = keycodes; *k != XCB_NO_SYMBOL; k++) {
						if (*k == mk)
							modfield |= (1 << i);
					}
				}
			}
		}
	}

	xcb_key_symbols_free(symbols);
	free(keycodes);
	free(reply);
	return modfield;
}

static void
window_grab_buttons(xcb_window_t win)
{
	for (int i = 0; i < NR_BUTTONS; i++) {
		if (conf.click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
			conf.click_to_focus == (int8_t) mouse_buttons[i])
			window_grab_button(win, mouse_buttons[i], XCB_NONE);
		if (conf.pointer_actions[i] != POINTER_ACTION_NOTHING)
			window_grab_button(win, mouse_buttons[i], conf.pointer_modifier);
	}
	DMSG("grabbed buttons on 0x%08x\n", win);
}

static void
window_grab_button(xcb_window_t win, uint8_t button, uint16_t modifier)
{
#define GRAB(b, m)												   \
	xcb_grab_button(conn, false, win, XCB_EVENT_MASK_BUTTON_PRESS, \
	                XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, b, m)

	GRAB(button, modifier);
	if (num_lock != XCB_NO_SYMBOL && caps_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | num_lock | caps_lock | scroll_lock);
	if (num_lock != XCB_NO_SYMBOL && caps_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | num_lock | caps_lock);
	if (caps_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | caps_lock | scroll_lock);
	if (num_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | num_lock | scroll_lock);
	if (num_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | num_lock);
	if (caps_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | caps_lock);
	if (scroll_lock != XCB_NO_SYMBOL)
		GRAB(button, modifier | scroll_lock);
}

/*
 * Returns true if pointer needs to be synced.
 */
static bool
pointer_grab(enum pointer_action pac)
{
	xcb_window_t win = XCB_NONE;
	xcb_point_t pos = (xcb_point_t) {0, 0};
	struct client *client;

	xcb_query_pointer_reply_t *qr =
		xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), NULL);

	if (qr == NULL) {
		return false;
	}

	win = qr->child;
	pos = (xcb_point_t) {qr->root_x, qr->root_y};
	free(qr);

	client = find_client(&win);
	if (client == NULL)
		return true;

	raise_window(client->window);
	if (pac == POINTER_ACTION_FOCUS) {
		DMSG("grabbing pointer to focus on 0x%08x\n", client->window);
		if (client != focused_win) {
			set_focused(client);
			if (!conf.replay_click_on_focus)
				return true;
		}
		return false;
	}

	if (is_special(client)) {
		return true;
	}

	xcb_grab_pointer_reply_t *reply =
		xcb_grab_pointer_reply(conn, xcb_grab_pointer(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), NULL);

	if (reply == NULL || reply->status != XCB_GRAB_STATUS_SUCCESS) {
		free(reply);
		return true;
	}
	free(reply);

	track_pointer(client, pac, pos);

	return true;
}

static enum resize_handle
get_handle(struct client *client, xcb_point_t pos, enum pointer_action pac)
{
	if (client == NULL)
		return pac == POINTER_ACTION_RESIZE_SIDE ? HANDLE_LEFT : HANDLE_TOP_LEFT;

	enum resize_handle handle;
	struct window_geom geom = client->geom;

	if (pac == POINTER_ACTION_RESIZE_SIDE) {
		/* coordinates relative to the window */
		int16_t x = pos.x - geom.x;
		int16_t y = pos.y - geom.y;
		bool left_of_a = (x * geom.height) < (geom.width * y);
		bool left_of_b = ((geom.width - x) * geom.height) > (geom.width * y);

		/* Problem is that the above algorithm works in a 2d system
		   where the origin is in the bottom-left. */
		if (left_of_a) {
			if (left_of_b) {
				handle = HANDLE_LEFT;
			}
			else
				handle = HANDLE_BOTTOM;
		} else {
			if (left_of_b)
				handle = HANDLE_TOP;
			else
				handle = HANDLE_RIGHT;
		}
	} else if (pac == POINTER_ACTION_RESIZE_CORNER) {
		int16_t mid_x = geom.x + geom.width / 2;
		int16_t mid_y = geom.y + geom.height / 2;

		if (pos.y < mid_y) {
			if (pos.x < mid_x)
				handle = HANDLE_TOP_LEFT;
			else
				handle = HANDLE_TOP_RIGHT;
		} else {
			if (pos.x < mid_x)
				handle = HANDLE_BOTTOM_LEFT;
			else
				handle = HANDLE_BOTTOM_RIGHT;
		}
	} else {
		handle = HANDLE_TOP_LEFT;
	}

	return handle;
}
static void
track_pointer(struct client *client, enum pointer_action pac, xcb_point_t pos)
{
	enum resize_handle handle = get_handle(client, pos, pac);
	struct window_geom geom = client->geom;

	xcb_generic_event_t *ev = NULL;

	bool grabbing = true;
	struct client *grabbed = client;

	if (client == NULL)
		return;

	do {
		free(ev);
		while ((ev = xcb_wait_for_event(conn)) == NULL)
			xcb_flush(conn);
		uint8_t resp = EVENT_MASK(ev->response_type);

		if (resp == XCB_MOTION_NOTIFY) {
			xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;
			DMSG("tracking window by mouse root_x = %d  root_y = %d  posx = %d  posy = %d\n", e->root_x, e->root_y, pos.x, pos.y);
			int16_t dx = e->root_x - pos.x;
			int16_t dy = e->root_y - pos.y;
			int32_t x = client->geom.x, y = client->geom.y,
				width = client->geom.width, height = client->geom.height;

			if (pac == POINTER_ACTION_MOVE) {
				client->geom.x = geom.x + dx;
				client->geom.y = geom.y + dy;
				teleport_window(client->window, client->geom.x, client->geom.y);
			} else if (pac == POINTER_ACTION_RESIZE_SIDE || pac == POINTER_ACTION_RESIZE_CORNER) {

				DMSG("dx: %d\tdy: %d\n", dx, dy);
				if (conf.resize_hints) {
					dx /= client->width_inc;
					dx *= client->width_inc;

					dy /= client->width_inc;
					dy *= client->width_inc;
					DMSG("we have resize hints\tdx: %d\tdy: %d\n", dx, dy);
				}
				/* oh boy */
				switch (handle) {
				case HANDLE_LEFT:
					x = geom.x + dx;
					width = geom.width - dx;
					break;
				case HANDLE_BOTTOM:
					height  = geom.height + dy;
					break;
				case HANDLE_TOP:
					y = geom.y + dy;
					height = geom.height - dy;
					break;
				case HANDLE_RIGHT:
					width = geom.width + dx;
					break;

				case HANDLE_TOP_LEFT:
					y = geom.y + dy;
					height = geom.height - dy;
					x = geom.x + dx;
					width = geom.width - dx;
					break;
				case HANDLE_TOP_RIGHT:
					y = geom.y + dy;
					height = geom.height - dy;
					width = geom.width + dx;
					break;
				case HANDLE_BOTTOM_LEFT:
					x = geom.x + dx;
					width = geom.width - dx;
					height = geom.height + dy;
					break;
				case HANDLE_BOTTOM_RIGHT:
					width = geom.width + dx;
					height = geom.height + dy;
					break;
				}

				/* check for overflow */
				if (width < client->min_width) {
					width = client->min_width;
					x = client->geom.x;
				}

				if (height < client->min_height) {
					height = client->min_height;
					y = client->geom.y;
				}

				DMSG("moving by %d %d\n", x - geom.x, y - geom.y);
				DMSG("resizing by %d %d\n", width - geom.width, height - geom.height);
				client->geom.x = x;
				client->geom.width = width;
				client->geom.height = height;
				client->geom.y = y;

				resize_window_absolute(client->window, client->geom.width, client->geom.height);
				teleport_window(client->window, client->geom.x, client->geom.y);
				xcb_flush(conn);
			}
		} else if (resp == XCB_BUTTON_RELEASE) {

			grabbing = false;
		} else {
			if (events[resp] != NULL)
				(events[resp])(ev);
		}
	} while (grabbing && grabbed != NULL);
	free(ev);

	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
}

static void
grab_buttons(void)
{
	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		window_grab_buttons(client->window);
	}
}

static void
ungrab_buttons(void)
{
	struct list_item *item;
	struct client *client;

	for (item = win_list; item != NULL; item = item->next) {
		client = item->data;
		xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, client->window, XCB_MOD_MASK_ANY);
	}
}

static void
usage(char *name)
{
	fprintf(stderr, "Usage: %s [-h|-v|-c CONFIG_PATH]\n", name);

	exit(EXIT_SUCCESS);
}

static void version(void)
{
	fprintf(stderr, "%s %s\n", __NAME__, __THIS_VERSION__);
	fprintf(stderr, "Copyright (c) 2016-2019 Tudor Ioan Roman\n");
	fprintf(stderr, "Released under the ISC License\n");

	exit(EXIT_SUCCESS);
}

static void
load_defaults(void)
{
	conf.border_width    = BORDER_WIDTH;
	conf.focus_color     = COLOR_FOCUS;
	conf.unfocus_color   = COLOR_UNFOCUS;
	conf.internal_border_width    = INTERNAL_BORDER_WIDTH;
	conf.internal_focus_color     = INTERNAL_COLOR_FOCUS;
	conf.internal_unfocus_color   = INTERNAL_COLOR_UNFOCUS;
	conf.gap_left = conf.gap_down
		= conf.gap_up = conf.gap_right = GAP;
	conf.grid_gap        = GRID_GAP;
	conf.cursor_position = CURSOR_POSITION;
	conf.groups          = GROUPS;
	conf.sloppy_focus    = SLOPPY_FOCUS;
	conf.resize_hints    = RESIZE_HINTS;
	conf.sticky_windows  = STICKY_WINDOWS;
	conf.borders         = BORDERS;
	conf.last_window_focusing = LAST_WINDOW_FOCUSING;
	conf.apply_settings       = APPLY_SETTINGS;
	conf.replay_click_on_focus = REPLAY_CLICK_ON_FOCUS;
	conf.pointer_actions[BUTTON_LEFT]   = DEFAULT_LEFT_BUTTON_ACTION;
	conf.pointer_actions[BUTTON_MIDDLE] = DEFAULT_MIDDLE_BUTTON_ACTION;
	conf.pointer_actions[BUTTON_RIGHT]  = DEFAULT_RIGHT_BUTTON_ACTION;
	conf.pointer_modifier = POINTER_MODIFIER;
	conf.click_to_focus = CLICK_TO_FOCUS_BUTTON;
}

static void
load_config(char *config_path)
{
	int f = fork();
	if (f == 0) {
		setsid();
		DMSG("loading %s\n", config_path);
		execl(config_path, config_path, NULL);
		err(EXIT_FAILURE, "couldn't load config file");
	} else if (f == -1) {
		err(EXIT_FAILURE, NULL);
	}
}

void
handle_child(int sig)
{
	if (sig == SIGCHLD) {
		wait(NULL);
	}
}

int main(int argc, char *argv[])
{
	int opt;
	char *config_path = malloc(MAXLEN * sizeof(char));
	config_path[0] = '\0';
	while ((opt = getopt(argc, argv, "hvc:")) != -1) {
		switch (opt) {
			case 'h':
				usage(argv[0]);
				break;
			case 'c':
				snprintf(config_path, MAXLEN * sizeof(char), "%s", optarg);
				break;
			case 'v':
				version();
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

	signal(SIGCHLD, handle_child);

	/* execute config file */
	load_config(config_path);
	run();

	free(config_path);

	return exit_code;
}
