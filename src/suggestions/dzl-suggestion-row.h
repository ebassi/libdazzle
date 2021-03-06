/* dzl-suggestion-row.h
 *
 * Copyright (C) 2017 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DZL_SUGGESTION_ROW_H
#define DZL_SUGGESTION_ROW_H

#include <gtk/gtk.h>

#include "dzl-suggestion.h"

G_BEGIN_DECLS

#define DZL_TYPE_SUGGESTION_ROW (dzl_suggestion_row_get_type())

G_DECLARE_DERIVABLE_TYPE (DzlSuggestionRow, dzl_suggestion_row, DZL, SUGGESTION_ROW, GtkListBoxRow)

struct _DzlSuggestionRowClass
{
  GtkListBoxRowClass parent_class;

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
};

GtkWidget     *dzl_suggestion_row_new            (void);
DzlSuggestion *dzl_suggestion_row_get_suggestion (DzlSuggestionRow *self);
void           dzl_suggestion_row_set_suggestion (DzlSuggestionRow *self,
                                                  DzlSuggestion    *suggestion);

G_END_DECLS

#endif /* DZL_SUGGESTION_ROW_H */
