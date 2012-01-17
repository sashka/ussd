/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id$
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include "stat_common.h"


/* Requested sysctl variables */
char *sysctl_vars[SYSCTL_MAXN];

/* Number of elements in %sysctl_vars% array */
u_int sysctl_n = 0;

#ifndef __linux__

#define OUTPUT_VAR(FORMAT, TYPE) {						\
	if (size != sizeof(TYPE)) {						\
		msg_err(0, "%s: Size of variable '%s' (%llu) differs from "	\
		    "size of it's type (%llu)", __FUNCTION__, var,		\
		    (u_llong)size, (u_llong)sizeof(TYPE));			\
		continue;							\
	}									\
	printf("%lu sysctl_%s "FORMAT"\n", (u_long)tm, var, *(TYPE *)valbuf);	\
}

/*****************************************************************************
 * Processes SYSCTL command.
 *****************************************************************************/
void stat_sysctl() {
	time_t tm;
	u_int i, type;
	int mib[CTL_MAXNAME + 2];
	char *var, fmtbuf[BUFSIZ], *fmt, valbuf[BUFSIZ];
	size_t mib_len, size;

	msg_debug(1, "Processing of SYSCTL command started");

	for (i = 0; i < sysctl_n; i++) {
		var = sysctl_vars[i];
		msg_debug(2, "%s: Processing variable '%s'", __FUNCTION__, var);

		/* translate variable to MIB */
		mib_len = CTL_MAXNAME;
		if (sysctlnametomib(var, &mib[2], &mib_len) < 0) {
			msg_syserr(0, "%s: sysctlnametomib(%s)", __FUNCTION__, var);
			continue;
		}

		/* get type and format of variable (undocumented) */
		mib[0] = 0;
		mib[1] = 4;
		size = sizeof(fmtbuf);
		if (sysctl(mib, mib_len + 2, fmtbuf, &size, NULL, 0) < 0) {
			msg_syserr(0, "%s: sysctl(0,4,%s)", __FUNCTION__, var);
			continue;
		}
		type = *(u_int *)fmtbuf & CTLTYPE;
		fmt = fmtbuf + sizeof(u_int);
		msg_debug(2, "%s: Variable '%s' has type=%u, format='%s'", __FUNCTION__,
		    var, type, fmt);

		/* skip non-numeric or string variables */
		if (!((type == CTLTYPE_INT || type == CTLTYPE_UINT ||
		    type == CTLTYPE_LONG || type == CTLTYPE_ULONG
		    || type == CTLTYPE_STRING
#ifdef CTLTYPE_QUAD
		    || type == CTLTYPE_QUAD
#endif
#ifdef CTLTYPE_S64
		    || type == CTLTYPE_S64
#endif
#ifdef CTLTYPE_U64
		    || type == CTLTYPE_U64
#endif
		    ) &&
		    (strcmp(fmt, "I") == 0 || strcmp(fmt, "IU") == 0 ||
		    strcmp(fmt, "L") == 0 || strcmp(fmt, "LU") == 0 ||
		    strcmp(fmt, "Q") == 0 || strcmp(fmt, "QU") == 0 ||
		    strcmp(fmt, "A") == 0 ))) {
			msg_err(0, "%s: Variable '%s' is not numeric or string", __FUNCTION__, var);
			continue;
		}

		/* get value of variable */
		size = sizeof(valbuf);
		if (sysctl(&mib[2], mib_len, valbuf, &size, NULL, 0) < 0) {
			msg_syserr(0, "%s: sysctl(%s)", __FUNCTION__, var);
			continue;
		}
		msg_debug(2, "%s: Variable '%s' has size=%llu", __FUNCTION__,
		    var, (u_llong)size);

		/* output result */
		tm = get_remote_tm();
		if	(strcmp(fmt, "I") == 0)
			OUTPUT_VAR("%d", int)
		else if (strcmp(fmt, "IU") == 0)
			OUTPUT_VAR("%u", u_int)
		else if (strcmp(fmt, "L") == 0)
			OUTPUT_VAR("%ld", long)
		else if (strcmp(fmt, "LU") == 0)
			OUTPUT_VAR("%lu", u_long)
		else if (strcmp(fmt, "Q") == 0)
			OUTPUT_VAR("%qd", llong)
		else if (strcmp(fmt, "QU") == 0)
			OUTPUT_VAR("%qu", u_llong)
		else if (strcmp(fmt, "A") == 0)
			printf("%lu sysctl_%s %.*s\n", (u_long)tm, var, (int) size, valbuf);
	}

	msg_debug(1, "Processing of SYSCTL command finished");
}

#endif // __linux__
