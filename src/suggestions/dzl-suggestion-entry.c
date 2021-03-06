/* dzl-suggestion-entry.c
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

#define G_LOG_DOMAIN "dzl-suggestion-entry"

#include <glib/gi18n.h>

#include "dzl-suggestion.h"
#include "dzl-suggestion-entry.h"
#include "dzl-suggestion-entry-buffer.h"
#include "dzl-suggestion-popover.h"

#if 0
# define _TRACE_LEVEL (1<<G_LOG_LEVEL_USER_SHIFT)
# define _TRACE(...) do { g_log(G_LOG_DOMAIN, _TRACE_LEVEL, __VA_ARGS__); } while (0)
# define DZL_TRACE_MSG(m,...) _TRACE("   MSG: %s():%u: "m, G_STRFUNC, __LINE__, __VA_ARGS__)
# define DZL_ENTRY _TRACE(" ENTRY: %s(): %u", G_STRFUNC, __LINE__)
# define DZL_EXIT do { _TRACE("  EXIT: %s(): %u", G_STRFUNC, __LINE__); return; } while (0)
# define DZL_RETURN(r) do { _TRACE("  EXIT: %s(): %u", G_STRFUNC, __LINE__); return (r); } while (0)
# define DZL_GOTO(_l) do { _TRACE("  GOTO: %s(): %u: %s", G_STRFUNC, __LINE__, #_l); goto _l; } while (0)
#else
# define DZL_TRACE_MSG(m,...) do { } while (0)
# define DZL_ENTRY            do { } while (0)
# define DZL_EXIT             return
# define DZL_RETURN(r)        return (r)
# define DZL_GOTO(_l)         goto _l
#endif

typedef struct
{
  DzlSuggestionPopover     *popover;
  DzlSuggestionEntryBuffer *buffer;
  GListModel               *model;
  gulong                    changed_handler;
} DzlSuggestionEntryPrivate;

enum {
  PROP_0,
  PROP_MODEL,
  PROP_TYPED_TEXT,
  N_PROPS
};

enum {
  ACTIVATE_SUGGESTION,
  HIDE_SUGGESTIONS,
  MOVE_SUGGESTION,
  SHOW_SUGGESTIONS,
  SUGGESTION_ACTIVATED,
  N_SIGNALS
};

static void buildable_iface_init (GtkBuildableIface    *iface);
static void editable_iface_init  (GtkEditableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (DzlSuggestionEntry, dzl_suggestion_entry, GTK_TYPE_ENTRY,
                         G_ADD_PRIVATE (DzlSuggestionEntry)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE, editable_iface_init))

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];
static guint changed_signal_id;
static GtkEditableInterface *editable_parent_iface;

static void
dzl_suggestion_entry_show_suggestions (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  dzl_suggestion_popover_popup (priv->popover);

  DZL_EXIT;
}

static void
dzl_suggestion_entry_hide_suggestions (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  dzl_suggestion_popover_popdown (priv->popover);

  DZL_EXIT;
}

static void
dzl_suggestion_entry_init_default_css (void)
{
  g_autoptr(GtkCssProvider) css_provider = NULL;
  static gsize initialized;

  if (g_once_init_enter (&initialized))
    {
      css_provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (css_provider,
                                           "/org/gnome/dazzle/dzl-suggestion-entry.css");
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                 GTK_STYLE_PROVIDER (css_provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION - 1);
      g_once_init_leave (&initialized, TRUE);
    }
}

static gboolean
dzl_suggestion_entry_focus_out_event (GtkWidget     *widget,
                                      GdkEventFocus *event)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)widget;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));
  g_assert (event != NULL);

  g_signal_emit (self, signals [HIDE_SUGGESTIONS], 0);

  return GTK_WIDGET_CLASS (dzl_suggestion_entry_parent_class)->focus_out_event (widget, event);
}

static void
dzl_suggestion_entry_hierarchy_changed (GtkWidget *widget,
                                        GtkWidget *old_toplevel)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)widget;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));
  g_assert (!old_toplevel || GTK_IS_WIDGET (old_toplevel));

  if (priv->popover != NULL)
    {
      GtkWidget *toplevel = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);

      gtk_window_set_transient_for (GTK_WINDOW (priv->popover), GTK_WINDOW (toplevel));
    }
}

static gboolean
dzl_suggestion_entry_key_press_event (GtkWidget   *widget,
                                      GdkEventKey *key)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)widget;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  /*
   * If Tab was pressed, and there is uncommitted suggested text,
   * commit it and stop propagation of the key press.
   */
  if (key->keyval == GDK_KEY_Tab && (key->state & GDK_MODIFIER_MASK) == 0)
    {
      const gchar *typed_text;
      DzlSuggestion *suggestion;

      typed_text = dzl_suggestion_entry_buffer_get_typed_text (priv->buffer);
      suggestion = dzl_suggestion_popover_get_selected (priv->popover);

      if (typed_text != NULL && suggestion != NULL)
        {
          g_autofree gchar *replace = dzl_suggestion_replace_typed_text (suggestion, typed_text);

          g_signal_handler_block (self, priv->changed_handler);

          if (replace != NULL)
            gtk_entry_set_text (GTK_ENTRY (self), replace);
          else
            dzl_suggestion_entry_buffer_commit (priv->buffer);
          gtk_editable_set_position (GTK_EDITABLE (self), -1);

          g_signal_handler_unblock (self, priv->changed_handler);

          return GDK_EVENT_STOP;
        }
    }

  return GTK_WIDGET_CLASS (dzl_suggestion_entry_parent_class)->key_press_event (widget, key);
}

