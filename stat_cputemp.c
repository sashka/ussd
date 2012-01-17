/*
 * Written by Konstantin Kukushkin <dark@rambler-co.ru>
 *
 * 	$Id$
 */

#include <sys/types.h>
#ifndef __linux__
#include <sys/sysctl.h>
#else
#include <dirent.h>
#define HWMONDIR "/sys/class/hwmon/"
#endif

#include <errno.h>

#include "stat_common.h"



/*****************************************************************************
 * Processes CPUTEMP command.
 *****************************************************************************/
void do_cputemp() {
	time_t tm;
	u_int i, cpus, type;
	int count = 0;
#ifndef __linux__
	int mib[CTL_MAXNAME + 2];
	char var[25], fmtbuf[BUFSIZ], *fmt, valbuf[BUFSIZ];
	size_t mib_len, size;
#else
	DIR * hwmon, * dev;
	FILE * fp;
	struct dirent * dp, * dp2;
	char fbuf[256], buf[1024];
	int j;
	char * p;
#endif
	float mintemp = 0, maxtemp = 0, avgtemp = 0, temp = 0;

	msg_debug(1, "Processing of CPUTEMP command started");

	/* detecting number of cpus */
#ifndef __linux__
	mib_len = CTL_MAXNAME;
	if (sysctlnametomib("hw.ncpu", &mib[2], &mib_len) < 0) {
		if (errno != ENOENT)
			msg_syserr(0, "%s: sysctlnametomib(hw.ncpu)", __FUNCTION__);
		return;
	}
	mib[0] = 0;
	mib[1] = 4;
	size = sizeof(fmtbuf);
	if (sysctl(mib, mib_len + 2, fmtbuf, &size, NULL, 0) < 0) {
		msg_syserr(0, "%s: sysctl(0,4,hw.ncpu)", __FUNCTION__);
		return;
	}
	type = *(u_int *)fmtbuf & CTLTYPE;
	fmt = fmtbuf + sizeof(u_int);
	if (!((type == CTLTYPE_INT || type == CTLTYPE_UINT) &&
	    (strcmp(fmt, "I") == 0 || strcmp(fmt, "IU") == 0))) {
		msg_err(0, "%s: Variable 'hw.ncpu' is not numeric", __FUNCTION__);
		return;
	}
	size = sizeof(valbuf);
	if (sysctl(&mib[2], mib_len, valbuf, &size, NULL, 0) < 0) {
		msg_syserr(0, "%s: sysctl(hw.ncpu)", __FUNCTION__);
		return;
	}
	if (fmt[1] == 'U')
		cpus = *(u_int *) valbuf;
	else
		cpus = *(int *) valbuf;
	if (cpus > 999)
		cpus = 999;

#else
	if ((hwmon = opendir(HWMONDIR)) == NULL) {
		if (errno == ENOENT)
			msg_debug(1, "No hwmon drivers!");
		else
			msg_err(0, "opendir(" HWMONDIR ")");
		return;
	}
#endif

#ifndef __linux__
	for (i = 0; i < cpus; i++) {
		snprintf(var, sizeof(var)-1, "dev.cpu.%u.temperature", i);
		msg_debug(2, "%s: Processing variable '%s'", __FUNCTION__, var);

		/* translate variable to MIB */
		mib_len = CTL_MAXNAME;
		if (sysctlnametomib(var, &mib[2], &mib_len) < 0) {
			if (errno != ENOENT)
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

		/* skip non-numeric variables */
		if (!((type == CTLTYPE_INT || type == CTLTYPE_UINT ||
		    type == CTLTYPE_LONG || type == CTLTYPE_ULONG
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
		    strcmp(fmt, "IK") == 0 ||
		    strcmp(fmt, "L") == 0 || strcmp(fmt, "LU") == 0 ||
		    strcmp(fmt, "Q") == 0 || strcmp(fmt, "QU") == 0))) {
			msg_err(0, "%s: Variable '%s' is not numeric", __FUNCTION__, var);
			continue;
		}
#else
	while((dp = readdir(hwmon)) != NULL) {
		if (sscanf(dp->d_name, "hwmon%d", &i) != 1)
			continue;
		snprintf(fbuf, sizeof(fbuf), HWMONDIR "hwmon%d/device/name", i);
		if ((fp=fopen(fbuf, "r")) == NULL) {
			msg_err(0, "fopen(hwmon%d/name)", i);
			continue;
		}
		if (fgets(buf, sizeof(buf), fp) == NULL) {
			fclose(fp);
			continue;
		}
		fclose(fp);
		parse_chomp(buf);
		if (strcmp(buf, "coretemp") && strcmp(buf, "via_cputemp") &&
		    strcmp(buf, "k8temp") && strcmp(buf, "k10temp"))
			continue;
		snprintf(fbuf, sizeof(fbuf), HWMONDIR "hwmon%d/device/", i);
		if ((dev=opendir(fbuf)) == NULL)
			continue;
#endif

		/* get value of variable */

#ifndef __linux__
		size = sizeof(valbuf);
		if (sysctl(&mib[2], mib_len, valbuf, &size, NULL, 0) < 0) {
			msg_syserr(0, "%s: sysctl(%s)", __FUNCTION__, var);
			continue;
		}
		msg_debug(2, "%s: Variable '%s' has size=%llu", __FUNCTION__,
		    var, (u_llong)size);

		/* output result */
		if	(strcmp(fmt, "I") == 0)
			temp = *(int *) valbuf;
		else if (strcmp(fmt, "IU") == 0)
			temp = *(u_int *) valbuf;
		else if	(strcmp(fmt, "IK") == 0) {
			temp = *(int *) valbuf;
			if (temp > 0)
				temp = (temp - 2732) / 10;
		}
#else
		while((dp2=readdir(dev)) != NULL) {
			if (!parse_get_str(dp2->d_name, &p, "temp") ||
			    !parse_get_int(p, &p, &j) ||
			    !parse_get_str(p, &p, "_input") ||
			    (*p))
				continue;
			snprintf(fbuf, sizeof(fbuf), HWMONDIR "hwmon%d/device/temp%d_input",
			         i, j);
			if ((fp=fopen(fbuf, "r")) == NULL) {
				msg_err(0, "fopen(hwmon%d/temp%d_input)", i, j);
				continue;
			}
			if (fgets(buf, sizeof(buf), fp) == NULL) {
				fclose(fp);
				continue;
			}
			fclose(fp);
			parse_chomp(buf);
			temp=strtol(buf, &p, 10) / 1000.0;
			if ((p == buf) || *p)
				continue;
#endif
		if (temp >= 0) {
			count ++;
			avgtemp += temp;
			if (mintemp > temp || count == 1)
				mintemp = temp;
			if (maxtemp < temp || count == 1)
				maxtemp = temp;
		}

#ifdef __linux__
		}
		closedir(dev);
#endif

	}
#ifdef __linux__
	closedir(hwmon);
#endif
	if (count == 0)
		return;
	avgtemp /= count;
	tm = get_remote_tm();
	printf("%lu cputemp_average %f\n", (u_long)tm, avgtemp);
	printf("%lu cputemp_minimum %f\n", (u_long)tm, mintemp);
	printf("%lu cputemp_maximum %f\n", (u_long)tm, maxtemp);
	msg_debug(1, "Processing of CPUTEMP command finished");
}
