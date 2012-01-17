/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stat_common.h 109983 2011-10-21 11:41:42Z e.kurganov $
 */

#include <time.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
//#include <osreldate.h>

#include "conf.h"


time_t get_remote_tm(void);
int sysctl_get_by_name(const char *, void *, size_t, int);