static void
dzl_suggestion_entry_update_attrs (DzlSuggestionEntry *self)
{
  PangoAttribute *attr;
  PangoAttrList *list;
  const gchar *typed_text;
  const gchar *text;
  GdkRGBA rgba;

  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  gdk_rgba_parse (&rgba, "#666666");

  text = gtk_entry_get_text (GTK_ENTRY (self));
  typed_text = dzl_suggestion_entry_get_typed_text (self);

  list = pango_attr_list_new ();
  attr = pango_attr_foreground_new (rgba.red * 0xFFFF, rgba.green * 0xFFFF, rgba.blue * 0xFFFF);
  attr->start_index = strlen (typed_text);
  attr->end_index = strlen (text);
  pango_attr_list_insert (list, attr);
  gtk_entry_set_attributes (GTK_ENTRY (self), list);
  pango_attr_list_unref (list);

  DZL_EXIT;
}

static void
dzl_suggestion_entry_changed (GtkEditable *editable)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)editable;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);
  DzlSuggestion *suggestion;
  const gchar *text;

  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  /*
   * If we aren't focused, just ignore everything. One such example might be
   * updating an URI in a webbrowser.
   */
  if (!gtk_widget_has_focus (GTK_WIDGET (editable)))
    return;

  g_signal_handler_block (self, priv->changed_handler);

  text = dzl_suggestion_entry_buffer_get_typed_text (priv->buffer);

  if (text == NULL || *text == '\0')
    {
      g_signal_emit (self, signals [HIDE_SUGGESTIONS], 0);
      DZL_GOTO (finish);
    }

  g_signal_emit (self, signals [SHOW_SUGGESTIONS], 0);

  suggestion = dzl_suggestion_popover_get_selected (priv->popover);

  if (suggestion != NULL)
    {
      g_object_ref (suggestion);
      dzl_suggestion_entry_buffer_set_suggestion (priv->buffer, suggestion);
      g_object_unref (suggestion);
    }

finish:
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TYPED_TEXT]);

  g_signal_handler_unblock (self, priv->changed_handler);

  dzl_suggestion_entry_update_attrs (self);

  DZL_EXIT;
}

static void
dzl_suggestion_entry_suggestion_activated (DzlSuggestionEntry   *self,
                                           DzlSuggestion        *suggestion,
                                           DzlSuggestionPopover *popover)
{
  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));
  g_assert (DZL_IS_SUGGESTION (suggestion));
  g_assert (DZL_IS_SUGGESTION_POPOVER (popover));

  g_signal_emit (self, signals [SUGGESTION_ACTIVATED], 0, suggestion);
  g_signal_emit (self, signals [HIDE_SUGGESTIONS], 0);
  gtk_entry_set_text (GTK_ENTRY (self), "");

  DZL_EXIT;
}

static void
dzl_suggestion_entry_move_suggestion (DzlSuggestionEntry *self,
                                      gint                amount)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  dzl_suggestion_popover_move_by (priv->popover, amount);
}

static void
dzl_suggestion_entry_activate_suggestion (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  DZL_ENTRY;

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  dzl_suggestion_popover_activate_selected (priv->popover);

  DZL_EXIT;
}

static void
dzl_suggestion_entry_constructed (GObject *object)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)object;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  G_OBJECT_CLASS (dzl_suggestion_entry_parent_class)->constructed (object);

  gtk_entry_set_buffer (GTK_ENTRY (self), GTK_ENTRY_BUFFER (priv->buffer));
}

