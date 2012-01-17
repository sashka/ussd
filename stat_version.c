/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id$
 */

#include "stat_common.h"


/*****************************************************************************
 * Processes VERSION command.
 *****************************************************************************/
void stat_version() {
	time_t tm;

	msg_debug(1, "Processing of VERSION command started");

	tm = get_remote_tm();
	printf("%lu version %u%02u%02u\n", (u_long)tm, (u_int)MAJOR_VERSION,
	    (u_int)MINOR_VERSION, (u_int)REVISION);

	msg_debug(1, "Processing of VERSION command finished");
}

