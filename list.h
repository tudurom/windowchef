/* See the LICENSE file for copyright and license details. */

#ifndef _LIST_H
#define _LIST_H

#include <stdbool.h>

struct list_item {
	void *data;
	struct list_item *prev;
	struct list_item *next;
};

void list_move_to_head(struct list_item **, struct list_item *);
struct list_item* list_add_item(struct list_item **);
void list_delete_item(struct list_item **, struct list_item *);
void list_delete_all_items(struct list_item **, bool);

#endif
