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
void do_df() {
	time_t tm;
#ifndef __linux__    
	struct statfs *mntbuf;
#else
	struct mntinfo *mntbuf;
#endif
    struct statfs fs;
	int mntsize, i;
	llong dfsize, dfsizeavail, dffree, dffreeavail, dfused;
	long inodessize, inodesfree, inodesused;
	double dfpercent, inodespercent;

	msg_debug(1, "Processing of DF command started");

	tm = get_remote_tm();
#ifndef __linux__    
	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
#else
	mntsize = init_mntbuf(&mntbuf);
#endif
	if (mntsize == 0) {
		msg_syserr(0, "%s: getmntinfo", __FUNCTION__);
		msg_debug(1, "Processing of DF command finished");
		return;
	}
	msg_debug(2, "%s: Found %d mounted file system(s)", __FUNCTION__, mntsize);

	for (i = 0; i < mntsize; i++) {
		msg_debug(2, "%s: Processing file system %s (type=%s)", __FUNCTION__,
		    mntbuf[i].f_mntonname, mntbuf[i].f_fstypename);
		if (strcmp(mntbuf[i].f_fstypename, "ufs") &&
		    strcmp(mntbuf[i].f_fstypename, "ext2") &&
		    strcmp(mntbuf[i].f_fstypename, "ext3") &&
		    strcmp(mntbuf[i].f_fstypename, "ext4") &&
		    strcmp(mntbuf[i].f_fstypename, "xfs") &&
		    strcmp(mntbuf[i].f_fstypename, "zfs")) {
			msg_debug(2, "%s: File system %s skipped", __FUNCTION__,
			    mntbuf[i].f_mntonname);
			continue;
		}
		if (statfs(mntbuf[i].f_mntonname, &fs) < 0) {
			msg_syserr(0, "%s: statfs(%s)", __FUNCTION__, mntbuf[i].f_mntonname);
			continue;
		}

		dffree		= (llong)fs.f_bfree * (llong)fs.f_bsize / 1024;
		dffreeavail	= (llong)fs.f_bavail * (llong)fs.f_bsize / 1024;
		dfsize		= (llong)fs.f_blocks * (llong)fs.f_bsize / 1024;
		dfused		= dfsize - dffree;
		dfsizeavail	= dfused + dffreeavail;
		dfpercent	= dfsizeavail == 0 ? 100.0 :
			(double)dfused / (double)dfsizeavail * 100.0;

		inodessize	= fs.f_files;
		inodesfree	= fs.f_ffree;
		inodesused	= inodessize - inodesfree;
		inodespercent	= inodessize == 0 ? 100.0 :
			(double)inodesused / (double)inodessize * 100.0;

		printf("%lu dfsize:%s %lld\n",		(u_long)tm, mntbuf[i].f_mntonname, dfsize);
		printf("%lu dfsizeavail:%s %lld\n",	(u_long)tm, mntbuf[i].f_mntonname, dfsizeavail);
		printf("%lu dffree:%s %lld\n",		(u_long)tm, mntbuf[i].f_mntonname, dffree);
		printf("%lu dffreeavail:%s %lld\n",	(u_long)tm, mntbuf[i].f_mntonname, dffreeavail);
		printf("%lu dfused:%s %lld\n",		(u_long)tm, mntbuf[i].f_mntonname, dfused);
		printf("%lu dfpercent:%s %.0f\n",	(u_long)tm, mntbuf[i].f_mntonname, dfpercent);

		printf("%lu inodessize:%s %ld\n",	(u_long)tm, mntbuf[i].f_mntonname, inodessize);
		printf("%lu inodesfree:%s %ld\n",	(u_long)tm, mntbuf[i].f_mntonname, inodesfree);
		printf("%lu inodesused:%s %ld\n",	(u_long)tm, mntbuf[i].f_mntonname, inodesused);
		printf("%lu inodespercent:%s %.0f\n",	(u_long)tm, mntbuf[i].f_mntonname, inodespercent);
	}
#ifdef __linux__    
    free_mntbuf(&mntbuf, mntsize);
#endif

	msg_debug(1, "Processing of DF command finished");
}
