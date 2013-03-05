/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */


#include "config.h"

#include "mock-service.h"

#include "secret-private.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static GTestDBus *test_bus = NULL;
static GPid pid = 0;

static gboolean
service_start (const gchar *mock_script,
               GError **error)
{
	gchar ready[8] = { 0, };
	GSpawnFlags flags;
	int wait_pipe[2];
	GPollFD poll_fd;
	gboolean ret;
	gint polled;

	gchar *argv[] = {
		"python", (gchar *)mock_script,
		"--name", MOCK_SERVICE_NAME,
		"--ready", ready,
		NULL
	};

	g_return_val_if_fail (mock_script != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);
	g_test_dbus_up (test_bus);

	g_setenv ("SECRET_SERVICE_BUS_NAME", MOCK_SERVICE_NAME, TRUE);

	if (pipe (wait_pipe) < 0) {
		g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
		                     "Couldn't create pipe for mock service");
		return FALSE;
	}

	snprintf (ready, sizeof (ready), "%d", wait_pipe[1]);

	flags = G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN;
	ret = g_spawn_async (SRCDIR, argv, NULL, flags, NULL, NULL, &pid, error);

	close (wait_pipe[1]);

	if (ret) {
		poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
		poll_fd.fd = wait_pipe[0];
		poll_fd.revents = 0;

		polled = g_poll (&poll_fd, 1, 2000);
		if (polled < -1)
			g_warning ("couldn't poll file descirptor: %s", g_strerror (errno));
		if (polled != 1)
			g_warning ("couldn't wait for mock service");
	}

	close (wait_pipe[0]);
	return ret;
}

gboolean
mock_service_start (const gchar *mock_script,
                    GError **error)
{
	gchar *path;
	gboolean ret;

	path = g_build_filename (SRCDIR, "libsecret", mock_script, NULL);
	ret = service_start (path, error);
	g_free (path);

	return ret;
}

void
mock_service_stop (void)
{
	const gchar *prgname;

	if (!pid)
		return;

	if (kill (pid, SIGTERM) < 0) {
		if (errno != ESRCH)
			g_warning ("kill() failed: %s", g_strerror (errno));
	}

	g_spawn_close_pid (pid);
	pid = 0;

	while (g_main_context_iteration (NULL, FALSE));

	/*
	 * HACK: Don't worry about leaking tests when running under gjs.
	 * Waiting for the connection to go away is hopeless due to
	 * the way gjs garbage collects.
	 */
	prgname = g_get_prgname ();
	if (prgname && strstr (prgname, "gjs")) {
		g_test_dbus_stop (test_bus);

	} else {
		g_test_dbus_down (test_bus);
		g_object_unref (test_bus);
	}
	test_bus = NULL;
}