/* ----------------------------------------------------------------------- *
 *
 *  module.c - common module-management functions
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include "automount.h"
#include "nsswitch.h"

int open_lookup(const char *name, const char *err_prefix, const char *mapfmt,
		int argc, const char *const *argv, struct lookup_mod **lookup)
{
	struct lookup_mod *mod;
	char buf[MAX_ERR_BUF];
	char fnbuf[PATH_MAX];
	size_t size;
	char *type;
	void *dh;
	int *ver;

	*lookup = NULL;

	mod = malloc(sizeof(struct lookup_mod));
	if (!mod) {
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NSS_STATUS_UNKNOWN;
	}

	type = strdup(name);
	if (!type) {
		free(mod);
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NSS_STATUS_UNKNOWN;
	}

	size = snprintf(fnbuf, sizeof(fnbuf),
			"%s/lookup_%s.so", AUTOFS_LIB_DIR, name);
	if (size >= sizeof(fnbuf)) {
		free(mod);
		free(type);
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NSS_STATUS_UNKNOWN;
	}

	if (!(dh = dlopen(fnbuf, RTLD_NOW))) {
		if (err_prefix)
			logerr("%scannot open lookup module %s (%s)",
			       err_prefix, name, dlerror());
		free(mod);
		free(type);
		return NSS_STATUS_UNKNOWN;
	}

	if (!(ver = (int *) dlsym(dh, "lookup_version"))
	    || *ver != AUTOFS_LOOKUP_VERSION) {
		if (err_prefix)
			logerr("%slookup module %s version mismatch",
			     err_prefix, name);
		dlclose(dh);
		free(mod);
		free(type);
		return NSS_STATUS_UNKNOWN;
	}

	if (!(mod->lookup_init = (lookup_init_t) dlsym(dh, "lookup_init")) ||
	    !(mod->lookup_reinit = (lookup_reinit_t) dlsym(dh, "lookup_reinit")) ||
	    !(mod->lookup_read_master = (lookup_read_master_t) dlsym(dh, "lookup_read_master")) ||
	    !(mod->lookup_read_map = (lookup_read_map_t) dlsym(dh, "lookup_read_map")) ||
	    !(mod->lookup_mount = (lookup_mount_t) dlsym(dh, "lookup_mount")) ||
	    !(mod->lookup_done = (lookup_done_t) dlsym(dh, "lookup_done"))) {
		if (err_prefix)
			logerr("%slookup module %s corrupt", err_prefix, name);
		dlclose(dh);
		free(mod);
		free(type);
		return NSS_STATUS_UNKNOWN;
	}

	if (mod->lookup_init(mapfmt, argc, argv, &mod->context)) {
		dlclose(dh);
		free(mod);
		free(type);
		return NSS_STATUS_UNKNOWN;
	}

	mod->type = type;
	mod->dlhandle = dh;
	*lookup = mod;

	return NSS_STATUS_SUCCESS;
}

int reinit_lookup(struct lookup_mod *mod, const char *name,
		  const char *err_prefix, const char *mapfmt,
		  int argc, const char *const *argv)
{
	if (mod->lookup_reinit(mapfmt, argc, argv, &mod->context)) {
		if (err_prefix)
			logerr("%scould not reinit lookup module %s",
			       err_prefix, name);
		return 1;
	}
	return 0;
}

int close_lookup(struct lookup_mod *mod)
{
	int rv = mod->lookup_done(mod->context);
	dlclose(mod->dlhandle);
	free(mod->type);
	free(mod);
	return rv;
}

struct parse_mod *open_parse(const char *name, const char *err_prefix,
			     int argc, const char *const *argv)
{
	struct parse_mod *mod;
	char buf[MAX_ERR_BUF];
	char fnbuf[PATH_MAX];
	size_t size;
	void *dh;
	int *ver;


	mod = malloc(sizeof(struct parse_mod));
	if (!mod) {
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NULL;
	}

	size = snprintf(fnbuf, sizeof(fnbuf),
			"%s/parse_%s.so", AUTOFS_LIB_DIR, name);
	if (size >= sizeof(fnbuf)) {
		free(mod);
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NULL;
	}

	if (!(dh = dlopen(fnbuf, RTLD_NOW))) {
		if (err_prefix)
			logerr("%scannot open parse module %s (%s)",
			     err_prefix, name, dlerror());
		free(mod);
		return NULL;
	}

	if (!(ver = (int *) dlsym(dh, "parse_version"))
	    || *ver != AUTOFS_PARSE_VERSION) {
		if (err_prefix)
			logerr("%sparse module %s version mismatch",
			     err_prefix, name);
		dlclose(dh);
		free(mod);
		return NULL;
	}

	if (!(mod->parse_init = (parse_init_t) dlsym(dh, "parse_init")) ||
	    !(mod->parse_reinit = (parse_reinit_t) dlsym(dh, "parse_reinit")) ||
	    !(mod->parse_mount = (parse_mount_t) dlsym(dh, "parse_mount")) ||
	    !(mod->parse_done = (parse_done_t) dlsym(dh, "parse_done"))) {
		if (err_prefix)
			logerr("%sparse module %s corrupt",
			     err_prefix, name);
		dlclose(dh);
		free(mod);
		return NULL;
	}

	if (mod->parse_init(argc, argv, &mod->context)) {
		dlclose(dh);
		free(mod);
		return NULL;
	}
	mod->dlhandle = dh;
	return mod;
}

int reinit_parse(struct parse_mod *mod, const char *name,
		 const char *err_prefix, int argc, const char *const *argv)
{
	if (mod->parse_reinit(argc, argv, &mod->context)) {
		if (err_prefix)
			logerr("%scould not reinit parse module %s",
			       err_prefix, name);
		return 1;
	}
	return 0;
}

int close_parse(struct parse_mod *mod)
{
	int rv = mod->parse_done(mod->context);
	dlclose(mod->dlhandle);
	free(mod);
	return rv;
}

struct mount_mod *open_mount(const char *name, const char *err_prefix)
{
	struct mount_mod *mod;
	char buf[MAX_ERR_BUF];
	char fnbuf[PATH_MAX];
	size_t size;
	void *dh;
	int *ver;


	mod = malloc(sizeof(struct mount_mod));
	if (!mod) {
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NULL;
	}

	size = snprintf(fnbuf, sizeof(fnbuf),
			"%s/mount_%s.so", AUTOFS_LIB_DIR, name);
	if (size >= sizeof(fnbuf)) {
		free(mod);
		if (err_prefix) {
			char *estr = strerror_r(errno, buf, MAX_ERR_BUF);
			logerr("%s%s", err_prefix, estr);
		}
		return NULL;
	}

	if (!(dh = dlopen(fnbuf, RTLD_NOW))) {
		if (err_prefix)
			logerr("%scannot open mount module %s (%s)",
			     err_prefix, name, dlerror());
		free(mod);
		return NULL;
	}

	if (!(ver = (int *) dlsym(dh, "mount_version"))
	    || *ver != AUTOFS_MOUNT_VERSION) {
		if (err_prefix)
			logerr("%smount module %s version mismatch",
			     err_prefix, name);
		dlclose(dh);
		free(mod);
		return NULL;
	}

	if (!(mod->mount_init = (mount_init_t) dlsym(dh, "mount_init")) ||
	    !(mod->mount_reinit = (mount_reinit_t) dlsym(dh, "mount_reinit")) ||
	    !(mod->mount_mount = (mount_mount_t) dlsym(dh, "mount_mount")) ||
	    !(mod->mount_done = (mount_done_t) dlsym(dh, "mount_done"))) {
		if (err_prefix)
			logerr("%smount module %s corrupt",
			     err_prefix, name);
		dlclose(dh);
		free(mod);
		return NULL;
	}

	if (mod->mount_init(&mod->context)) {
		dlclose(dh);
		free(mod);
		return NULL;
	}
	mod->dlhandle = dh;
	return mod;
}

int reinit_mount(struct mount_mod *mod, const char *name, const char *err_prefix)
{
	if (mod->mount_reinit(&mod->context)) {
		if (err_prefix)
			logerr("%scould not reinit mount module %s",
			       err_prefix, name);
		return 1;
	}
	return 0;
}

int close_mount(struct mount_mod *mod)
{
	int rv = mod->mount_done(mod->context);
	dlclose(mod->dlhandle);
	free(mod);
	return rv;
}
