/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stat_swap.c 109983 2011-10-21 11:41:42Z e.kurganov $
 */

#ifndef __linux__

#include <fcntl.h>
#include <kvm.h>
#include <unistd.h>

#include "stat_common.h"


static void stat_swap_activity(void);
static void stat_swap_space_usage(void);

/*****************************************************************************
 * Processes SWAP command.
 *****************************************************************************/
void stat_swap() {
	msg_debug(1, "Processing of SWAP command started");

	/* print swap activity statistics */
	stat_swap_activity();

	/* print swap space usage statistics */
	stat_swap_space_usage();

	msg_debug(1, "Processing of SWAP command finished");
}

/*****************************************************************************
 * Prints swap activity statistics.
 *****************************************************************************/
void stat_swap_activity() {
	time_t tm;
	u_llong value;

	value = 0;
	if (sysctl_get_by_name("vm.stats.vm.v_swapout", &value, sizeof(value), 0)) {
		tm = get_remote_tm();
		printf("%lu swap_operations_out %llu\n", (u_long)tm, value);
	}
	value = 0;
	if (sysctl_get_by_name("vm.stats.vm.v_swapin", &value, sizeof(value), 0)) {
		tm = get_remote_tm();
		printf("%lu swap_operations_in %llu\n", (u_long)tm, value);
	}
	value = 0;
	if (sysctl_get_by_name("vm.stats.vm.v_swappgsout", &value, sizeof(value), 0)) {
		tm = get_remote_tm();
		printf("%lu swap_pages_out %llu\n", (u_long)tm, value);
	}
	value = 0;
	if (sysctl_get_by_name("vm.stats.vm.v_swappgsin", &value, sizeof(value), 0)) {
		tm = get_remote_tm();
		printf("%lu swap_pages_in %llu\n", (u_long)tm, value);
	}
}

/*****************************************************************************
 * Prints swap space usage statistics.
 *****************************************************************************/
void stat_swap_space_usage() {
	time_t tm;
	kvm_t *kd;
	char errbuf[_POSIX2_LINE_MAX];
	struct kvm_swap kswap;
	int page_size;
	llong space_size, space_used, space_free;
	double space_used_ratio;
	int f_swap_exists;

	/* get kvm descriptor */
	if (!(kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf))) {
		msg_err(0, "%s: kvm_openfiles: %s", __FUNCTION__, errbuf);
		return;
	}

	/* get swap statistics */
	if (kvm_getswapinfo(kd, &kswap, 1, 0) < 0) {
		msg_err(0, "%s: kvm_getswapinfo: %s", __FUNCTION__, kvm_geterr(kd));
		kvm_close(kd);
		return;
	}
	page_size = getpagesize();
	space_size = (llong)kswap.ksw_total * page_size / 1024;
	space_used = (llong)kswap.ksw_used * page_size / 1024;
	space_free = space_size - space_used;
	space_used_ratio = space_size ? (double)space_used / space_size * 100 : 0;
	f_swap_exists = space_size ? 1 : 0;

	/* print swap statistics */
	tm = get_remote_tm();
	printf("%lu swap_exists %d\n", (u_long)tm, f_swap_exists);
	printf("%lu swap_space_size %lld\n", (u_long)tm, space_size);
	printf("%lu swap_space_used %lld\n", (u_long)tm, space_used);
	printf("%lu swap_space_free %lld\n", (u_long)tm, space_free);
	printf("%lu swap_space_used_ratio %.0f\n", (u_long)tm, space_used_ratio);

	/* close kvm descriptor */
	kvm_close(kd);
}
#endif //__linux__
