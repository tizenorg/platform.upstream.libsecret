/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2012 Stef Walter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#if !defined (__SECRET_INSIDE_HEADER__) && !defined (SECRET_COMPILATION)
#error "Only <secret/secret.h> or <secret/secret-unstable.h> can be included directly."
#endif

#ifndef __SECRET_SCHEMAS_H__
#define __SECRET_SCHEMAS_H__

#include <glib.h>

#include "secret-schema.h"

G_BEGIN_DECLS

/*
 * This schema is here for compatibility with libgnome-keyring's network
 * password functions.
 */

extern const SecretSchema *  SECRET_SCHEMA_COMPAT_NETWORK;

G_END_DECLS

#endif /* __SECRET_SCHEMAS_H___ */