static void
dzl_suggestion_entry_destroy (GtkWidget *widget)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)widget;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  if (priv->popover != NULL)
    gtk_widget_destroy (GTK_WIDGET (priv->popover));

  g_clear_object (&priv->model);

  GTK_WIDGET_CLASS (dzl_suggestion_entry_parent_class)->destroy (widget);

  g_assert (priv->popover == NULL);
}

static void
dzl_suggestion_entry_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  DzlSuggestionEntry *self = DZL_SUGGESTION_ENTRY (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, dzl_suggestion_entry_get_model (self));
      break;

    case PROP_TYPED_TEXT:
      g_value_set_string (value, dzl_suggestion_entry_get_typed_text (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
dzl_suggestion_entry_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  DzlSuggestionEntry *self = DZL_SUGGESTION_ENTRY (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      dzl_suggestion_entry_set_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
dzl_suggestion_entry_class_init (DzlSuggestionEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkBindingSet *bindings;

  object_class->constructed = dzl_suggestion_entry_constructed;
  object_class->get_property = dzl_suggestion_entry_get_property;
  object_class->set_property = dzl_suggestion_entry_set_property;

  widget_class->destroy = dzl_suggestion_entry_destroy;
  widget_class->focus_out_event = dzl_suggestion_entry_focus_out_event;
  widget_class->hierarchy_changed = dzl_suggestion_entry_hierarchy_changed;
  widget_class->key_press_event = dzl_suggestion_entry_key_press_event;

  klass->hide_suggestions = dzl_suggestion_entry_hide_suggestions;
  klass->show_suggestions = dzl_suggestion_entry_show_suggestions;
  klass->move_suggestion = dzl_suggestion_entry_move_suggestion;

  properties [PROP_MODEL] =
    g_param_spec_object ("model",
                         "Model",
                         "The model to be visualized",
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TYPED_TEXT] =
    g_param_spec_string ("typed-text",
                         "Typed Text",
                         "Typed text into the entry, does not include suggested text",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [HIDE_SUGGESTIONS] =
    g_signal_new ("hide-suggestions",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (DzlSuggestionEntryClass, hide_suggestions),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * DzlSuggestionEntry::move-suggestion:
   * @self: A #DzlSuggestionEntry
   * @amount: The number of items to move
   *
   * This moves the selected suggestion in the popover by the value
   * provided. -1 moves up one row, 1, moves down a row.
   */
  signals [MOVE_SUGGESTION] =
    g_signal_new ("move-suggestion",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (DzlSuggestionEntryClass, move_suggestion),
                  NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);

  signals [SHOW_SUGGESTIONS] =
    g_signal_new ("show-suggestions",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (DzlSuggestionEntryClass, show_suggestions),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [SUGGESTION_ACTIVATED] =
    g_signal_new ("suggestion-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (DzlSuggestionEntryClass, suggestion_activated),
                  NULL, NULL, NULL, G_TYPE_NONE, 1, DZL_TYPE_SUGGESTION);

  widget_class->activate_signal = signals [ACTIVATE_SUGGESTION] =
    g_signal_new_class_handler ("activate-suggestion",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (dzl_suggestion_entry_activate_suggestion),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  bindings = gtk_binding_set_by_class (klass);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Escape, 0, "hide-suggestions", 0);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_space, GDK_CONTROL_MASK, "show-suggestions", 0);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Up, 0, "move-suggestion", 1, G_TYPE_INT, -1);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Down, 0, "move-suggestion", 1, G_TYPE_INT, 1);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Page_Up, 0, "move-suggestion", 1, G_TYPE_INT, -10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_KP_Page_Up, 0, "move-suggestion", 1, G_TYPE_INT, -10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Prior, 0, "move-suggestion", 1, G_TYPE_INT, -10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Next, 0, "move-suggestion", 1, G_TYPE_INT, 10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Page_Down, 0, "move-suggestion", 1, G_TYPE_INT, 10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_KP_Page_Down, 0, "move-suggestion", 1, G_TYPE_INT, 10);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Return, 0, "activate-suggestion", 0);

  changed_signal_id = g_signal_lookup ("changed", GTK_TYPE_ENTRY);
}

static void
dzl_suggestion_entry_init (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  dzl_suggestion_entry_init_default_css ();

  priv->changed_handler =
    g_signal_connect_after (self,
                            "changed",
                            G_CALLBACK (dzl_suggestion_entry_changed),
                            NULL);

  priv->popover = g_object_new (DZL_TYPE_SUGGESTION_POPOVER,
                                "destroy-with-parent", TRUE,
                                "modal", FALSE,
                                "relative-to", self,
                                "type", GTK_WINDOW_POPUP,
                                NULL);
  g_signal_connect_object (priv->popover,
                           "suggestion-activated",
                           G_CALLBACK (dzl_suggestion_entry_suggestion_activated),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect (priv->popover,
                    "destroy",
                    G_CALLBACK (gtk_widget_destroyed),
                    &priv->popover);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "suggestion");

  priv->buffer = dzl_suggestion_entry_buffer_new ();
}

GtkWidget *
dzl_suggestion_entry_new (void)
{
  return g_object_new (DZL_TYPE_SUGGESTION_ENTRY, NULL);
}

static GObject *
dzl_suggestion_entry_get_internal_child (GtkBuildable *buildable,
                                         GtkBuilder   *builder,
                                         const gchar  *childname)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)buildable;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  if (g_strcmp0 (childname, "popover") == 0)
    return G_OBJECT (priv->popover);

  return NULL;
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  iface->get_internal_child = dzl_suggestion_entry_get_internal_child;
}

