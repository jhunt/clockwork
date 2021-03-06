/*
  Copyright 2011-2015 James Hunt <james@jameshunt.us>

  This file is part of Clockwork.

  Clockwork is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Clockwork is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Clockwork.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RESOURCES_H
#define RESOURCES_H

#include "clockwork.h"

#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>

#include "resource.h"
#include "policy.h"

#define NEW_RESOURCE(t) \
void*          res_ ## t ## _new(const char *key); \
void*          res_ ## t ## _clone(const void *res, const char *key); \
void           res_ ## t ## _free(void *res); \
char*          res_ ## t ## _key(const void *res); \
int            res_ ## t ## _gencode(const void *res, FILE *io); \
int            res_ ## t ## _attrs(const void *res, hash_t *attrs); \
int            res_ ## t ## _norm(void *res, struct policy *pol, hash_t *facts); \
int            res_ ## t ## _set(void *res, const char *attr, const char *value); \
int            res_ ## t ## _match(const void *res, const char *attr, const char *value); \
content_t*     res_ ## t ## _content(const void *res, hash_t *facts)

#define RES_NONE 0x00

#define RES_USER_ABSENT   0x80000000
#define RES_USER_NAME     0x0001
#define RES_USER_PASSWD   0x0002
#define RES_USER_UID      0x0004
#define RES_USER_GID      0x0008
#define RES_USER_GECOS    0x0010
#define RES_USER_DIR      0x0020
#define RES_USER_SHELL    0x0040
#define RES_USER_MKHOME   0x0080
#define RES_USER_PWMIN    0x0100
#define RES_USER_PWMAX    0x0200
#define RES_USER_PWWARN   0x0400
#define RES_USER_INACT    0x0800
#define RES_USER_EXPIRE   0x1000
#define RES_USER_LOCK     0x2000

/**
  A system user.
 */
struct res_user {
	char *key;       /* unique identifier, starts with "res_user:" */

	char *name;   /* username */
	char *passwd; /* encrypted password for /etc/shadow */
	uid_t uid;    /* numeric user ID */
	gid_t gid;    /* numeric group ID (primary) */
	char *gecos;  /* comment (GECOS) field */
	char *shell;  /* path to the user's login shell */
	char *dir;    /* path to user's home directory */
	char *skel;   /* path to home directory skeleton */

	unsigned char mkhome; /* should we make the home directory? */
	unsigned char lock;   /* is the account locked? */

	/* These members match struct spwd; cf. getspnam(3) */
	long pwmin;  /* minimum number of days between password changes */
	long pwmax;  /* maximum password age (in days) */
	long pwwarn; /* number of days before password change to warn user */
	long inact;  /* disable expired accounts after X days */
	long expire; /* when account expires (days since 1/1/70) */

	unsigned int enforced;  /* enforcements, see RES_USER_* constants */
};

NEW_RESOURCE(user);


#define RES_GROUP_ABSENT   0x80000000
#define RES_GROUP_NAME     0x01
#define RES_GROUP_PASSWD   0x02
#define RES_GROUP_GID      0x04
#define RES_GROUP_MEMBERS  0x08
#define RES_GROUP_ADMINS   0x10

/**
  A system group.
 */
struct res_group {
	char *key;       /* unique identifier, starts with "res_group:" */

	char *name;   /* group name */
	char *passwd; /* encrypted group password */
	gid_t gid;    /* numeric group ID */

	strings_t *mem_add;  /* users that should be in this group */
	strings_t *mem_rm;   /* users that should not be in this group */

	strings_t *adm_add;  /* users that should be admins */
	strings_t *adm_rm;   /* users that should not be admins */

	unsigned int enforced;   /* enforcements; see RES_GROUP_* constants */
};

NEW_RESOURCE(group);

int res_group_enforce_members(struct res_group *rg, int enforce);
int res_group_add_member(struct res_group *rg, const char *user);
int res_group_remove_member(struct res_group *rg, const char *user);

int res_group_enforce_admins(struct res_group *rg, int enforce);
int res_group_add_admin(struct res_group *rg, const char *user);
int res_group_remove_admin(struct res_group *rg, const char *user);



#define RES_FILE_ABSENT   0x80000000
#define RES_FILE_UID      0x01
#define RES_FILE_GID      0x02
#define RES_FILE_MODE     0x04
#define RES_FILE_SHA1     0x08

/**
  A File
 */
struct res_file {
	char *key;      /* unique identifier, starts with "res_file:" */

	char *path;     /* path to the file */
	char *tmpfile;  /* path to the tempfile (for pre-change verify) */
	char *source;   /* source of file contents, as a static file */
	char *template; /* source of file contents, as a template */

	char *verify;   /* command to run pre-change verfication */
	int  expectrc;  /* expected return code of verify command */
	int  cache;     /* whether or not to cache file contents locally */

	char  *owner;   /* name of the file's user owner */
	uid_t  uid;     /* UID of the file's user owner */
	char  *group;   /* name of the file's group owner */
	gid_t  gid;     /* GID of the file's group owner */
	mode_t mode;    /* permissions / mode */

	unsigned int enforced;  /* enforcements; see RES_FILE_* constants */
};

NEW_RESOURCE(file);


/**
  Symbolic link
 */
struct res_symlink {
	char *key;    /* unique identifier, starts with "res_symlink:" */
	char *path;   /* path to the link itself */
	char *target; /* target the link should point to */
	int   absent; /* link should be removed */
};

NEW_RESOURCE(symlink);


#define RES_PACKAGE_ABSENT   0x80000000

/**
  A software package resource.
 */
struct res_package {
	char *key;        /* unique identifier; starts with "res_package:" */

	char *name;       /* name of the package */
	char *version;    /* version to be installed */

	unsigned int latest;
	unsigned int enforced;  /* enforcements; see RES_PACKAGE_* constants */
};

NEW_RESOURCE(package);


#define RES_SERVICE_RUNNING   0x0001
#define RES_SERVICE_STOPPED   0x0002
#define RES_SERVICE_ENABLED   0x0004
#define RES_SERVICE_DISABLED  0x0010

/**
  A system init service.
 */
struct res_service {
	char *key;     /* unique identifier; starts with "res_service:" */

	char *service; /* name of the script in /etc/init.d */
	char *notify;     /* init script action to use for notify */

	unsigned int enforced;  /* enforcemnts; see RES_SERVICE_* constants */
};

NEW_RESOURCE(service);

#define RES_HOST_ABSENT   0x80000000
#define RES_HOST_ALIASES  0x01

struct res_host {
	char *key;

	char *hostname;
	char *ip;
	strings_t *aliases;

	unsigned int enforced;
};

NEW_RESOURCE(host);

#define RES_DIR_ABSENT  0x80000000
#define RES_DIR_MODE    0x01
#define RES_DIR_UID     0x02
#define RES_DIR_GID     0x04

struct res_dir {
	char *key;

	char *path;

	char *owner;
	uid_t uid;

	char *group;
	gid_t gid;

	mode_t mode;

	unsigned int enforced;
};

NEW_RESOURCE(dir);

#define RES_EXEC_NEEDSRUN  0x80000000
#define RES_EXEC_UID       0x02
#define RES_EXEC_GID       0x04
#define RES_EXEC_ONDEMAND  0x08
#define RES_EXEC_TEST      0x10

struct res_exec {
	char *key;
	char *command;
	char *test;

	/* run `command' as this user/UID */
	char *user;
	uid_t uid;

	/* run `command' as this group/GID */
	char *group;
	gid_t gid;

	unsigned int enforced;
};

NEW_RESOURCE(exec);

#undef NEW_RESOURCE

#endif
