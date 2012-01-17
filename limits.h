/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: limits.h 112401 2012-01-12 12:57:01Z dark $
 */

#include <stdio.h>
#include <limits.h>

#include "vg_lib/vg_parse.h"


/* Maximum length of file name not including null */
#define FILENAME_MAXLEN		FILENAME_MAX - 1

/* Maximum length of unix domain socket name not including null */
#define SOCKNAME_MAXLEN		103

/* Maximum length of user and group strings passed via command line
   not including null */
#define USER_GROUP_MAXLEN	32

/* Maximum length of input line not including null */
#define INPUT_LINE_MAXLEN	1023

/* Maximum length of variable not including null */
#define VAR_MAXLEN		63

/* Possible characters in variable name */
#define VAR_CHSET		CHSET_ALPHA_ENG CHSET_DIGITS "-_"

/* Maximum length of shell command not including null */
#define SHELL_COMMAND_MAXLEN	511

/* Maximum number of interfaces */
#define IFACE_MAXN		32

/* Maximum number of ACPI thermal zones */
#define ACPI_TZ_MAXN		8

/* Maximum number of SYSCTL commands */
#define SYSCTL_MAXN		128

/* Maximum length of SYSCTL variable not including null */
#define SYSCTL_VAR_MAXLEN	63

/* Possible characters in SYSCTL variable name */
#define SYSCTL_VAR_CHSET	CHSET_ALPHA_ENG CHSET_DIGITS "._%"

/* Maximum number of 'apache' directives in config file */
#define APACHE_MAXN		8

/* Maximum number of 'nginx' directives in config file */
#define NGINX_MAXN		8

/* Maximum number of 'memcache' directives in config file */
#define MEMCACHE_MAXN		64

/* Maximum number of 'exec' directives in config file */
#define EXEC_MAXN		16

/* Maximum number of 'socket' directives in config file */
#define SOCKET_MAXN		64

