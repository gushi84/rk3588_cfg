/*
 * Notifier lists
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "notify.h"

void notifier_list_init(NotifierList *list)
{
    LIST_INIT(&list->notifiers);
}

void notifier_list_add(NotifierList *list, Notifier *notifier)
{
    LIST_INSERT_HEAD(&list->notifiers, notifier, node);
}

void notifier_list_remove(Notifier *notifier)
{
    LIST_REMOVE(notifier, node);
}

void notifier_list_notify(NotifierList *list)
{
    Notifier *notifier, *next;

    LIST_FOREACH_SAFE(notifier, &list->notifiers, node, next) {
        notifier->notify(notifier);
    }
}