static void
dzl_suggestion_entry_set_selection_bounds (GtkEditable *editable,
                                           gint         start_pos,
                                           gint         end_pos)
{
  DzlSuggestionEntry *self = (DzlSuggestionEntry *)editable;
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_assert (DZL_IS_SUGGESTION_ENTRY (self));

  g_signal_handler_block (self, priv->changed_handler);

  if (end_pos < 0)
    end_pos = gtk_entry_buffer_get_length (GTK_ENTRY_BUFFER (priv->buffer));

  if (end_pos > (gint)dzl_suggestion_entry_buffer_get_typed_length (priv->buffer))
    dzl_suggestion_entry_buffer_commit (priv->buffer);

  editable_parent_iface->set_selection_bounds (editable, start_pos, end_pos);

  g_signal_handler_unblock (self, priv->changed_handler);
}

static void
editable_iface_init (GtkEditableInterface *iface)
{
  editable_parent_iface = g_type_interface_peek_parent (iface);

  iface->set_selection_bounds = dzl_suggestion_entry_set_selection_bounds;
}


/**
 * dzl_suggestion_entry_get_model:
 * @self: a #DzlSuggestionEntry
 *
 * Gets the model being visualized.
 *
 * Returns: (nullable) (transfer none): A #GListModel or %NULL.
 */
GListModel *
dzl_suggestion_entry_get_model (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SUGGESTION_ENTRY (self), NULL);

  return priv->model;
}

void
dzl_suggestion_entry_set_model (DzlSuggestionEntry *self,
                                GListModel         *model)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  DZL_ENTRY;

  g_return_if_fail (DZL_IS_SUGGESTION_ENTRY (self));
  g_return_if_fail (!model || g_type_is_a (g_list_model_get_item_type (model), DZL_TYPE_SUGGESTION));

  if (g_set_object (&priv->model, model))
    {
      DZL_TRACE_MSG ("Model has %u items",
                     model ? g_list_model_get_n_items (model) : 0);
      dzl_suggestion_popover_set_model (priv->popover, model);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODEL]);
      dzl_suggestion_entry_update_attrs (self);
    }

  DZL_EXIT;
}

/**
 * dzl_suggestion_entry_get_suggestion:
 * @self: a #DzlSuggestionEntry
 *
 * Gets the currently selected suggestion.
 *
 * Returns: (nullable) (transfer none): An #DzlSuggestion or %NULL.
 */
DzlSuggestion *
dzl_suggestion_entry_get_suggestion (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SUGGESTION_ENTRY (self), NULL);

  return dzl_suggestion_popover_get_selected (priv->popover);
}

void
dzl_suggestion_entry_set_suggestion (DzlSuggestionEntry *self,
                                     DzlSuggestion      *suggestion)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  DZL_ENTRY;

  g_return_if_fail (DZL_IS_SUGGESTION_ENTRY (self));
  g_return_if_fail (!suggestion || DZL_IS_SUGGESTION_ENTRY (suggestion));

  dzl_suggestion_popover_set_selected (priv->popover, suggestion);
  dzl_suggestion_entry_buffer_set_suggestion (priv->buffer, suggestion);

  DZL_EXIT;
}

const gchar *
dzl_suggestion_entry_get_typed_text (DzlSuggestionEntry *self)
{
  DzlSuggestionEntryPrivate *priv = dzl_suggestion_entry_get_instance_private (self);

  g_return_val_if_fail (DZL_IS_SUGGESTION_ENTRY (self), NULL);

  return dzl_suggestion_entry_buffer_get_typed_text (priv->buffer);
}
