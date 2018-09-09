/* Copyright (c) 2016-2018 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for full license information. */

#ifndef WCHF_HELPERS_H
#define WCHF_HELPERS_H

#include <stdbool.h>
#include <stdarg.h>

int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, va_list);

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
