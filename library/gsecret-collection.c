/* GSecret - GLib wrapper for Secret Service
 *
 * Copyright 2012 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#include "config.h"

#include "gsecret-collection.h"
#include "gsecret-dbus-generated.h"
#include "gsecret-item.h"
#include "gsecret-private.h"
#include "gsecret-service.h"
#include "gsecret-types.h"

#include <glib/gi18n-lib.h>

enum {
	PROP_0,
	PROP_SERVICE,
	PROP_ITEMS,
	PROP_LABEL,
	PROP_LOCKED,
	PROP_CREATED,
	PROP_MODIFIED
};

struct _GSecretCollectionPrivate {
	/* Doesn't change between construct and finalize */
	GSecretService *service;
	GCancellable *cancellable;
	gboolean constructing;

	/* Protected by mutex */
	GMutex mutex;
	GHashTable *items;
};

static GInitableIface *gsecret_collection_initable_parent_iface = NULL;

static GAsyncInitableIface *gsecret_collection_async_initable_parent_iface = NULL;

static void   gsecret_collection_initable_iface         (GInitableIface *iface);

static void   gsecret_collection_async_initable_iface   (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GSecretCollection, gsecret_collection, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gsecret_collection_initable_iface);
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, gsecret_collection_async_initable_iface);
);

static GHashTable *
items_table_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
	                              g_free, g_object_unref);
}

static void
gsecret_collection_init (GSecretCollection *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, GSECRET_TYPE_COLLECTION,
	                                        GSecretCollectionPrivate);

	g_mutex_init (&self->pv->mutex);
	self->pv->cancellable = g_cancellable_new ();
	self->pv->items = items_table_new ();
	self->pv->constructing = TRUE;
}

