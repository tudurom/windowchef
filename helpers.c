/* Copyright (c) 2016-2018 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "helpers.h"

int
asprintf(char **buf, const char *fmt, ...)
{
	int size = 0;
	va_list args;
	va_start(args, fmt);
	size = vasprintf(buf, fmt, args);
	va_end(args);
	return size;
}

int
vasprintf(char **buf, const char *fmt, va_list args)
{
	va_list tmp;
	va_copy(tmp, args);
	int size = vsnprintf(NULL, 0, fmt, tmp);
	va_end(tmp);

	if (size < 0) {
		return -1;
	}

	*buf = malloc(size + 1);

	if (*buf == NULL) {
		return -1;
	}

	size = vsprintf(*buf, fmt, args);
	return size;
}

/*
 * Move list item to the beginning (head) of the list.
 */

void
list_move_to_head(struct list_item **list, struct list_item *item)
{
	if (list == NULL || *list == NULL || *list == item || item == NULL)
		return;

	/* fill the hole */
	if (item->prev != NULL)
	    item->prev->next = item->next;
	if (item->next != NULL)
	    item->next->prev = item->prev;

	/* we are at the head, nothing is behind us now */
	item->prev = NULL;
	item->next = *list;
	item->next->prev = item;
	*list = item;
}

/*
 * Allocate item at the head of the list.
 */

struct list_item*
list_add_item(struct list_item **list)
{
	struct list_item *item;

	item = malloc(sizeof(struct list_item));
	if (item == NULL)
		return NULL;

	if (*list == NULL) {
		item->prev = item->next = NULL;
	} else {
		item->prev = NULL;
		item->next = *list;
		item->next->prev = item;
	}

	*list = item;

	return item;
}

/*
 * Delete item from the list.
 * Data must be freed manually.
 */

void
list_delete_item(struct list_item **list, struct list_item *item)
{
	if (list == NULL || *list == NULL || item == NULL)
		return;

	if (*list == item) {
		*list = item->next;
	} else {
		item->prev->next = item->next;
		if (item->next != NULL)
			item->next->prev = item->prev;
	}

	free(item);
}

/*
 * Deletes a whole list.
 */

void
list_delete_all_items(struct list_item **list, bool can_free_data)
{
	struct list_item *item, *next;

	for (item = *list; item->next != NULL; item = next) {
		next = item->next;
		if (can_free_data)
			free(item->data);
		list_delete_item(list, item);
	}
}
