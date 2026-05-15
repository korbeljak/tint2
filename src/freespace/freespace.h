/**************************************************************************
* Copyright (C) 2011 Mishael A Sibiryakov (death@junki.org)
**************************************************************************/

#ifndef FREESPACE_H
#define FREESPACE_H

#include <stdbool.h>
#include "common.h"
#include "area.h"

typedef struct FreeSpace {
    Area area;
} FreeSpace;

struct Panel;

void cleanup_freespace(struct Panel *panel);
void init_freespace_panel(void *panel);

bool resize_freespace(void *obj);

#endif
