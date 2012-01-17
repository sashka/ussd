/*
 * Written by Konstantin Kukushkin <dark@rambler-co.ru>
 *
 * 	$Id: stat_pkginfo.c 109983 2011-10-21 11:41:42Z e.kurganov $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#ifdef __linux__
// for strlcpy and strlcat
#include <bsd/string.h>
#endif

#include "stat_common.h"
#define PKGDIR "/var/db/pkg/"


/*****************************************************************************
 * Processes PKGINFO command.
 *****************************************************************************/
void do_pkginfo() {
	unsigned pkgs = 0;
	time_t tm;
	DIR * pkgdir;
	FILE * contents;
	struct dirent * dp;
	struct stat sb;
	char filename[MAXNAMLEN], buf[256], *p;
	msg_debug(1, "Processing of PKGINFO command started");

	/* open PKGDIR directory */
	if ((pkgdir=opendir(PKGDIR)) == NULL) {
		msg_syserr(0, "opendir(" PKGDIR ")", __FUNCTION__);
		return;
	}

	tm = get_remote_tm();
	strlcpy(filename, PKGDIR, sizeof(filename));
	while ((dp = readdir(pkgdir)) != NULL) {
		if (dp->d_type != DT_DIR)
			continue;
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		strlcpy(filename+strlen(PKGDIR), dp->d_name, sizeof(filename)-strlen(PKGDIR));
		if (stat(filename, &sb) == 0) {
			printf("%lu pkg_ctime:%s %lu\n", (u_long)tm, dp->d_name, 0lu+sb.st_ctime);
		}
		strlcat(filename, "/+CONTENTS", sizeof(filename));
		if ((contents=fopen(filename, "r")) == NULL) {
			msg_syserr(0, "fopen(%s)", filename, __FUNCTION__);
			continue;
		}
		while (fgets(buf, sizeof(buf), contents) != NULL) {
			parse_chomp(buf);
			if (parse_get_str(buf, &p, "@name") && parse_get_wspace(p, &p)) {
				printf("%lu pkg_exist:%s 1\n", (u_long)tm, p);
			} else if (parse_get_str(buf, &p, "@comment") && parse_get_wspace(p, &p) && parse_get_str(p, &p, "ORIGIN:")) {
				printf("%lu pkg_origin:%s %s\n", (u_long)tm, dp->d_name, p);
			}
		}
		fclose(contents);
		pkgs ++;
	}
	closedir(pkgdir);
	printf("%lu pkg_count %u\n", (u_long)tm, pkgs);
	msg_debug(1, "Processing of PKGINFO command finished");
}

