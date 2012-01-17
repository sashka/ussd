/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id$
 */

#ifndef __linux__

#include <sys/param.h>
#include <sys/mount.h>

#else 

#include <sys/statfs.h>
//#include <sys/statvfs.h>

#include "grep.h"
#include "linux_fs.h"

#endif
#include "stat_common.h"


/* This flag shows that FS command is given */
int f_stat_fs_command_fs = 0;

/* This flag shows that FS_LIST command is given */
int f_stat_fs_command_fs_list = 0;


/*****************************************************************************
 * Processes FS command.
 *****************************************************************************/
void stat_fs() {
	time_t tm;
#ifndef __linux__    
	struct statfs *mntbuf;
#else
	struct mntinfo *mntbuf;
#endif
    struct statfs fs;
	int mntsize, i;
	llong space_size, space_size_avail, space_free, space_free_avail, space_used;
	long inodes_size, inodes_free, inodes_used;
	double space_used_ratio, inodes_used_ratio;

	msg_debug(1, "Processing of FS command started");

	/* get list of all mounted file systems */
#ifndef __linux__    
	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
#else
	mntsize = init_mntbuf(&mntbuf);
#endif
	if (!mntsize) {
		msg_syserr(0, "%s: getmntinfo", __FUNCTION__);
		msg_debug(1, "Processing of FS command finished");
		return;
	}
	msg_debug(2, "%s: Found %d mounted file system(s)", __FUNCTION__, mntsize);

	/* process all file systems */
	for (i = 0; i < mntsize; i++) {
		msg_debug(2, "%s: Processing file system %s (type=%s)", __FUNCTION__,
		    mntbuf[i].f_mntonname, mntbuf[i].f_fstypename);

		/* skip file systems of wrong type */
		if (strcmp(mntbuf[i].f_fstypename, "ufs") &&
		    strcmp(mntbuf[i].f_fstypename, "zfs") &&
		    strcmp(mntbuf[i].f_fstypename, "ext2") &&
		    strcmp(mntbuf[i].f_fstypename, "ext3") &&
		    strcmp(mntbuf[i].f_fstypename, "ext4") &&
		    strcmp(mntbuf[i].f_fstypename, "xfs") &&
		    strcmp(mntbuf[i].f_fstypename, "tmpfs")) {
			msg_debug(2, "%s: File system %s skipped", __FUNCTION__,
			    mntbuf[i].f_mntonname);
			continue;
		}

		/* process FS_LIST command */
		if (f_stat_fs_command_fs_list) {
			tm = get_remote_tm();
			printf("%lu fs_exists:%s 1\n", (u_long)tm, mntbuf[i].f_mntonname);
		}

		/* go to the next file system if no FS command given */
		if (!f_stat_fs_command_fs)
			continue;

		/* refresh file system statistics because it can be cached by system */
		if (statfs(mntbuf[i].f_mntonname, &fs) < 0) {
			msg_syserr(0, "%s: statfs(%s)", __FUNCTION__, mntbuf[i].f_mntonname);
			continue;
		}

		space_free		= (llong)fs.f_bfree * (llong)fs.f_bsize / 1024;
		space_free_avail	= (llong)fs.f_bavail * (llong)fs.f_bsize / 1024;
		space_size		= (llong)fs.f_blocks * (llong)fs.f_bsize / 1024;
		space_used		= space_size - space_free;
		space_size_avail	= space_used + space_free_avail;
		space_used_ratio	= space_size_avail == 0 ? 100.0 :
		    (double)space_used / (double)space_size_avail * 100.0;

		inodes_size		= fs.f_files;
		inodes_free		= fs.f_ffree;
		inodes_used		= inodes_size - inodes_free;
		inodes_used_ratio	= inodes_size == 0 ? 100.0 :
		    (double)inodes_used / (double)inodes_size * 100.0;

		tm = get_remote_tm();
		printf("%lu fs_space_size:%s %lld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_size);
		printf("%lu fs_space_size_avail:%s %lld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_size_avail);
		printf("%lu fs_space_free:%s %lld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_free);
		printf("%lu fs_space_free_avail:%s %lld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_free_avail);
		printf("%lu fs_space_used:%s %lld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_used);
		printf("%lu fs_space_used_ratio:%s %.0f\n",
		    (u_long)tm, mntbuf[i].f_mntonname, space_used_ratio);

		printf("%lu fs_inodes_size:%s %ld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, inodes_size);
		printf("%lu fs_inodes_free:%s %ld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, inodes_free);
		printf("%lu fs_inodes_used:%s %ld\n",
		    (u_long)tm, mntbuf[i].f_mntonname, inodes_used);
		printf("%lu fs_inodes_used_ratio:%s %.0f\n",
		    (u_long)tm, mntbuf[i].f_mntonname, inodes_used_ratio);
	}
#ifdef __linux__    
    free_mntbuf(&mntbuf, mntsize);
#endif

	msg_debug(1, "Processing of FS command finished");
}