static void
on_set_label (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSecretCollection *self = GSECRET_COLLECTION (user_data);
	GError *error = NULL;

	gsecret_collection_set_label_finish (self, result, &error);
	if (error != NULL) {
		g_warning ("couldn't set GSecretCollection Label: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (self);
}

static void
gsecret_collection_set_property (GObject *obj,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	GSecretCollection *self = GSECRET_COLLECTION (obj);

	switch (prop_id) {
	case PROP_SERVICE:
		g_return_if_fail (self->pv->service == NULL);
		self->pv->service = g_value_get_object (value);
		if (self->pv->service)
			g_object_add_weak_pointer (G_OBJECT (self->pv->service),
			                           (gpointer *)&self->pv->service);
		break;
	case PROP_LABEL:
		gsecret_collection_set_label (self, g_value_get_string (value),
		                              self->pv->cancellable, on_set_label,
		                              g_object_ref (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gsecret_collection_get_property (GObject *obj,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	GSecretCollection *self = GSECRET_COLLECTION (obj);

	switch (prop_id) {
	case PROP_SERVICE:
		g_value_set_object (value, self->pv->service);
		break;
	case PROP_ITEMS:
		g_value_take_boxed (value, gsecret_collection_get_items (self));
		break;
	case PROP_LABEL:
		g_value_take_string (value, gsecret_collection_get_label (self));
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, gsecret_collection_get_locked (self));
		break;
	case PROP_CREATED:
		g_value_set_uint64 (value, gsecret_collection_get_created (self));
		break;
	case PROP_MODIFIED:
		g_value_set_uint64 (value, gsecret_collection_get_modified (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gsecret_collection_dispose (GObject *obj)
{
	GSecretCollection *self = GSECRET_COLLECTION (obj);

	g_cancellable_cancel (self->pv->cancellable);

	G_OBJECT_CLASS (gsecret_collection_parent_class)->dispose (obj);
}

static void
gsecret_collection_finalize (GObject *obj)
{
	GSecretCollection *self = GSECRET_COLLECTION (obj);

	if (self->pv->service)
		g_object_remove_weak_pointer (G_OBJECT (self->pv->service),
		                              (gpointer *)&self->pv->service);

	g_mutex_clear (&self->pv->mutex);
	g_hash_table_destroy (self->pv->items);
	g_object_unref (self->pv->cancellable);

	G_OBJECT_CLASS (gsecret_collection_parent_class)->finalize (obj);
}

static GSecretItem *
collection_lookup_item (GSecretCollection *self,
                        const gchar *path)
{
	GSecretItem *item = NULL;

	g_mutex_lock (&self->pv->mutex);

	item = g_hash_table_lookup (self->pv->items, path);
	if (item != NULL)
		g_object_ref (item);

	g_mutex_unlock (&self->pv->mutex);

	return item;
}

static void
collection_update_items (GSecretCollection *self,
                         GHashTable *items)
{
	GHashTable *previous;

	g_hash_table_ref (items);

	g_mutex_lock (&self->pv->mutex);
	previous = self->pv->items;
	self->pv->items = items;
	g_mutex_unlock (&self->pv->mutex);

	g_hash_table_unref (previous);
}

typedef struct {
	GCancellable *cancellable;
	GHashTable *items;
	gint items_loading;
} ItemsClosure;

static void
items_closure_free (gpointer data)
{
	ItemsClosure *closure = data;
	g_clear_object (&closure->cancellable);
	g_hash_table_unref (closure->items);
	g_slice_free (ItemsClosure, closure);
}

static void
on_load_item (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	ItemsClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GSecretCollection *self = GSECRET_COLLECTION (g_async_result_get_source_object (user_data));
	const gchar *path;
	GError *error = NULL;
	GSecretItem *item;

	closure->items_loading--;

	item = gsecret_item_new_finish (result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	if (item != NULL) {
		path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (item));
		g_hash_table_insert (closure->items, g_strdup (path), item);
	}

	if (closure->items_loading == 0) {
		collection_update_items (self, closure->items);
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (self);
	g_object_unref (res);
}

static void
collection_load_items_async (GSecretCollection *self,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	ItemsClosure *closure;
	GSecretItem *item;
	GSimpleAsyncResult *res;
	const gchar *path;
	GVariant *paths;
	GVariantIter iter;

	paths = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Items");
	g_return_if_fail (paths != NULL);

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 collection_load_items_async);
	closure = g_slice_new0 (ItemsClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	closure->items = items_table_new ();
	g_simple_async_result_set_op_res_gpointer (res, closure, items_closure_free);

	g_variant_iter_init (&iter, paths);
	while (g_variant_iter_loop (&iter, "&o", &path)) {
		item = collection_lookup_item (self, path);

		/* No such collection yet create a new one */
		if (item == NULL) {
			gsecret_item_new (self->pv->service, path, cancellable,
			                  on_load_item, g_object_ref (res));
			closure->items_loading++;

		} else {
			g_hash_table_insert (closure->items, g_strdup (path), item);
		}
	}

	if (closure->items_loading == 0) {
		collection_update_items (self, closure->items);
		g_simple_async_result_complete_in_idle (res);
	}

	g_variant_unref (paths);
	g_object_unref (res);
}

static gboolean
collection_load_items_finish (GSecretCollection *self,
                              GAsyncResult *result,
                              GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}


static gboolean
collection_load_items_sync (GSecretCollection *self,
                            GCancellable *cancellable,
                            GError **error)
{
	GSecretItem *item;
	GHashTable *items;
	GVariant *paths;
	GVariantIter iter;
	const gchar *path;
	gboolean ret = TRUE;

	paths = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Items");
	g_return_val_if_fail (paths != NULL, FALSE);

	items = items_table_new ();

	g_variant_iter_init (&iter, paths);
	while (g_variant_iter_next (&iter, "&o", &path)) {
		item = collection_lookup_item (self, path);

		/* No such collection yet create a new one */
		if (item == NULL) {
			item = gsecret_item_new_sync (self->pv->service, path,
			                              cancellable, error);
			if (item == NULL) {
				ret = FALSE;
				break;
			}
		}

		g_hash_table_insert (items, g_strdup (path), item);
	}

	if (ret)
		collection_update_items (self, items);

	g_hash_table_unref (items);
	g_variant_unref (paths);
	return ret;
}

static void
handle_property_changed (GSecretCollection *self,
                         const gchar *property_name,
                         GVariant *value)
{
	if (g_str_equal (property_name, "Label"))
		g_object_notify (G_OBJECT (self), "label");

	else if (g_str_equal (property_name, "Locked"))
		g_object_notify (G_OBJECT (self), "locked");

	else if (g_str_equal (property_name, "Created"))
		g_object_notify (G_OBJECT (self), "created");

	else if (g_str_equal (property_name, "Modified"))
		g_object_notify (G_OBJECT (self), "modified");

	else if (g_str_equal (property_name, "Items") && !self->pv->constructing)
		collection_load_items_async (self, self->pv->cancellable, NULL, NULL);
}

static void
gsecret_collection_properties_changed (GDBusProxy *proxy,
                                       GVariant *changed_properties,
                                       const gchar* const *invalidated_properties)
{
	GSecretCollection *self = GSECRET_COLLECTION (proxy);
	gchar *property_name;
	GVariantIter iter;
	GVariant *value;

	g_object_freeze_notify (G_OBJECT (self));

	g_variant_iter_init (&iter, changed_properties);
	while (g_variant_iter_loop (&iter, "{sv}", &property_name, &value))
		// TODO: zzzz;
		handle_property_changed (self, property_name, value);

	g_object_thaw_notify (G_OBJECT (self));
}

static void
gsecret_collection_class_init (GSecretCollectionClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GDBusProxyClass *proxy_class = G_DBUS_PROXY_CLASS (klass);

	gobject_class->get_property = gsecret_collection_get_property;
	gobject_class->set_property = gsecret_collection_set_property;
	gobject_class->dispose = gsecret_collection_dispose;
	gobject_class->finalize = gsecret_collection_finalize;

	proxy_class->g_properties_changed = gsecret_collection_properties_changed;

	g_object_class_install_property (gobject_class, PROP_SERVICE,
	            g_param_spec_object ("service", "Service", "Secret Service",
	                                 GSECRET_TYPE_SERVICE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_ITEMS,
	             g_param_spec_boxed ("items", "Items", "Items in collection",
	                                 _gsecret_list_get_type (), G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_LABEL,
	            g_param_spec_string ("label", "Label", "Item label",
	                                 NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_LOCKED,
	           g_param_spec_boolean ("locked", "Locked", "Item locked",
	                                 TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_CREATED,
	            g_param_spec_uint64 ("created", "Created", "Item creation date",
	                                 0UL, G_MAXUINT64, 0UL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_MODIFIED,
	            g_param_spec_uint64 ("modified", "Modified", "Item modified date",
	                                 0UL, G_MAXUINT64, 0UL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (gobject_class, sizeof (GSecretCollectionPrivate));
}

static gboolean
gsecret_collection_initable_init (GInitable *initable,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GSecretCollection *self;
	GDBusProxy *proxy;

	if (!gsecret_collection_initable_parent_iface->init (initable, cancellable, error))
		return FALSE;

	proxy = G_DBUS_PROXY (initable);

	if (!_gsecret_util_have_cached_properties (proxy)) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
		             "No such secret collection at path: %s",
		             g_dbus_proxy_get_object_path (proxy));
		return FALSE;
	}

	self = GSECRET_COLLECTION (initable);

	if (!collection_load_items_sync (self, cancellable, error))
		return FALSE;

	return TRUE;
}

static void
gsecret_collection_initable_iface (GInitableIface *iface)
{
	gsecret_collection_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init = gsecret_collection_initable_init;
}

typedef struct {
	GCancellable *cancellable;
} InitClosure;

static void
init_closure_free (gpointer data)
{
	InitClosure *closure = data;
	g_clear_object (&closure->cancellable);
	g_slice_free (InitClosure, closure);
}

static void
on_init_items (GObject *source,
               GAsyncResult *result,
               gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GSecretCollection *self = GSECRET_COLLECTION (source);
	GError *error = NULL;

	if (!collection_load_items_finish (self, result, &error))
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_init_base (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GSecretCollection *self = GSECRET_COLLECTION (source);
	InitClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GDBusProxy *proxy = G_DBUS_PROXY (self);
	GError *error = NULL;

	if (!gsecret_collection_async_initable_parent_iface->init_finish (G_ASYNC_INITABLE (self),
	                                                                  result, &error)) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else if (!_gsecret_util_have_cached_properties (proxy)) {
		g_simple_async_result_set_error (res, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
		                                 "No such secret collection at path: %s",
		                                 g_dbus_proxy_get_object_path (proxy));
		g_simple_async_result_complete (res);

	} else {
		collection_load_items_async (self, closure->cancellable,
		                             on_init_items, g_object_ref (res));
	}

	g_object_unref (res);
}

static void
gsecret_collection_async_initable_init_async (GAsyncInitable *initable,
                                              int io_priority,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data)
{
	GSimpleAsyncResult *res;
	InitClosure *closure;

	res = g_simple_async_result_new (G_OBJECT (initable), callback, user_data,
	                                 gsecret_collection_async_initable_init_async);
	closure = g_slice_new0 (InitClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, init_closure_free);

	gsecret_collection_async_initable_parent_iface->init_async (initable, io_priority,
	                                                            cancellable,
	                                                            on_init_base,
	                                                            g_object_ref (res));

	g_object_unref (res);
}

static gboolean
gsecret_collection_async_initable_init_finish (GAsyncInitable *initable,
                                               GAsyncResult *result,
                                               GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (initable),
	                      gsecret_collection_async_initable_init_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
gsecret_collection_async_initable_iface (GAsyncInitableIface *iface)
{
	gsecret_collection_async_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init_async = gsecret_collection_async_initable_init_async;
	iface->init_finish = gsecret_collection_async_initable_init_finish;
}

void
gsecret_collection_new (GSecretService *service,
                        const gchar *collection_path,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GDBusProxy *proxy;

	g_return_if_fail (GSECRET_IS_SERVICE (service));
	g_return_if_fail (collection_path != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	proxy = G_DBUS_PROXY (service);

	g_async_initable_new_async (GSECRET_SERVICE_GET_CLASS (service)->collection_gtype,
	                            G_PRIORITY_DEFAULT, cancellable, callback, user_data,
	                            "g-flags", G_DBUS_CALL_FLAGS_NONE,
	                            "g-interface-info", _gsecret_gen_collection_interface_info (),
	                            "g-name", g_dbus_proxy_get_name (proxy),
	                            "g-connection", g_dbus_proxy_get_connection (proxy),
	                            "g-object-path", collection_path,
	                            "g-interface-name", GSECRET_COLLECTION_INTERFACE,
	                            "service", service,
	                            NULL);
}

GSecretCollection *
gsecret_collection_new_finish (GAsyncResult *result,
                               GError **error)
{
	GObject *source_object;
	GObject *object;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	source_object = g_async_result_get_source_object (result);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
	                                      result, error);
	g_object_unref (source_object);

	if (object == NULL)
		return NULL;

	return GSECRET_COLLECTION (object);
}

GSecretCollection *
gsecret_collection_new_sync (GSecretService *service,
                             const gchar *collection_path,
                             GCancellable *cancellable,
                             GError **error)
{
	GDBusProxy *proxy;

	g_return_val_if_fail (GSECRET_IS_SERVICE (service), NULL);
	g_return_val_if_fail (collection_path != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	proxy = G_DBUS_PROXY (service);

	return g_initable_new (GSECRET_SERVICE_GET_CLASS (service)->collection_gtype,
	                       cancellable, error,
	                       "g-flags", G_DBUS_CALL_FLAGS_NONE,
	                       "g-interface-info", _gsecret_gen_collection_interface_info (),
	                       "g-name", g_dbus_proxy_get_name (proxy),
	                       "g-connection", g_dbus_proxy_get_connection (proxy),
	                       "g-object-path", collection_path,
	                       "g-interface-name", GSECRET_COLLECTION_INTERFACE,
	                       "service", service,
	                       NULL);
}

void
gsecret_collection_refresh (GSecretCollection *self)
{
	g_return_if_fail (GSECRET_IS_COLLECTION (self));

	_gsecret_util_get_properties (G_DBUS_PROXY (self),
	                              gsecret_collection_refresh,
	                              self->pv->cancellable, NULL, NULL);
}

void
gsecret_collection_delete (GSecretCollection *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	const gchar *object_path;

	g_return_if_fail (GSECRET_IS_COLLECTION (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (self));
	gsecret_service_delete_path (self->pv->service, object_path, cancellable,
	                             callback, user_data);
}

gboolean
gsecret_collection_delete_finish (GSecretCollection *self,
                                  GAsyncResult *result,
                                  GError **error)
{
	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return gsecret_service_delete_path_finish (self->pv->service, result, error);
}

gboolean
gsecret_collection_delete_sync (GSecretCollection *self,
                                GCancellable *cancellable,
                                GError **error)
{
	GSecretSync *sync;
	gboolean ret;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	sync = _gsecret_sync_new ();
	g_main_context_push_thread_default (sync->context);

	gsecret_collection_delete (self, cancellable, _gsecret_sync_on_result, sync);

	g_main_loop_run (sync->loop);

	ret = gsecret_collection_delete_finish (self, sync->result, error);

	g_main_context_pop_thread_default (sync->context);
	_gsecret_sync_free (sync);

	return ret;
}

GList *
gsecret_collection_get_items (GSecretCollection *self)
{
	GList *l, *items;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), NULL);

	g_mutex_lock (&self->pv->mutex);
	items = g_hash_table_get_values (self->pv->items);
	for (l = items; l != NULL; l = g_list_next (l))
		g_object_ref (l->data);
	g_mutex_unlock (&self->pv->mutex);

	return items;
}

GSecretItem *
_gsecret_collection_find_item_instance (GSecretCollection *self,
                                        const gchar *item_path)
{
	GSecretItem *item;

	g_mutex_lock (&self->pv->mutex);
	item = g_hash_table_lookup (self->pv->items, item_path);
	if (item != NULL)
		g_object_ref (item);
	g_mutex_unlock (&self->pv->mutex);

	return item;
}

gchar *
gsecret_collection_get_label (GSecretCollection *self)
{
	GVariant *variant;
	gchar *label;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Label");
	g_return_val_if_fail (variant != NULL, NULL);

	label = g_variant_dup_string (variant, NULL);
	g_variant_unref (variant);

	return label;
}

void
gsecret_collection_set_label (GSecretCollection *self,
                              const gchar *label,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	g_return_if_fail (GSECRET_IS_COLLECTION (self));
	g_return_if_fail (label != NULL);

	_gsecret_util_set_property (G_DBUS_PROXY (self), "Label",
	                            g_variant_new_string (label),
	                            gsecret_collection_set_label,
	                            cancellable, callback, user_data);
}

gboolean
gsecret_collection_set_label_finish (GSecretCollection *self,
                                     GAsyncResult *result,
                                     GError **error)
{
	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), FALSE);

	return _gsecret_util_set_property_finish (G_DBUS_PROXY (self),
	                                          gsecret_collection_set_label,
	                                          result, error);
}

gboolean
gsecret_collection_set_label_sync (GSecretCollection *self,
                                   const gchar *label,
                                   GCancellable *cancellable,
                                   GError **error)
{
	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	return _gsecret_util_set_property_sync (G_DBUS_PROXY (self), "Label",
	                                        g_variant_new_string (label),
	                                        cancellable, error);
}

gboolean
gsecret_collection_get_locked (GSecretCollection *self)
{
	GVariant *variant;
	gboolean locked;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Locked");
	g_return_val_if_fail (variant != NULL, TRUE);

	locked = g_variant_get_boolean (variant);
	g_variant_unref (variant);

	return locked;
}

guint64
gsecret_collection_get_created (GSecretCollection *self)
{
	GVariant *variant;
	guint64 created;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Created");
	g_return_val_if_fail (variant != NULL, 0);

	created = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return created;
}

guint64
gsecret_collection_get_modified (GSecretCollection *self)
{
	GVariant *variant;
	guint64 modified;

	g_return_val_if_fail (GSECRET_IS_COLLECTION (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Modified");
	g_return_val_if_fail (variant != NULL, 0);

	modified = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return modified;
}
