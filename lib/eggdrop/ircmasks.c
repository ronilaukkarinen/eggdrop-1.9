/* ircmasks.c: functions for working with ircmask lists
 *
 * Copyright (C) 2002, 2003, 2004 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef lint
static const char rcsid[] = "$Id: ircmasks.c,v 1.6 2004/06/21 18:44:20 wingman Exp $";
#endif

#include <string.h>				/* strlen		*/
#include <stdlib.h>				/* free			*/

#include <eggdrop/logging.h>			/* putlog		*/
#include <eggdrop/match.h>			/* wild_match		*/
#include <eggdrop/ircmasks.h>			/* ircmask_t		*/

int ircmask_list_add(ircmask_list_t *list, const char *ircmask, void *data)
{
	int i, stars, len;
	ircmask_list_entry_t *entry, *prev;

	stars = 0;
	len = strlen(ircmask);
	for (i = 0; i < len; i++) {
		if (ircmask[i] == '*') stars++;
	}
	len -= stars;
	prev = NULL;
	for (entry = list->head; entry; entry = entry->next) {
		if (len >= entry->len) break;
		prev = entry;
	}
	entry = malloc(sizeof(*entry));
	entry->ircmask = strdup(ircmask);
	entry->len = len;
	entry->data = data;
	if (prev) {
		entry->next = prev->next;
		prev->next = entry;
	}
	else {
		entry->next = list->head;
		list->head = entry;
	}
	return(0);
}

int ircmask_list_del(ircmask_list_t *list, const char *ircmask, void *data)
{
	ircmask_list_entry_t *entry, *prev;

	prev = NULL;
	for (entry = list->head; entry; entry = entry->next) {
		if (!strcasecmp(ircmask, entry->ircmask)) break;
		prev = entry;
	}
	if (entry) {
		if (prev) prev->next = entry->next;
		else list->head = entry->next;
		free(entry->ircmask);
		free(entry);
	}
	return(0);
}

int ircmask_list_find(ircmask_list_t *list, const char *irchost, void *dataptr)
{
	ircmask_list_entry_t *entry;

	for (entry = list->head; entry; entry = entry->next) {
		if (wild_match(entry->ircmask, irchost) > 0) {
			*(void **)dataptr = entry->data;
			return(0);
		}
	}
	*(void **)dataptr = NULL;
	return(-1);
}
