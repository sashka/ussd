/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stats.c 112412 2012-01-13 11:56:04Z dark $
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>
#ifndef __linux__
    #include <vm/vm_param.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ifaddrs.h>
#ifndef __linux__
    #include <sys/dkstat.h>
    #include <sys/socketvar.h>
    #include <sys/un.h>
    #include <sys/unpcb.h>
    #include <devstat.h>
    #include <arpa/inet.h>
    #include <net/ethernet.h>
    #include <net/if_types.h>
    #include <net/if_dl.h>
    #include <netdb.h>
    #include <netinet/in_pcb.h>
    #include <netinet/tcp.h>
    #include <netinet/tcp_var.h>
    #include <netinet/tcp_fsm.h>
    #include <stddef.h>
    #include <kvm.h>
#else
    #include <dirent.h>
    #include <netpacket/packet.h>
#endif
#include "conf.h"
#include "stat.h"

/* Maximum time in seconds available for each child process */
#define CHILD_TIMEOUT		15

/* Structure for interface statistics */
struct if_stats {
	/* interface name */
	char ifname[IFNAMSIZ];
	/* this flag shows whether current element of array used or not */
	int f_used;
	/* raw 32-bit interface counters retrieved on previous iteration */
	struct {
		u_long ipackets;
		u_long ibytes;
		u_long ierrors;
		u_long opackets;
		u_long obytes;
		u_long oerrors;
		u_long collisions;
	} prev;
	/* current cumulative 64-bit interface counters */
	struct {
		u_llong ipackets;
		u_llong ibytes;
		u_llong ierrors;
		u_llong opackets;
		u_llong obytes;
		u_llong oerrors;
		u_llong collisions;
	} cur;
};

/* Structure for sockets load average */
struct socket_la {
	char	var[VAR_MAXLEN + 1];
	int	qlen[300];
	u_int	incqlen;
	u_int	qlimit;
	u_int	last_ptr;
	u_int	first_ptr;
	u_int	entries;
	double	sum;
/* caching KVM pointers for fast updating w/o lookups in sysctl */
#ifndef __linux__
	u_quad_t	gencnt;
	void		* pcb;
	struct socket	* so;
#endif
};

/* Array of sockets */
struct socket_la sockets_la[SOCKET_MAXN];

/* Last update time for sockets */
struct timespec sockets_ltime;

/* Number of elements in sockets_la array */
int sockets_count = 0;

#ifndef __linux__
/* Structure for HDD's load average */
struct hdd_la {
	char	device_name[DEVSTAT_NAME_LEN];
	int	unit_number;
	u_int	incompleted_count[900];
	u_int	last_ptr;
	llong	sum_5min;
	llong	sum_15min;
	char	f_used;
};

/* Array of interfaces */
struct if_stats iface_stats[IFACE_MAXN];
/* Number of elements in %iface_stats% array */
int iface_count = 0;

/* Array of HDD's */
struct hdd_la hdds_la[32];
/* Number of elements in hdds_la array */
int hdds_count = 0;

struct devinfo hdd_dinfo;

#endif //__linux__
/* Time of remote system at the moment when it's serving started */
time_t remote_tm;
/* Value of local timer at the moment when serving of remote system started */
struct timeval start_timeval;

int sysctl_get(int *, u_int, void *, size_t, const char *);
int sysctl_get_by_name(const char *, void *, size_t, int);
size_t sysctl_get_alloc(int *, u_int, void **, const char *);
struct if_stats *iface_get(const char *);
struct if_stats *iface_add(const char *);
void ifaces_pack(void);
void init_remote_tm(time_t);
time_t get_remote_tm(void);
void wait_for_children(void);
FILE *port_read(uint32_t, uint16_t, const char *);
FILE *unixsock_read(const char *, const char *);
void terminate_pgroup(int);
void do_help(void);
void do_time(void);
void do_uname(void);
void do_uptime(void);
void do_netstat(void);
void do_ifaddrs(void);
void do_vmstat(void);
void do_acpi_temperature(void);
void do_df(void);
void get_apache_stats(struct apache_conf *);
void do_apache(void);
void get_nginx_stats(struct nginx_conf *);
void do_nginx(void);
void get_memcache_stats(struct memcache_conf *);
void do_memcache(void);
void do_socket(void);
void get_exec_stats(struct exec_conf *);
void do_exec(void);
void do_cputemp(void);
void do_hdd_load(void);
void do_pkginfo(void);

/*****************************************************************************
 * Processes client connection. %fd% is socket descriptor of client
 * connection.
 *****************************************************************************/
void process_connection(int fd) {
	char line[INPUT_LINE_MAXLEN + 1], *p, *q, *arg1_b;
	u_int debug_level_tmp;
	u_long tm_tmp;
	u_char smart_attr;
	int f_go		= 0;
	int f_version		= 0;
	int f_time		= 0;
	int f_uname		= 0;
	int f_vmstat		= 0;
	int f_sysctl		= 0;
	int f_swap		= 0;
	int f_acpi_temperature	= 0;
	int f_df		= 0;
	int f_fs		= 0;
	int f_hdd		= 0;
#ifdef __linux__    
	int f_smart		= 0;
	int f_hdd_list		= 0;
#endif    
	int f_raid		= 0;
	int f_uptime		= 0;
	int f_netstat		= 0;
	int f_ifaddrs		= 0;
	int f_smbios		= 0;
	int f_apache		= 0;
	int f_nginx		= 0;
	int f_memcache		= 0;
	int f_socket		= 0;
	int f_exec		= 0;
	int f_cputemp		= 0;
	int f_hdd_load		= 0;
	int f_pkginfo		= 0;

	/* redirect stdin, stdout and stderr to connected socket */
	dup2(fd, 0);
	dup2(0, 1);
	dup2(0, 2);
	close(fd);
	setlinebuf(stdin);
	setlinebuf(stdout);
	setlinebuf(stderr);

	/* tune messages */
	strcpy(msg_notice_prefix, "NOTICE");

	/* all further messages should be sent both to stderr and syslog */
	f_msg_stderr = 1;

	/* set remote time to local time */
	init_remote_tm(time(NULL));

	while (fgets(line, sizeof(line), stdin)) {
		/* remove end of line for easy parsing */
		parse_chomp(line);

		/* line too long */
		if (strlen(line) == (sizeof(line) - 1))
			msg_err(1, "Input line is too long: '%s'", line);

		/* do parsing */
		if (       parse_get_str(line, &p, "GO") && !*p) {
			f_go = 1;
			break;
		} else if (parse_get_str(line, &p, "HELP") && !*p) {
			do_help();
		} else if (parse_get_str(line, &p, "QUIT") && !*p) {
			return;
		} else if (parse_get_str(line, &p, "DEBUG")) {
			if (parse_get_ch(p, &p, ' ') &&
			    parse_get_uint(p, &p, &debug_level_tmp) && !*p &&
			    debug_level_tmp <= 2) {
				msg_debug_level = debug_level_tmp;
			} else {
				msg_err(0, "Parsing error. Format: DEBUG <0-2>");
			}
		} else if (parse_get_str(line, &p, "VERSION") && !*p) {
			f_version = 1;
		} else if (parse_get_str(line, &p, "TIME")) {
			if (parse_get_ch(p, &p, ' ') &&
			    parse_get_ulint(p, &p, &tm_tmp) && !*p) {
				f_time = 1;
				init_remote_tm(tm_tmp);
			} else {
				msg_err(0, "Parsing error. Format: TIME <time>");
			}
		} else if (parse_get_str(line, &p, "UNAME") && !*p) {
			f_uname = 1;
		} else if (parse_get_str(line, &p, "VMSTAT") && !*p) {
			f_vmstat = 1;
		} else if (parse_get_str(line, &p, "SYSCTL")) {
			if (!(parse_get_wspace(p, &p) && (arg1_b = p, 1) &&
			    parse_get_chset(p, &p, SYSCTL_VAR_CHSET, -SYSCTL_VAR_MAXLEN) &&
			    !*p)) {
				msg_err(0, "Parsing error. Format: SYSCTL <variable>");
				continue;
			}
			if (sysctl_n == SYSCTL_MAXN) {
				msg_err(0, "Too many SYSCTL commands (maximum %u allowed)",
				    (u_int)SYSCTL_MAXN);
				continue;
			}
			if (!(sysctl_vars[sysctl_n] = strdup(arg1_b))) {
				msg_syserr(0, "%s: SYSCTL: strdup(%s)", __FUNCTION__, arg1_b);
				continue;
			}
			sysctl_n++;
			f_sysctl = 1;
		} else if (parse_get_str(line, &p, "SWAP") && !*p) {
			f_swap = 1;
		} else if (parse_get_str(line, &p, "ACPI_TEMPERATURE") && !*p) {
			f_acpi_temperature = 1;
		} else if (parse_get_str(line, &p, "DF") && !*p) {
			f_df = 1;
		} else if (parse_get_str(line, &p, "FS") && !*p) {
			f_fs = 1;
			f_stat_fs_command_fs = 1;
		} else if (parse_get_str(line, &p, "FS_LIST") && !*p) {
			f_fs = 1;
			f_stat_fs_command_fs_list = 1;
		} else if (parse_get_str(line, &p, "HDD") && !*p) {
			f_hdd = 1;
			f_stat_hdd_command_hdd = 1;
		} else if (parse_get_str(line, &p, "HDD_LIST") && !*p) {
#ifndef __linux__        
			f_hdd = 1;
			f_stat_hdd_command_hdd_list = 1;
#else
            f_hdd_list = 1;
#endif            
		} else if (parse_get_str(line, &p, "SMART")) {
			if (!*p) {
#ifndef __linux__        
				f_hdd = 1;
#else
                f_smart = 1;
#endif
				f_stat_hdd_command_smart = 1;
			} else if (parse_get_ch(p, &q, ' ') &&
			    parse_get_uint8(q, &q, &smart_attr) && !*q) {
#ifndef __linux__        
				f_hdd = 1;
#else
                f_smart = 1;
#endif
				f_stat_hdd_command_smart = 1;
				f_stat_hdd_smart_attrs_requested = 1;
				f_stat_hdd_smart_attrs[smart_attr] = 1;
			} else if (parse_get_ch(p, &q, ' ') &&
			    parse_get_str(q, &q, "ALL") && !*q) {
#ifndef __linux__        
				f_hdd = 1;
#else
                f_smart = 1;
#endif
				f_stat_hdd_command_smart = 1;
				f_stat_hdd_smart_attrs_requested = 1;
				memset(f_stat_hdd_smart_attrs, 1,
				    sizeof(f_stat_hdd_smart_attrs));
			} else {
				msg_err(0, "Parsing error. Format: SMART [<attribute>|ALL]");
			}
		} else if (parse_get_str(line, &p, "RAID") && !*p) {
			f_raid = 1;
			f_stat_raid_command_raid = 1;
		} else if (parse_get_str(line, &p, "RAID_LIST") && !*p) {
			f_raid = 1;
			f_stat_raid_command_raid_list = 1;
		} else if (parse_get_str(line, &p, "UPTIME") && !*p) {
			f_uptime = 1;
		} else if (parse_get_str(line, &p, "NETSTAT") && !*p) {
			f_netstat = 1;
		} else if (parse_get_str(line, &p, "IFADDRS") && !*p) {
			f_ifaddrs = 1;
		} else if (parse_get_str(line, &p, "SMBIOS") && !*p) {
			f_smbios = 1;
		} else if (parse_get_str(line, &p, "APACHE") && !*p) {
			f_apache = 1;
		} else if (parse_get_str(line, &p, "NGINX") && !*p) {
			f_nginx = 1;
		} else if (parse_get_str(line, &p, "MEMCACHE") && !*p) {
			f_memcache = 1;
		} else if (parse_get_str(line, &p, "SOCKET") && !*p) {
			f_socket = 1;
		} else if (parse_get_str(line, &p, "EXEC") && !*p) {
			f_exec = 1;
		} else if (parse_get_str(line, &p, "CPUTEMP") && !*p) {
			f_cputemp = 1;
		} else if (parse_get_str(line, &p, "HDDLOAD") && !*p) {
			f_hdd_load = 1;
		} else if (parse_get_str(line, &p, "PKGINFO") && !*p) {
			f_pkginfo = 1;
		} else {
			msg_err(0, "Unknown directive '%s'", line);
		}
	}
	if (ferror(stdin))
		msg_syserr(1, "process_connection: fgets");

	if (!f_go)
		msg_err(1, "Incomplete directives: no GO");

	if (f_time)		do_time();
	if (f_uname)		do_uname();
	if (f_version)		stat_version();
	if (f_uptime)		do_uptime();
	if (f_netstat)		do_netstat();
	if (f_ifaddrs)		do_ifaddrs();
	if (f_smbios)		stat_smbios();
	if (f_vmstat)		do_vmstat();
	if (f_sysctl)		stat_sysctl();
	if (f_swap)		stat_swap();
	if (f_acpi_temperature)	do_acpi_temperature();
	if (f_raid)		stat_raid();
	if (f_apache)		do_apache();
	if (f_nginx)		do_nginx();
	if (f_memcache)		do_memcache();
	if (f_socket)		do_socket();
	if (f_exec)		do_exec();
	if (f_cputemp)		do_cputemp();
	if (f_hdd_load)		do_hdd_load();
	/* stat_hdd() and do_df()/stat_fs() should be called last because
	   they sometimes hang */
	if (f_pkginfo)		do_pkginfo();
#ifdef __linux__
	if (f_hdd)		stat_hdd(0);
	if (f_smart)		stat_smart();
	if (f_hdd_list)		stat_hdd_list();
#else    
	if (f_hdd)		stat_hdd();
#endif
	if (f_df)		do_df();
	if (f_fs)		stat_fs();

	wait_for_children();
}
#ifndef __linux__

/*****************************************************************************
 * Retrieves system information. %mib%, %mib_len%, %buf% and %size% arguments
 * are the same as first four arguments of sysctl(3) function. Buffer %buf%
 * must be allocated before calling this function. %mib_str% is textual
 * representation of %mib%, used for error messages. If successful, returns
 * non-zero. Otherwise returns zero.
 *****************************************************************************/
int sysctl_get(int *mib, u_int mib_len, void *buf, size_t size, const char *mib_str) {
	if (sysctl(mib, mib_len, buf, &size, NULL, 0) < 0) {
		msg_syserr(0, "sysctl_get: sysctl(%s)", (mib_str ? mib_str : ""));
		return(0);
	}
	return(1);
}

/*****************************************************************************
 * Retrieves system information. %name%, %buf% and %size% arguments are
 * the same as first three arguments of sysctlbyname(3) function. Buffer %buf%
 * must be allocated before calling this function. If successful, returns
 * non-zero. Otherwise returns zero. If %f_silent% flag is non-zero, error
 * message ENOENT isn't printed.
 *****************************************************************************/
int sysctl_get_by_name(const char *name, void *buf, size_t size, int f_silent) {
	if (sysctlbyname(name, buf, &size, NULL, 0) < 0) {
		if (!f_silent || (errno != ENOENT))
			msg_syserr(0, "sysctl_get_by_name: sysctlbyname(%s)", name);
		return(0);
	}
	return(1);
}

/*****************************************************************************
 * Retrieves system information. %mib% and %mib_len% arguments are the same
 * as first two arguments of sysctl(3) function. %mib_str% is textual
 * representation of %mib%, used for error messages. If successful, returns
 * size of allocated buffer %buf%, which contains retrieved system
 * information. In this case %buf% must be freed by the caller. If not
 * successful, returns zero. In this case %buf% is equal NULL.
 *****************************************************************************/
size_t sysctl_get_alloc(int *mib, u_int mib_len, void **buf, const char *mib_str) {
	size_t size;

	/* determine buffer size */
	if (sysctl(mib, mib_len, NULL, &size, NULL, 0) < 0) {
		msg_syserr(0, "sysctl_get_alloc: sysctl(%s)", (mib_str ? mib_str : ""));
		*buf = NULL;
		return(0);
	}

	/* allocate buffer */
	if ((*buf = malloc(size)) == NULL) {
		msg_syserr(0, "sysctl_get_alloc: malloc(%s)", (mib_str ? mib_str : ""));
		return(0);
	}

	/* retrieve system information */
	if (!sysctl_get(mib, mib_len, *buf, size, mib_str)) {
		free(*buf);
		*buf = NULL;
		return(0);
	}

	return(size);
}

/*****************************************************************************
 * Finds element of array %iface_stats% corresponding to interface name
 * %ifname%. If successful, returns pointer to found element. Otherwise
 * returns NULL.
 *****************************************************************************/
struct if_stats *iface_get(const char *ifname) {
	int i;

	for (i = 0; i < iface_count; i++)
		if (strcmp(ifname, iface_stats[i].ifname) == 0)
			return(&iface_stats[i]);

	return(NULL);
}

/*****************************************************************************
 * Adds new element to array %iface_stats% with interface name %ifname%. If
 * successful, returns pointer to new element. Otherwise returns NULL.
 *****************************************************************************/
struct if_stats *iface_add(const char *ifname) {
	struct if_stats *p;

	if (iface_count == IFACE_MAXN) {
		msg_err(0, "Too many interfaces");
		return(NULL);
	}

	p = &iface_stats[iface_count];
	iface_count++;
	bzero(p, sizeof(*p));
	strcpy(p->ifname, ifname);
	return(p);
}

/*****************************************************************************
 * Deletes unused elements from %iface_stats% array.
 *****************************************************************************/
void ifaces_pack() {
	int i, j;

	for (i = 0, j = 0; i < iface_count; i++) {
		if (iface_stats[i].f_used && (i > j)) {
			iface_stats[j] = iface_stats[i];
			iface_stats[i].f_used = 0;
		}
		if (iface_stats[j].f_used)
			iface_stats[j++].f_used = 0;
	}
	iface_count = j;
}

/*****************************************************************************
 * Updates interfaces statistics.
 *****************************************************************************/
void update_iface_counters() {
	struct if_msghdr *ifm;
	struct if_data *ifd;
	struct if_stats *ifs;
	char *buf, *lim, *next, ifname[IFNAMSIZ];
	int mib[6];
	size_t size;

	/* get interfaces statistics from system */
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;
	size = sysctl_get_alloc(mib, 6, (void *)&buf, "NET_RT_IFLIST");
	if (size == 0)
		return;

	lim = buf + size;
	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		next += ifm->ifm_msglen;

		/* skip unneeded records */
		if (ifm->ifm_type != RTM_IFINFO)
			continue;

		/* skip loopback and down interfaces */
		if ((ifm->ifm_flags & IFF_LOOPBACK) || !(ifm->ifm_flags & IFF_UP))
			continue;
#ifdef IFF_CANTCONFIG
		if (ifm->ifm_flags & IFF_CANTCONFIG)
			continue;
#endif
		/* skip point-point interfaces if needed */
		if ((ifm->ifm_flags & IFF_POINTOPOINT) && conf.f_skip_p2p_interfaces)
			continue;

		/* determine interface name by it's index */
		if (if_indextoname(ifm->ifm_index, ifname) == NULL) {
			msg_syserr(0, "update_iface_counters: if_indextoname(%d)", ifm->ifm_index);
			continue;
		}

		ifd = &(ifm->ifm_data);

		/* find element of %iface_stats% array corresponding to interface name */
		if (!(ifs = iface_get(ifname)))
			if (!(ifs = iface_add(ifname)))
				continue;

		ifs->f_used = 1;

		ifs->cur.ipackets	+= ifd->ifi_ipackets	- ifs->prev.ipackets;
		ifs->cur.ibytes		+= ifd->ifi_ibytes	- ifs->prev.ibytes;
		ifs->cur.ierrors	+= ifd->ifi_ierrors	- ifs->prev.ierrors;
		ifs->cur.opackets	+= ifd->ifi_opackets	- ifs->prev.opackets;
		ifs->cur.obytes		+= ifd->ifi_obytes	- ifs->prev.obytes;
		ifs->cur.oerrors	+= ifd->ifi_oerrors	- ifs->prev.oerrors;
		ifs->cur.collisions	+= ifd->ifi_collisions	- ifs->prev.collisions;

		ifs->prev.ipackets	= ifd->ifi_ipackets;
		ifs->prev.ibytes	= ifd->ifi_ibytes;
		ifs->prev.ierrors	= ifd->ifi_ierrors;
		ifs->prev.opackets	= ifd->ifi_opackets;
		ifs->prev.obytes	= ifd->ifi_obytes;
		ifs->prev.oerrors	= ifd->ifi_oerrors;
		ifs->prev.collisions	= ifd->ifi_collisions;
	}
	free(buf);

	/* delete unused elements from %iface_stats% array */
	ifaces_pack();
}
#if __FreeBSD_version >= 500000
/*****************************************************************************
 * Finds element of array %hdds_la% corresponding to HDD name.
 * If successful, returns pointer to found element. Otherwise
 * returns NULL.
 *****************************************************************************/
struct hdd_la *hdd_get(struct devstat *ds) {
	int i;

	for (i = 0; i < hdds_count; i++)
		if (!strcmp(ds->device_name, hdds_la[i].device_name) && ds->unit_number == hdds_la[i].unit_number)
			return(&hdds_la[i]);
	return(NULL);
}

/*****************************************************************************
 * Adds new element to array %hdds_la%. If successful, returns pointer
 to new element. Otherwise returns NULL.
 *****************************************************************************/
struct hdd_la *hdd_add(struct devstat *ds) {

	if (hdds_count == sizeof(hdds_la) / sizeof(hdds_la[0])) {
		msg_err(0, "Too many disks");
		return(NULL);
	}
	bzero(&hdds_la[hdds_count], sizeof(hdds_la[0]));
	strcpy(hdds_la[hdds_count].device_name, ds->device_name);
	hdds_la[hdds_count].unit_number = ds->unit_number;
	return(&hdds_la[hdds_count++]);
}

/*****************************************************************************
 * Deletes unused elements from %hdds_la% array.
 *****************************************************************************/
void hdds_pack() {
	int i, j;

	for (i = 0, j = 0; i < hdds_count; i++) {
		if (hdds_la[i].f_used && (i > j)) {
			hdds_la[j] = hdds_la[i];
			hdds_la[i].f_used = 0;
		}
		if (hdds_la[j].f_used)
			hdds_la[j++].f_used = 0;
	}
	hdds_count = j;
}

/*****************************************************************************
 * Updates HDD's statistics.
 *****************************************************************************/
void update_hdds_counters() {
	struct statinfo stats;
	int i, ptr_5min;
	long	delta, last_count;
	struct hdd_la *hdd;

	if (conf.f_disable_hdds_la)
		return;

	stats.dinfo = &hdd_dinfo;
#if __FreeBSD_version < 500000
	if (getdevs(&stats) < 0) {
		msg_err(0, "%s: getdevs: %s", __FUNCTION__, devstat_errbuf);
#else
	if (devstat_getdevs(NULL, &stats) < 0) {
		msg_err(0, "%s: devstat_getdevs: %s", __FUNCTION__, devstat_errbuf);
#endif
		return;
	}
	for (i=0; i < hdd_dinfo.numdevs; i++) {
		if (((hdd_dinfo.devices[i].device_type & DEVSTAT_TYPE_MASK) != DEVSTAT_TYPE_DIRECT) || (hdd_dinfo.devices[i].device_type & DEVSTAT_TYPE_PASS))
			continue;
		if((hdd = hdd_get(&hdd_dinfo.devices[i])) == NULL)
			if ((hdd = hdd_add(&hdd_dinfo.devices[i])) == NULL)
				continue;
		delta = hdd_dinfo.devices[i].start_count - hdd_dinfo.devices[i].end_count;
		hdd->f_used = 1;
		hdd->last_ptr++;
		if(hdd->last_ptr >= sizeof(hdd->incompleted_count) / sizeof(hdd->incompleted_count[0]))
			hdd->last_ptr -= sizeof(hdd->incompleted_count) / sizeof(hdd->incompleted_count[0]);
		last_count = hdd->incompleted_count[hdd->last_ptr];
		hdd->sum_15min += delta - last_count;
		if((ptr_5min = hdd->last_ptr - 300) < 0)
			ptr_5min += sizeof(hdd->incompleted_count) / sizeof(hdd->incompleted_count[0]);
		last_count = hdd->incompleted_count[ptr_5min];
		hdd->sum_5min += delta - last_count;
		hdd->incompleted_count[hdd->last_ptr] = delta;
	}
	hdds_pack();
}
#endif // __FreeBSD_version

#endif // __linux__

/*****************************************************************************
 * Finds element of array %socket_conf% corresponding to socket type and
 * address %addr%. If successful, returns pointer to found name. Otherwise
 * returns NULL.
 *****************************************************************************/
char *sockname_get(int type, void *addr) {
	int i;
#ifndef __linux__
	struct inpcb *inp = addr;
	struct xunpcb *xunp = addr;
#else
	struct sockaddr_in *sin = addr;
	struct sockaddr_in6 *sin6 = addr;
	char * path = addr;
#endif

	for (i=0; i < conf.socket_count; i++) {
		if (!conf.socket_conf[i].var[0] ||
		    conf.socket_conf[i].type != type)
			continue;
		switch(type) {
		case 0:
		case 1:
#ifndef __linux__
			if (conf.socket_conf[i].sockaddr.sin.sin_port != inp->inp_lport)
				continue;
			if (conf.socket_conf[i].sockaddr.sin.sin_addr.s_addr != inp->inp_laddr.s_addr)
				continue;
#else
			if (conf.socket_conf[i].sockaddr.sin.sin_port != sin->sin_port)
				continue;
			if (conf.socket_conf[i].sockaddr.sin.sin_addr.s_addr != sin->sin_addr.s_addr)
				continue;
#endif
			return(conf.socket_conf[i].var);
			break;
		case 2:
		case 3:
#ifndef __linux__
			if (conf.socket_conf[i].sockaddr.sin6.sin6_port != inp->inp_lport)
				continue;
			if (memcmp(&conf.socket_conf[i].sockaddr.sin6.sin6_addr, &inp->in6p_laddr, sizeof(inp->in6p_laddr)))
				continue;
#else
			if (conf.socket_conf[i].sockaddr.sin6.sin6_port != sin6->sin6_port)
				continue;
			if (memcmp(&conf.socket_conf[i].sockaddr.sin6.sin6_addr, &sin6->sin6_addr, sizeof(sin6->sin6_addr)))
				continue;
#endif
			return(conf.socket_conf[i].var);
			break;
		case 4:
#ifndef __linux__
			if (strncmp(conf.socket_conf[i].sockaddr.sun.sun_path, xunp->xu_addr.sun_path, xunp->xu_addr.sun_len - offsetof(struct sockaddr_un, sun_path)))
#else
			if (strcmp(conf.socket_conf[i].sockaddr.sun.sun_path, path))
#endif
				continue;
			return(conf.socket_conf[i].var);
			break;
		}
	}
	return(NULL);
}

/*****************************************************************************
 * Finds element of array %sockets_la% corresponding to socket name %name%.
 * If successful, returns pointer to found name. Otherwise returns NULL.
 *****************************************************************************/
int sock_get(char *name) {
	int i;

	for (i=0; i < sockets_count; i++) {
		if (strcmp(sockets_la[i].var, name))
			continue;
		return(i);
	}
	return(-1);
}

#define ENTRIES(n) (sizeof(n)/sizeof(n[0]))

/*****************************************************************************
 * Adds new element to array %sockets_la% with socket name %var%. If
 * successful, returns new element number. Otherwise returns -1.
 *****************************************************************************/
int sock_add(char *name) {
	if (sockets_count == SOCKET_MAXN)
		return(-1);

	bzero(&sockets_la[sockets_count], sizeof(sockets_la[0]));
	strcpy(sockets_la[sockets_count].var, name);
	memset(sockets_la[sockets_count].qlen, -1, sizeof(sockets_la[sockets_count].qlen));
	return(sockets_count ++);
}

/*****************************************************************************
 * Update element %socknum% in array %sockets_la%.
 *****************************************************************************/
void sock_update(int socknum, int qlen, int incqlen, int qlimit) {
	struct socket_la *socket = &sockets_la[socknum];
	int lastqlen = 0;

	if (socket->last_ptr == socket->first_ptr) {
		socket->first_ptr ++;
		if (socket->first_ptr >= ENTRIES(sockets_la[0].qlen))
			socket->first_ptr -= ENTRIES(sockets_la[0].qlen);
		if ((lastqlen = socket->qlen[socket->first_ptr]) >= 0) {
			socket->entries--;
			socket->sum -= lastqlen;
		}
	}
	socket->last_ptr ++;
	if (socket->last_ptr >= ENTRIES(sockets_la[0].qlen))
		socket->last_ptr -= ENTRIES(sockets_la[0].qlen);
	if (qlen >= 0) {
		socket->entries ++;
		socket->sum += qlen;
	}
	socket->qlen[socket->last_ptr] = qlen;
	if (incqlen >= 0)
		socket->incqlen = incqlen;
	if (qlimit >= 0)
		socket->qlimit = qlimit;
}

/*****************************************************************************
 * Deletes unused elements from %sockets_la% array.
 *****************************************************************************/
void sock_pack() {
	int i, j, d;
	char sockmask[sockets_count];

	for (i = 0; i < sockets_count; i++) {
		sockmask[i] = 1;
		d = sockets_la[i].last_ptr - sockets_la[i].first_ptr;
		if (d <= 0)
			d += ENTRIES(sockets_la[0].qlen);
		if (d - sockets_la[i].entries > ENTRIES(sockets_la[0].qlen) / 2) {
			d = 0;
			if (sockets_la[i].qlen[sockets_la[i].last_ptr] < 0)
				d++;
			else
				continue;
			for (j = sockets_la[i].last_ptr - 1; j != (int) sockets_la[i].first_ptr; j--, d++) {
				if (j < 0)
					j += ENTRIES(sockets_la[0].qlen);
				if (sockets_la[i].qlen[j] >= 0)
					break;
			}
			if (d > (int) ENTRIES(sockets_la[0].qlen) / 2)
				sockmask[i] = 0;
		}
	}
	for (i = 0, j = 0; i < sockets_count; i++) {
		if (sockmask[i] && (i > j)) {
			sockets_la[j] = sockets_la[i];
			sockmask[i] = 0;
		}
		if (sockmask[j])
			sockmask[j++] = 0;
	}
	sockets_count = j;
}

/*****************************************************************************
 * Updates socket statistics.
 *****************************************************************************/
void update_socket_counters(char force) {
	int proto, type, socknum = 0;
	int i;
	uint j;
	char sockmask[SOCKET_MAXN];
	int typemask[5];
	char *name, *sockname;
	size_t len;
	struct timespec ts;
	float dtime;
#ifndef __linux__
	char unixmibs[][28] = {
		"net.local.stream.pcblist",	"net.local.dgram.pcblist",
		"net.local.raw.pcblist",	"net.local.rdm.pcblist",
		"net.local.seqpacket.pcblist"};
	void * buf = NULL;
	struct xinpgen *xig, *oxig;
	struct xunpgen *xug, *oxug;
	struct tcpcb *tp;
	struct inpcb *inp = NULL;
	struct xunpcb *xunp;
	struct xsocket *so;
	struct {
		char	mib[21];
		int	proto;
		int	type1;
		int	type2;
	} ipmibs[] = {
		{ "net.inet.tcp.pcblist",	IPPROTO_TCP,	0,	2 },
		{ "net.inet.udp.pcblist",	IPPROTO_UDP,	1,	3 },
	};
	struct inpcb kvm_inp;
	struct unpcb kvm_unp;
	struct socket kvm_so;
	kvm_t * kvmd;
	char errbuf[_POSIX2_LINE_MAX];
#else
	struct {
		char mib[15];
		int type;
	} ipmibs[] = {
		{ "/proc/net/tcp",	0 },
		{ "/proc/net/udp",	1 },
		{ "/proc/net/tcp6",	2 },
		{ "/proc/net/udp6",	3 },
		{ "/proc/net/unix",	4 }
	};
	char buf[1024];
	char f_line_too_long;
	FILE *fp = 0;
	char *p;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	char * path;
	void * inp;
	unsigned state, tx_queue, rx_queue;
	char hexchar[]="0123456789aAbBcCdDeEfF";
#endif

	if (!conf.socket_count) {
		if (sockets_count)
			sockets_count = 0;
		return;
	}
#ifdef CLOCK_MONOTONIC
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
#else
	if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
#endif
		dtime = (ts.tv_sec - sockets_ltime.tv_sec) + (ts.tv_nsec - sockets_ltime.tv_nsec) * 0.000000001;
	else {
		msg_syserr(0, "clock_gettime()");
#ifdef SELECT_TIMEOUT
		dtime = SELECT_TIMEOUT;
#else
		dtime = 1;
#endif
	}
	if (!force && dtime < conf.socket_interval)
		return;
	sockets_ltime = ts;
	bzero(sockmask, sizeof(sockmask));
	bzero(typemask, sizeof(typemask));
	for (j = 0; (int) j < conf.socket_count; j++)
		typemask[conf.socket_conf[j].type] ++;
#ifndef __linux__
	/* Check cached KVM entries */
	bzero(&kvm_unp, sizeof(kvm_unp));
	if ((kvmd=kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf)) != NULL) {
#endif
		for (j = 0; (int) j < sockets_count; j++) {
			sockname = sockets_la[j].var;
			socknum = -1;
			for (i = 0; i < conf.socket_count; i++)
				if (!strcmp(sockname, conf.socket_conf[i].var)) {
					socknum = i;
					break;
				}
			/* First of all we're need to purge old data */
			if (socknum == -1) {
				if ((int) j != sockets_count)
					sockets_la[j] = sockets_la[sockets_count-1];
				sockets_count --;
				j --;
				continue;
			}
#ifdef __linux__
		}
#else
			/* we're can't update sockets w/o cached address */
			if (sockets_la[j].pcb == NULL || sockets_la[j].so == NULL)
				continue;
			if (conf.socket_conf[socknum].type < 4) {
				if ((i = kvm_read(kvmd, (uintptr_t) sockets_la[j].pcb, &kvm_inp, sizeof(kvm_inp))) != sizeof(kvm_inp)) {
					msg_syserr(0, "kvm_read(): read %d bytes while %d expected, error %s", i, sizeof(kvm_inp), kvm_geterr(kvmd));
					continue;
				}
				/* Check cached generation count */
				if (kvm_inp.inp_gencnt != sockets_la[j].gencnt)
					continue;
				/* Check cached socket address */
				if (kvm_inp.inp_socket != sockets_la[j].so)
					continue;
				/* Check addresses */
				if (conf.socket_conf[socknum].type < 2) {
					if (kvm_inp.inp_lport != conf.socket_conf[socknum].sockaddr.sin.sin_port)
						continue;
					if (kvm_inp.inp_laddr.s_addr != conf.socket_conf[socknum].sockaddr.sin.sin_addr.s_addr)
						continue;
				} else {
					if (kvm_inp.inp_lport != conf.socket_conf[socknum].sockaddr.sin6.sin6_port)
						continue;
					if(memcmp(&kvm_inp.in6p_laddr, &conf.socket_conf[socknum].sockaddr.sin6.sin6_addr, sizeof(kvm_inp.in6p_laddr)))
						continue;
				}
			} else {
				if((i = kvm_read(kvmd, (uintptr_t) sockets_la[j].pcb, &kvm_unp, sizeof(kvm_unp))) != sizeof(kvm_unp)) {
					msg_syserr(0, "kvm_read(): read %d bytes while %d expected, error %s", i, sizeof(kvm_unp), kvm_geterr(kvmd));
					continue;
				}
				/* Check cached generation count */
				if (kvm_unp.unp_gencnt != sockets_la[j].gencnt)
					continue;
				/* Check cached socket address */
				if (kvm_unp.unp_socket != sockets_la[j].so)
					continue;
			}
			if((i = kvm_read(kvmd, (uintptr_t) sockets_la[j].so, &kvm_so, sizeof(kvm_so))) != sizeof(kvm_so)) {
				msg_syserr(0, "kvm_read(): read %d bytes while %d expected, error %s", i, sizeof(kvm_so), kvm_geterr(kvmd));
				continue;
			}
			/* Check back cached pcb address */
			if (kvm_so.so_pcb != sockets_la[j].pcb)
				continue;
			sock_update(j, kvm_so.so_qlen, kvm_so.so_incqlen, kvm_so.so_qlimit);
			sockmask[j] = 1;
			typemask[conf.socket_conf[socknum].type] --;
		}
		kvm_close(kvmd);
	} else
		msg_err(0, "kvm_openfiles(): %s", errbuf);
#endif

	for (j = 0; j < sizeof(ipmibs) / sizeof(ipmibs[0]); j++) {
#ifndef __linux__
		if (!typemask[ipmibs[j].type1] && !typemask[ipmibs[j].type2])
			continue;
		if (buf != NULL) {
			free(buf);
			buf = NULL;
		}
#else
		if (!typemask[(type = ipmibs[j].type)])
			continue;
#endif
		name = ipmibs[j].mib;
#ifdef __linux__
		if ((fp=fopen(name, "r")) == NULL)
			continue;
		f_line_too_long = 0;
		while(fgets(buf, sizeof(buf), fp)) {
#else
		proto = ipmibs[j].proto;
		if (sysctlbyname(name, NULL, &len, NULL, 0) == -1)
			continue;
		if ((buf = malloc(len)) == NULL)
			continue;
		xig = oxig = buf;
		if (sysctlbyname(name, xig, &len, NULL, 0) == -1)
			continue;
		for (xig = (struct xinpgen *)((char *)xig + xig->xig_len);
		     xig->xig_len > sizeof(struct xinpgen);
		     xig = (struct xinpgen *)((char *)xig + xig->xig_len)) {
			type = 0;
			if (proto == IPPROTO_TCP) {
				tp = &((struct xtcpcb *)xig)->xt_tp;
				if (tp->t_state != TCPS_LISTEN)
					continue;
				inp = &((struct xtcpcb *)xig)->xt_inp;
				so = &((struct xtcpcb *)xig)->xt_socket;
			} else {
				type += 1;
				inp = &((struct xinpcb *)xig)->xi_inp;
				so = &((struct xinpcb *)xig)->xi_socket;
			}
			if (so->xso_protocol != proto)
				continue;
			if (inp->inp_gencnt > oxig->xig_gen ||
			    (inp->inp_vflag & (INP_IPV4 | INP_IPV6)) == 0)
				continue;
			if (inp->inp_vflag & INP_IPV6)
				type += 2;
#endif

#ifdef __linux__
			parse_chomp(buf);
			if (strlen(buf) == (sizeof(buf) - 1)) {
				f_line_too_long = 1;
				continue;
			}
			if (f_line_too_long) {
				f_line_too_long = 0;
				continue;
			}
			parse_rtrim(buf);
			switch (type) {
			case 0:
			case 1:
				if (sscanf(buf,
				           "%*d: %x:%hx %*x:%*x %x %x:%x %*x",
				           &sin.sin_addr.s_addr, &sin.sin_port,
				           &state, &tx_queue, &rx_queue) < 5)
					continue;
				sin.sin_port = htons(sin.sin_port);
				inp = &sin;
				break;
			case 2:
			case 3:
				if (sscanf(buf,
				           "%*d: %08x%08x%08x%08x:%hx %*08x%*08x%*08x%*08x:%*x %x %x:%x %*x",
				           &sin6.sin6_addr.s6_addr32[0],
				           &sin6.sin6_addr.s6_addr32[1],
				           &sin6.sin6_addr.s6_addr32[2],
				           &sin6.sin6_addr.s6_addr32[3],
				           &sin6.sin6_port,
				           &state, &tx_queue, &rx_queue) < 8)
					continue;
				sin6.sin6_port = htons(sin6.sin6_port);
				inp = &sin6;
				break;
			case 4:
				if (parse_get_chset(buf, &path, hexchar, -16) &&
				    parse_get_ch(path, &path, ':') &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -8) &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -8) &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -8) &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -4) &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -2) &&
				    parse_get_wspace(path, &path) &&
				    parse_get_chset(path, &path, hexchar, -8) &&
				    parse_get_wspace(path, &path) && *path) {
					tx_queue = 0;
					rx_queue = 0;
					inp = path;
				} else
					continue;
				break;
			}
			if ((type == 0 || type == 2) && state != 10)
				continue;
			if ((type == 1 || type == 3) && state != 7)
				continue;
#endif

			if (!typemask[type])
				continue;
			if ((sockname = sockname_get(type, inp)) == NULL)
				continue;
			if (((socknum = sock_get(sockname)) < 0) &&
			    ((socknum = sock_add(sockname)) < 0))
				continue;
			/* skip already updated sockets */
			if (sockmask[socknum])
				continue;
			typemask[type] --;
			sockmask[socknum] = 1;
#ifndef __linux__
			sock_update(socknum, so->so_qlen, so->so_incqlen, so->so_qlimit);
			sockets_la[socknum].gencnt = inp->inp_gencnt;
			sockets_la[socknum].so = inp->inp_socket;
			sockets_la[socknum].pcb = so->so_pcb;
#else
			sock_update(socknum, rx_queue, -1, tx_queue);
#endif
		}
#ifdef __linux__
		fclose(fp);
#endif
	}
#ifndef __linux__
	type = 4;

	for (j=0; j < sizeof(unixmibs)/sizeof(unixmibs[0]); j++) {
		if (buf != NULL) {
			free(buf);
			buf = NULL;
		}
		if (!typemask[type])
			continue;
		name = unixmibs[j];
		len = 0;
		if (sysctlbyname(name, NULL, &len, NULL, 0) == -1)
			continue;
		if ((buf = malloc(len)) == NULL)
			continue;
		xug = oxug = buf;
		if (sysctlbyname(name, xug, &len, NULL, 0) == -1)
			continue;
		for (xug = (struct xunpgen *)((char *)xug + xug->xug_len);
		     xug->xug_len > sizeof(struct xunpgen);
		     xug = (struct xunpgen *)((char *)xug + xug->xug_len)) {
			xunp = (struct xunpcb *)xug;
			so = &xunp->xu_socket;

			if (xunp->xu_unp.unp_gencnt > oxug->xug_gen)
				continue;
			if (xunp->xu_unp.unp_addr == NULL)
				continue;
			if (!so->so_qlimit)
				continue;

			if ((sockname = sockname_get(type, xunp)) == NULL)
				continue;
			if (((socknum = sock_get(sockname)) < 0) &&
			    ((socknum = sock_add(sockname)) < 0))
				continue;
			if (sockmask[socknum])
				continue;
			sockmask[socknum] = 1;
			typemask[type] --;
			sock_update(socknum, so->so_qlen, so->so_incqlen, so->so_qlimit);
			sockets_la[socknum].gencnt = xunp->xu_unp.unp_gencnt;
			sockets_la[socknum].so = xunp->xu_unp.unp_socket;
			sockets_la[socknum].pcb = so->so_pcb;
		}
	}
	if (buf != NULL)
		free(buf);
#endif
	for (j = 0; j < (uint) sockets_count; j++) {
		if (sockmask[j])
			continue;
		sock_update(j, -1, -1, -1);
#ifndef __linux__
		/* remove cached KVM adresses */
		sockets_la[j].so = NULL;
		sockets_la[j].pcb = NULL;
#endif

	}
	sock_pack();
}

/*****************************************************************************
 * Initializes remote time. Sets %remote_tm% global variable to %tm% and
 * %start_timeval% global variable to the current value of timer.
 *****************************************************************************/
void init_remote_tm(time_t tm) {
	struct itimerval itval;

	remote_tm = tm;
	if (getitimer(ITIMER_REAL, &itval) < 0)
		msg_syserr(1, "init_remote_tm: getitimer");
	start_timeval = itval.it_value;
}

/*****************************************************************************
 * Returns current remote time.
 *****************************************************************************/
time_t get_remote_tm() {
	struct itimerval itval;

	if (getitimer(ITIMER_REAL, &itval) < 0)
		msg_syserr(1, "get_remote_tm: getitimer");
	return(remote_tm + (start_timeval.tv_sec - itval.it_value.tv_sec));
}

/*****************************************************************************
 * Waits for all children to terminate.
 *****************************************************************************/
void wait_for_children() {
	pid_t pid;
	int status;

	for (;;) {
		pid = wait(&status);
		if ((pid < 0) && (errno != EINTR))
			break;
	}
}

/*****************************************************************************
 * Connects to socket with ip %ip% and port %port% and sends request
 * %request%. %ip% must be in network byte order. %ip% should be
 * local ip address. If successful, returns pointer to opened stream. If not
 * successful, returns NULL.
 *****************************************************************************/
FILE *port_read(uint32_t ip, uint16_t port, const char *request) {
	int fd;
	struct sockaddr_in addr;
	FILE *f;

	/* create socket */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return(NULL);

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = htons(port);

	/* connect */
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return(NULL);
	}

	/* open stream */
	if ((f = fdopen(fd, "r+")) == NULL) {
		close(fd);
		return(NULL);
	}
	setlinebuf(f);

	/* send request */
	fprintf(f, "%s", request);

	return(f);
}

/*****************************************************************************
 * Connects to unix domain socket %sockname% and sends request %request%.
 * If successful, returns pointer to opened stream. If not successful,
 * returns NULL.
 *****************************************************************************/
FILE *unixsock_read(const char *sockname, const char *request) {
	int fd;
	struct sockaddr_un addr;
	FILE *f;

	/* create socket */
	if ((fd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
		return(NULL);

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path) - 1);

	/* connect */
	if (connect(fd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0) {
		close(fd);
		return(NULL);
	}

	/* open stream */
	if ((f = fdopen(fd, "r+")) == NULL) {
		close(fd);
		return(NULL);
	}
	setlinebuf(f);

	/* send request */
	fprintf(f, "%s", request);

	return(f);
}

/*****************************************************************************
 * Terminates current process group.
 *****************************************************************************/
void terminate_pgroup(int signo) {
	/* suppress compiler warning */
	signo = signo;

	kill(0, SIGKILL);
}

/*****************************************************************************/
void do_help() {
	printf("ussd version %u.%u.%u\n", (u_int)MAJOR_VERSION, (u_int)MINOR_VERSION,
	    (u_int)REVISION);
	printf(
	    "Valid commands are:\n"
	    "        ACPI_TEMPERATURE\n"
	    "        APACHE\n"
	    "        DEBUG\n"
	    "        DF\n"
	    "        EXEC\n"
	    "        FS\n"
	    "        FS_LIST\n"
	    "        HDD\n"
	    "        HDD_LIST\n"
	    "        HELP\n"
	    "        MEMCACHE\n"
	    "        NETSTAT\n"
	    "        NGINX\n"
	    "        QUIT\n"
	    "        RAID\n"
	    "        RAID_LIST\n"
	    "        SMART [<attribute>|ALL]\n"
	    "        SMBIOS\n"
	    "        SWAP\n"
	    "        SYSCTL <variable>\n"
	    "        TIME <time>\n"
	    "        UNAME\n"
	    "        UPTIME\n"
	    "        VERSION\n"
	    "        VMSTAT\n"
	    "ending with GO\n");
}

/*****************************************************************************/
void do_time() {
	time_t tm, local_tm;

	msg_debug(1, "Processing of TIME command started");

	tm = get_remote_tm();
	local_tm = time(NULL);
	printf("%lu time %lu\n",	(u_long)tm, (u_long)local_tm);
	printf("%lu timediff %ld\n",	(u_long)tm, (long)(local_tm - tm));

	msg_debug(1, "Processing of TIME command finished");
}

/*****************************************************************************/
#ifndef __linux__
void do_uname() {
	time_t tm;
	char *buf;
	int mib[2];
	u_int i;

	msg_debug(1, "Processing of UNAME command started");

	tm = get_remote_tm();
	mib[0] = CTL_HW;
	mib[1] = HW_MACHINE;
	if (sysctl_get_alloc(mib, 2, (void *)&buf, "HW_MACHINE")) {
		printf("%lu machine %s\n", 	(u_long)tm, buf);
		free(buf);
	}

	mib[0] = CTL_KERN;
	mib[1] = KERN_OSTYPE;
	if (sysctl_get_alloc(mib, 2, (void *)&buf, "KERN_OSTYPE")) {
		printf("%lu os_name %s\n", 	(u_long)tm, buf);
		free(buf);
	}

	mib[0] = CTL_KERN;
	mib[1] = KERN_OSRELEASE;
	if (sysctl_get_alloc(mib, 2, (void *)&buf, "KERN_OSRELEASE")) {
		printf("%lu os_release %s\n", 	(u_long)tm, buf);
		free(buf);
	}

	mib[0] = CTL_KERN;
	mib[1] = KERN_VERSION;
	if (sysctl_get_alloc(mib, 2, (void *)&buf, "KERN_VERSION")) {
		for (i = 0; i < strlen(buf); i++)
			if (buf[i] == '\n')
				buf[i] = ' ';
		printf("%lu os_version %s\n", 	(u_long)tm, buf);
		free(buf);
	}

	msg_debug(1, "Processing of UNAME command finished");
}

/*****************************************************************************/
void do_uptime() {
	time_t tm;
	struct timeval boot_tm;
	struct loadavg load;
	int mib[2];
	long uptime;

	msg_debug(1, "Processing of UPTIME command started");

	tm = get_remote_tm();
	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	if (sysctl_get(mib, 2, &boot_tm, sizeof(boot_tm), "KERN_BOOTTIME")) {
		uptime = time(NULL) - boot_tm.tv_sec;
		printf("%lu uptime %lu\n",	(u_long)tm, (uptime > 0 ? uptime : 0));
	}

	mib[0] = CTL_VM;
	mib[1] = VM_LOADAVG;
	if (sysctl_get(mib, 2, &load, sizeof(load), "VM_LOADAVG")) {
		printf("%lu load1 %.2f\n",	(u_long)tm, (double)load.ldavg[0] / (double)load.fscale);
		printf("%lu load5 %.2f\n",	(u_long)tm, (double)load.ldavg[1] / (double)load.fscale);
		printf("%lu load15 %.2f\n",	(u_long)tm, (double)load.ldavg[2] / (double)load.fscale);
	}

	msg_debug(1, "Processing of UPTIME command finished");
}

/*****************************************************************************/
void do_netstat() {
	time_t tm;
	struct if_stats *ifs;
	int i;

	msg_debug(1, "Processing of NETSTAT command started");

	tm = get_remote_tm();
	for (i = 0; i < iface_count; i++) {
		ifs = &iface_stats[i];
		printf("%lu interface_packets_in:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.ipackets);
		printf("%lu interface_bytes_in:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.ibytes);
		printf("%lu interface_errors_in:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.ierrors);
		printf("%lu interface_packets_out:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.opackets);
		printf("%lu interface_bytes_out:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.obytes);
		printf("%lu interface_errors_out:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.oerrors);
		printf("%lu interface_collisions:%s %llu\n",	(u_long)tm, ifs->ifname, ifs->cur.collisions);
	}

	msg_debug(1, "Processing of NETSTAT command finished");
}

/*****************************************************************************/
void do_hdd_load() {
#if __FreeBSD_version >= 500000
	time_t tm;
	int i;
	double load5, load15;

	msg_debug(1, "Processing of HDDLOAD command started");
	tm = get_remote_tm();
	for (i = 0; i < hdds_count; i++) {
		load5 = hdds_la[i].sum_5min / 300.0;
		load15 = hdds_la[i].sum_15min / 900.0;
		printf("%lu hdd_load5:%s%d %.2f\n",	(u_long)tm, hdds_la[i].device_name, hdds_la[i].unit_number, load5);
		printf("%lu hdd_load15:%s%d %.2f\n",	(u_long)tm, hdds_la[i].device_name, hdds_la[i].unit_number, load15);
	}
#endif
}

/*****************************************************************************/
void do_vmstat() {
	time_t tm;
	u_long cp_time[CPUSTATES], cp_total;
	int i;

	msg_debug(1, "Processing of VMSTAT command started");

	if (CPUSTATES != 5) {
		msg_err(0, "do_vmstat: CPUSTATES != 5, statistics may be incorrect");
		msg_debug(1, "Processing of VMSTAT command finished");
		return;
	}

	tm = get_remote_tm();
	if (!sysctl_get_by_name("kern.cp_time", cp_time, sizeof(cp_time), 0)) {
		msg_debug(1, "Processing of VMSTAT command finished");
		return;
	}

	cp_total = 0;
	for (i = 0; i < CPUSTATES; i++)
		cp_total += cp_time[i];
	printf("%lu cp_user %lu\n",	(u_long)tm, cp_time[CP_USER]);
	printf("%lu cp_nice %lu\n",	(u_long)tm, cp_time[CP_NICE]);
	printf("%lu cp_sys %lu\n",	(u_long)tm, cp_time[CP_SYS]);
	printf("%lu cp_intr %lu\n",	(u_long)tm, cp_time[CP_INTR]);
	printf("%lu cp_idle %lu\n",	(u_long)tm, cp_time[CP_IDLE]);
	printf("%lu cp_total %lu\n",	(u_long)tm, cp_total);

	msg_debug(1, "Processing of VMSTAT command finished");
}

/*****************************************************************************/
#endif //__linux__

/*****************************************************************************/
void do_ifaddrs() {
	time_t tm;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in *sin;
	struct sockaddr_dl *sdl;
	struct sockaddr_in6 *sin6;
#ifdef __linux__
	struct sockaddr_ll *sll;
#endif
	char addr_buf[MAXHOSTNAMELEN *2 + 1] = "", addr_buf1[MAXHOSTNAMELEN *2 + 1] = "";
	int prefixlen, i;
	u_char *ptr;

	msg_debug(1, "Processing of IFADDRS command started");
	tm = get_remote_tm();
	if (getifaddrs(&ifap) != 0) {
		msg_syserr(0, "%s: getifaddrs()", __FUNCTION__);
		return;
	}
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		switch (ifa->ifa_addr->sa_family) {
		case AF_INET:
			sin = (struct sockaddr_in *) ifa->ifa_addr;
			if (sin == NULL)
				strncpy(addr_buf, "_0.0.0.0", sizeof(addr_buf));
			else
				strncpy(addr_buf, inet_ntoa(sin->sin_addr), sizeof(addr_buf));
			if (ifa->ifa_flags & IFF_POINTOPOINT) {
				sin = (struct sockaddr_in *)ifa->ifa_dstaddr;
				if (sin == NULL)
					strncpy(addr_buf1, "_0.0.0.0", sizeof(addr_buf1));
				else
					strncpy(addr_buf1, inet_ntoa(sin->sin_addr), sizeof(addr_buf1));
				printf("%lu interface_inet:%s %s -> %s\n", (u_long)tm, ifa->ifa_name, addr_buf, addr_buf1);
			} else {
				sin = (struct sockaddr_in *) ifa->ifa_netmask;
				if (sin == NULL)
					strncpy(addr_buf1, "_0.0.0.0", sizeof(addr_buf1));
				else
					strncpy(addr_buf1, inet_ntoa(sin->sin_addr), sizeof(addr_buf1));
				printf("%lu interface_inet:%s %s/%s\n", (u_long)tm, ifa->ifa_name, addr_buf, addr_buf1);
			}
			break;
#ifndef __linux__
		case AF_LINK:
			sdl = (struct sockaddr_dl *) ifa->ifa_addr;
			if (sdl->sdl_alen < 1)
				break;
			if (sdl->sdl_alen != ETHER_ADDR_LEN || (sdl->sdl_type != IFT_ETHER && sdl->sdl_type != IFT_L2VLAN
#ifdef IFT_BRIDGE
			  && sdl->sdl_type != IFT_BRIDGE
#endif
			))
				break;
			printf("%lu interface_ether:%s %s\n",
			       (u_long)tm, ifa->ifa_name,
			       ether_ntoa((struct ether_addr *)LLADDR(sdl)));
#else
		case AF_PACKET:
			sll = (struct sockaddr_ll *) ifa->ifa_addr;
			if (sll == NULL || sll->sll_protocol != 0 || sll->sll_hatype != 1 ||
			    sll->sll_pkttype != 0 || sll->sll_halen != 6)
				break;
			printf("%lu interface_ether:%s %02x:%02x:%02x:%02x:%02x:%02x\n",
			       (u_long)tm, ifa->ifa_name,
			       sll->sll_addr[0], sll->sll_addr[1], sll->sll_addr[2],
			       sll->sll_addr[3], sll->sll_addr[4], sll->sll_addr[5]);
#endif
			break;
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *) ifa->ifa_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) && *(u_short *) &sin6->sin6_addr.s6_addr[2] != 0)
				break;
#ifndef __linux__
			if (getnameinfo((struct sockaddr *) sin6, sin6->sin6_len, addr_buf, sizeof(addr_buf), NULL, 0, NI_NUMERICHOST) != 0)
#else
			if (getnameinfo((struct sockaddr *) sin6, sizeof(struct sockaddr_in6), addr_buf, sizeof(addr_buf), NULL, 0, 0) != 0)
#endif
				inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, sizeof(addr_buf));
			if (ifa->ifa_flags & IFF_POINTOPOINT) {
				sin6 = (struct sockaddr_in6 *)ifa->ifa_dstaddr;
				if (sin6 != NULL && sin6->sin6_family == AF_INET6) {
					if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) && *(u_short *) &sin6->sin6_addr.s6_addr[2] != 0)
						break;
#ifndef __linux__
					if (getnameinfo((struct sockaddr *) sin6, sin6->sin6_len, addr_buf1, sizeof(addr_buf1), NULL, 0, NI_NUMERICHOST) != 0)
#else
					if (getnameinfo((struct sockaddr *) sin6, sizeof(addr_buf1), addr_buf1, sizeof(addr_buf1), NULL, 0, 0) != 0)
#endif
						inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf1, sizeof(addr_buf));
					printf("%lu interface_inet6:%s %s -> %s\n", (u_long)tm, ifa->ifa_name, addr_buf, addr_buf1);
				}
			} else {
				sin6 = (struct sockaddr_in6 *)ifa->ifa_netmask;
				if (sin6 == NULL)
					prefixlen = -1;
				else {
					prefixlen = 0;
					ptr = (u_char *) &sin6->sin6_addr;
					for (i=0; i < (int) sizeof(sin6->sin6_addr); i++, prefixlen += 8, ptr++)
						if (*ptr != 0xff)
							break;
					for (i = 7; i != 0; i--, prefixlen ++)
						if (!(*ptr & (1 << i)))
							break;
					for(; i != 0; i--)
						if (*ptr & (1 << i)) {
							printf ("prefixlen=%d\n", prefixlen);
							prefixlen = -1;
							break;
						}
					if (prefixlen != -1) {
						ptr ++;
						for (i=0; (ptr - (u_char *) &sin6->sin6_addr) + i < (int) sizeof(sin6->sin6_addr); i++)
							if (ptr[i]) {
								prefixlen = -1;
								break;
							}
					}
				}
				printf("%lu interface_inet6:%s %s/%d\n", (u_long)tm, ifa->ifa_name, addr_buf, prefixlen);
			}
			break;
		}
	}
	freeifaddrs(ifap);

	msg_debug(1, "Processing of IFADDRS command finished");
}

/*****************************************************************************/
void do_acpi_temperature() {
	time_t tm;
	int temp, i;
	char buf[64];
#ifdef __linux__
	char fbuf[256], *p;
	int j, k;
	DIR * devices, *dev;
	FILE *fp;
	struct dirent * dp, * dp2;
	char mibs[][2][21] = {{ "/sys/class/thermal/", "thermal_zone" },
			   { "/sys/class/hwmon/", "hwmon" }};
#endif

	msg_debug(1, "Processing of ACPI_TEMPERATURE command started");

	tm = get_remote_tm();
#ifndef __linux__
	for (i = 0; i < ACPI_TZ_MAXN; i++) {
		snprintf(buf, sizeof(buf), "hw.acpi.thermal.tz%d.temperature", i);
		if (!sysctl_get_by_name(buf, &temp, sizeof(temp), 1)) {
			msg_debug(1, "Processing of ACPI_TEMPERATURE command finished");
			return;
		}
		printf("%lu acpi_temperature:tz%d %.0f\n", (u_long)tm, i,
		    ((double)temp - 2731.5) / 10);
#else
	for (i=0; i < sizeof(mibs)/sizeof(mibs[0]); i++) {
		if ((devices=opendir(mibs[i][0])) == NULL)
			continue;
		while ((dp=readdir(devices)) != NULL) {
			if (!parse_get_str(dp->d_name, &p, mibs[i][1]) ||
			    !parse_get_int(p, &p, &j) ||
			    (*p))
			continue;
			switch (dp->d_name[0]) {
			case 't':
				snprintf(fbuf, sizeof(buf), "%s/%s%d/temp", mibs[i][0], mibs[i][1], j);
				if ((fp=fopen(fbuf, "r")) == NULL)
					continue;
				if (fgets(buf, sizeof(buf), fp) == NULL) {
					fclose(fp);
					continue;
				}
				fclose(fp);
				parse_chomp(buf);
				if (!*buf)
					continue;
				temp = strtol(buf, &p, 10);
				if ((p == buf) || *p)
					continue;
				printf("%lu acpi_temperature:%s%d %.3f\n", (u_long)tm, mibs[i][1], j, temp / 1000.0);
				break;
			case 'h':
				/* this functional is slightly like do_cputemp in linux,
				   so we're skipping coretemp-like drivers because
				   we're only need average temperature of all cores */
				snprintf(fbuf, sizeof(fbuf), "%s/%s%d/device/name", mibs[i][0], mibs[i][1], j);
				if ((fp=fopen(fbuf, "r")) == NULL)
					continue;
				if (fgets(buf, sizeof(buf), fp) == NULL) {
					fclose(fp);
					continue;
				}
				fclose(fp);
				parse_chomp(buf);
				if (!strcmp(buf, "coretemp") || !strcmp(buf, "via_cputemp") ||
				    !strcmp(buf, "k8temp") || !strcmp(buf, "k10temp"))
					continue;
				snprintf(fbuf, sizeof(fbuf), "%s/%s%d/device/", mibs[i][0], mibs[i][1], j);
				if ((dev=opendir(fbuf)) == NULL)
					continue;
				while ((dp2=readdir(dev)) != NULL) {
					msg_debug(2, "Got file %s", dp2->d_name);
					if (!parse_get_str(dp2->d_name, &p, "temp") ||
					    !parse_get_int(p, &p, &k) ||
					    !parse_get_str(p, &p, "_label") ||
					    (*p))
						continue;
					if ((fp=fopen(fbuf, "r")) == NULL)
						continue;
					if (fgets(buf, sizeof(buf), fp) == NULL) {
						fclose(fp);
						continue;
					}
					fclose(fp);
					parse_chomp(buf);
					if (!*buf)
						continue;
					temp = strtol(buf, &p, 10);
					if ((buf == p) || *p)
						continue;
					snprintf(fbuf, sizeof(fbuf), "%s/%s%d/device/temp%d_label", mibs[i][0], mibs[i][1], j, k);
					if ((fp=fopen(fbuf, "r")) == NULL)
						continue;
					if (fgets(buf, sizeof(buf), fp) == NULL) {
						fclose(fp);
						continue;
					}
					fclose(fp);
					parse_chomp(buf);
					for (p=buf; *p; p++)
						if (index(VAR_CHSET, *p) == NULL)
							*p = '_';
					printf("%lu acpi_temperature:%s %.3f\n", (u_long)tm, buf, temp / 1000.0);
				}
				closedir(dev);
				break;
			}
		}
		closedir(devices);
#endif
	}

	msg_debug(1, "Processing of ACPI_TEMPERATURE command finished");
}

/*****************************************************************************/
void do_socket() {
	time_t tm;
	int i, j, maxq;
	double load;

	msg_debug(1, "Processing of SOCKET command started");
	/* We're must update socket counters! */
	if (conf.socket_interval < 0)
		update_socket_counters(1);
	tm = get_remote_tm();
	for (i = 0; i < sockets_count; i++) {
		load = 0;
		maxq = sockets_la[i].qlen[sockets_la[i].last_ptr];
		for (j = sockets_la[i].last_ptr - 1; j != (int) sockets_la[i].first_ptr; j--) {
			if (j < 0)
				j += ENTRIES(sockets_la[0].qlen);
			if (maxq < sockets_la[i].qlen[j])
				maxq = sockets_la[i].qlen[j];
		}
		if (sockets_la[i].entries)
			load = sockets_la[i].sum / sockets_la[i].entries;
		printf("%lu socket_exist:%s %d\n", (u_long) tm, sockets_la[i].var, (sockets_la[i].qlen[sockets_la[i].last_ptr] >= 0));
		printf("%lu socket_queue_receive_limit:%s %d\n", (u_long) tm, sockets_la[i].var, sockets_la[i].qlimit);
		if (sockets_la[i].qlen[sockets_la[i].last_ptr] >= 0)
			printf("%lu socket_queue_receive_length:%s %d\n", (u_long) tm, sockets_la[i].var, sockets_la[i].qlen[sockets_la[i].last_ptr]);
#ifndef __linux__
		printf("%lu socket_queue_receive_inclength:%s %d\n", (u_long) tm, sockets_la[i].var, sockets_la[i].incqlen);
#endif
		printf("%lu socket_queue_receive_load_average:%s %f\n", (u_long) tm, sockets_la[i].var, load);
		printf("%lu socket_queue_receive_peak_max:%s %d\n", (u_long) tm, sockets_la[i].var, maxq);
	}
}

#undef ENTRIES

/*****************************************************************************/
void get_apache_stats(struct apache_conf *apache) {
	time_t tm;
	char line[INPUT_LINE_MAXLEN + 1], request[128], *p;
	int f_line_too_long, f_http_error, f_http_status_line, f_http_headers;
	u_llong n;
	u_int tmp;
	FILE *f;

	/* send HTTP request */
	snprintf(request, sizeof(request),
	    "GET /server-status?auto HTTP/1.0\r\n"
	    "Host: %s\r\n"
	    "User-Agent: ussd/%u.%u.%u\r\n\r\n",
	    apache->ip_str, (u_int)MAJOR_VERSION, (u_int)MINOR_VERSION, (u_int)REVISION);
	if ((f = port_read(apache->ip, apache->port, request))) {
		msg_debug(2, "%s: [%s] Connected to %s:%d", __FUNCTION__,
		    apache->var, apache->ip_str, apache->port);
	} else {
		msg_debug(2, "%s: [%s] Can't connect to %s:%d", __FUNCTION__,
		    apache->var, apache->ip_str, apache->port);
		return;
	}

	/* process HTTP response */
	tm = get_remote_tm();
	f_line_too_long = 0;
	f_http_error = 0;
	f_http_status_line = 0;
	f_http_headers = 0;
	while (fgets(line, sizeof(line), f)) {
		/* just read and discard all lines if any error occured */
		if (f_http_error)
			continue;

		/* remove end of line for easy parsing */
		parse_chomp(line);

		/* line too long, ignoring */
		if (strlen(line) == (sizeof(line) - 1)) {
			f_line_too_long = 1;
			continue;
		}
		/* skip the rest of too long line */
		if (f_line_too_long) {
			f_line_too_long = 0;
			continue;
		}

		if (!f_http_status_line) {
			/* process HTTP status line */
			msg_debug(2, "%s: [%s] Processing HTTP status line: %s", __FUNCTION__,
			    apache->var, line);
			if (parse_get_str(line, &p, "HTTP/") && parse_get_uint(p, &p, &tmp) &&
			    parse_get_ch(p, &p, '.') && parse_get_uint(p, &p, &tmp) &&
			    parse_get_str(p, &p, " 200 OK") && !*p) {
				f_http_status_line = 1;
				msg_debug(2, "%s: [%s] HTTP status line is good",
				    __FUNCTION__, apache->var);
			} else {
				f_http_error = 1;
				msg_debug(2, "%s: [%s] HTTP status line is bad, discarding whole response",
				    __FUNCTION__, apache->var);
			}
			continue;
		}

		if (!f_http_headers) {
			/* process HTTP headers */
			if (*line) {
				msg_debug(2, "%s: [%s] Skipping HTTP header: %s",
				    __FUNCTION__, apache->var, line);
			} else {
				f_http_headers = 1;
				msg_debug(2, "%s: [%s] End of HTTP headers",
				    __FUNCTION__, apache->var);
			}
			continue;
		}

		/* process HTTP body */
		if (       parse_get_str(line, &p, "Total Accesses: ") &&
		    parse_get_ullint(p, &p, &n) && !*p) {
			printf("%lu apache_total_accesses:%s %llu\n", (u_long)tm, apache->var, n);
		} else if (parse_get_str(line, &p, "Total kBytes: ") &&
		    parse_get_ullint(p, &p, &n) && !*p) {
			printf("%lu apache_total_kbytes:%s %llu\n", (u_long)tm, apache->var, n);
		} else if ((parse_get_str(line, &p, "BusyServers: ") ||
		    parse_get_str(line, &p, "BusyWorkers: ")) &&
		    parse_get_ullint(p, &p, &n) && !*p) {
			printf("%lu apache_busy_servers:%s %llu\n", (u_long)tm, apache->var, n);
		} else if ((parse_get_str(line, &p, "IdleServers: ") ||
		    parse_get_str(line, &p, "IdleWorkers: ")) &&
		    parse_get_ullint(p, &p, &n) && !*p) {
			printf("%lu apache_idle_servers:%s %llu\n", (u_long)tm, apache->var, n);
		} else if (parse_get_str(line, &p, "Uptime: ") &&
		    parse_get_ullint(p, &p, &n) && !*p) {
			printf("%lu apache_uptime:%s %llu\n", (u_long)tm, apache->var, n);
		}
	}
	fclose(f);
}

/*****************************************************************************/
void do_apache() {
	time_t tm;
	int i;
	pid_t pid;

	msg_debug(1, "Processing of APACHE command started");

	for (i = 0; i < conf.apache_count; i++) {
		tm = get_remote_tm();
		if ((pid = fork()) == 0) { /* child */
			alarm(CHILD_TIMEOUT);
			init_remote_tm(tm);
			get_apache_stats(&conf.apache_conf[i]);
			exit(EXIT_SUCCESS);
		} else if (pid < 0) {
			msg_syserr(0, "do_apache: can't fork");
			break;
		}
	}

	msg_debug(1, "Processing of APACHE command finished");
}

/*****************************************************************************/
void get_nginx_stats(struct nginx_conf *nginx) {
	time_t tm;
	char line[INPUT_LINE_MAXLEN + 1], request[128], *p;
	int f_line_too_long, f_http_error, f_http_status_line, f_http_headers;
	u_llong n, n1, n2, n3;
	u_int tmp;
	FILE *f;

	/* send HTTP request */
	snprintf(request, sizeof(request),
	    "GET /mathopd.dmp HTTP/1.0\r\n"
	    "Host: %s\r\n"
	    "User-Agent: ussd/%u.%u.%u\r\n\r\n",
	    nginx->ip_str, (u_int)MAJOR_VERSION, (u_int)MINOR_VERSION, (u_int)REVISION);
	if ((f = port_read(nginx->ip, nginx->port, request))) {
		msg_debug(2, "%s: [%s] Connected to %s:%d", __FUNCTION__,
		    nginx->var, nginx->ip_str, nginx->port);
	} else {
		msg_debug(2, "%s: [%s] Can't connect to %s:%d", __FUNCTION__,
		    nginx->var, nginx->ip_str, nginx->port);
		return;
	}

	/* process HTTP response */
	tm = get_remote_tm();
	f_line_too_long = 0;
	f_http_error = 0;
	f_http_status_line = 0;
	f_http_headers = 0;
	while (fgets(line, sizeof(line), f)) {
		/* just read and discard all lines if any error occured */
		if (f_http_error)
			continue;

		/* remove end of line for easy parsing */
		parse_chomp(line);

		/* line too long, ignoring */
		if (strlen(line) == (sizeof(line) - 1)) {
			f_line_too_long = 1;
			continue;
		}
		/* skip the rest of too long line */
		if (f_line_too_long) {
			f_line_too_long = 0;
			continue;
		}

		if (!f_http_status_line) {
			/* process HTTP status line */
			msg_debug(2, "%s: [%s] Processing HTTP status line: %s", __FUNCTION__,
			    nginx->var, line);
			if (parse_get_str(line, &p, "HTTP/") && parse_get_uint(p, &p, &tmp) &&
			    parse_get_ch(p, &p, '.') && parse_get_uint(p, &p, &tmp) &&
			    parse_get_str(p, &p, " 200 OK") && !*p) {
				f_http_status_line = 1;
				msg_debug(2, "%s: [%s] HTTP status line is good",
				    __FUNCTION__, nginx->var);
			} else {
				f_http_error = 1;
				msg_debug(2, "%s: [%s] HTTP status line is bad, discarding whole response",
				    __FUNCTION__, nginx->var);
			}
			continue;
		}

		if (!f_http_headers) {
			/* process HTTP headers */
			if (*line) {
				msg_debug(2, "%s: [%s] Skipping HTTP header: %s",
				    __FUNCTION__, nginx->var, line);
			} else {
				f_http_headers = 1;
				msg_debug(2, "%s: [%s] End of HTTP headers",
				    __FUNCTION__, nginx->var);
			}
			continue;
		}

		/* process HTTP body */
		if (parse_get_str(line, &p, "Active connections: ") &&
		    parse_get_ullint(p, &p, &n)) {
			printf("%lu nginx_active:%s %llu\n", (u_long)tm, nginx->var, n);
		} else if (parse_get_wspace(line, &p) && parse_get_ullint(p, &p, &n1) &&
		    parse_get_wspace(p, &p) && parse_get_ullint(p, &p, &n2) &&
		    parse_get_wspace(p, &p) && parse_get_ullint(p, &p, &n3)) {
			printf("%lu nginx_accepts:%s %llu\n", (u_long)tm, nginx->var, n1);
			printf("%lu nginx_handled:%s %llu\n", (u_long)tm, nginx->var, n2);
			printf("%lu nginx_requests:%s %llu\n", (u_long)tm, nginx->var, n3);
		} else if (parse_get_str(line, &p, "Reading: ") &&
		    parse_get_ullint(p, &p, &n1) && parse_get_wspace(p, &p) &&
		    parse_get_str(p, &p, "Writing: ") && parse_get_ullint(p, &p, &n2) &&
		    parse_get_wspace(p, &p) && parse_get_str(p, &p, "Waiting: ") &&
		    parse_get_ullint(p, &p, &n3)) {
			printf("%lu nginx_reading:%s %llu\n", (u_long)tm, nginx->var, n1);
			printf("%lu nginx_writing:%s %llu\n", (u_long)tm, nginx->var, n2);
			printf("%lu nginx_waiting:%s %llu\n", (u_long)tm, nginx->var, n3);
		}
	}
	fclose(f);
}

/*****************************************************************************/
void do_nginx() {
	time_t tm;
	int i;
	pid_t pid;

	msg_debug(1, "Processing of NGINX command started");

	for (i = 0; i < conf.nginx_count; i++) {
		tm = get_remote_tm();
		if ((pid = fork()) == 0) { /* child */
			alarm(CHILD_TIMEOUT);
			init_remote_tm(tm);
			get_nginx_stats(&conf.nginx_conf[i]);
			exit(EXIT_SUCCESS);
		} else if (pid < 0) {
			msg_syserr(0, "do_nginx: can't fork");
			break;
		}
	}

	msg_debug(1, "Processing of NGINX command finished");
}

/*****************************************************************************/
void get_memcache_stats(struct memcache_conf *memcache) {
	time_t tm;
	char line[INPUT_LINE_MAXLEN + 1], *p;
	char var[VAR_MAXLEN + 1], *var_b, *var_e, *rest;
	int f_line_too_long;
	FILE *f;

	/* do request */
	if (memcache->f_unixsock) {
		if ((f = unixsock_read(memcache->sockname, "stats\r\n")) == NULL)
			return;
	} else {
		if ((f = port_read(memcache->ip, memcache->port, "stats\r\n")) == NULL)
			return;
	}

	tm = get_remote_tm();
	f_line_too_long = 0;
	while (fgets(line, sizeof(line), f)) {
		/* remove end of line for easy parsing */
		parse_chomp(line);

		/* line too long, ignoring */
		if (strlen(line) == (sizeof(line) - 1)) {
			f_line_too_long = 1;
			continue;
		}
		/* skip the rest of too long line */
		if (f_line_too_long) {
			f_line_too_long = 0;
			continue;
		}

		/* remove trailing white spaces */
		parse_rtrim(line);

		/* do parsing */
		if (parse_get_str(line, &p, "STAT")) {
			/* format: STAT <variable> <value> */
			if (parse_get_wspace(p, &var_b) &&
			    parse_get_chset(var_b, &var_e, VAR_CHSET, -(int)(sizeof(var) - 1)) &&
			    parse_get_wspace(var_e, &rest)) {
				strncpy(var, var_b, var_e - var_b);
				var[var_e - var_b] = 0;
				parse_tolower(var);
				printf("%lu memcache_%s:%s %s\n", (u_long)tm, var, memcache->var, rest);
			}
		} else if (parse_get_str(line, &p, "END") && !*p) {
			break;
		}
	}
	fclose(f);
}

/*****************************************************************************/
void do_memcache() {
	time_t tm;
	int i;
	pid_t pid;

	msg_debug(1, "Processing of MEMCACHE command started");

	for (i = 0; i < conf.memcache_count; i++) {
		tm = get_remote_tm();
		if ((pid = fork()) == 0) { /* child */
			alarm(CHILD_TIMEOUT);
			init_remote_tm(tm);
			get_memcache_stats(&conf.memcache_conf[i]);
			exit(EXIT_SUCCESS);
		} else if (pid < 0) {
			msg_syserr(0, "do_memcache: can't fork");
			break;
		}
	}

	msg_debug(1, "Processing of MEMCACHE command finished");
}

/*****************************************************************************/
void get_exec_stats(struct exec_conf *exec) {
	time_t tm;
	char line[INPUT_LINE_MAXLEN + 1];
	char var[VAR_MAXLEN + 1], *var_b, *var_e, *rest;
	int f_line_too_long;
	FILE *f;

	/* do exec */
	fclose(stderr);
    msg_debug(1, "Executing: %s", exec->command);
#ifdef __linux__    
	if ((f = popen(exec->command, "r")) == NULL) {
#else
	if ((f = popen(exec->command, "r+")) == NULL) {
#endif
        msg_err(0, "Can't popen. Errno = %d, command: %s", errno, exec->command);
		return;
    }
	setlinebuf(f);

	f_line_too_long = 0;
	while (fgets(line, sizeof(line), f)) {
		/* remove end of line for easy parsing */
		parse_chomp(line);
        msg_debug(2, "Got line from EXEC: %s", line);

		/* line too long, ignoring */
		if (strlen(line) == (sizeof(line) - 1)) {
			f_line_too_long = 1;
			continue;
		}
		/* skip the rest of too long line */
		if (f_line_too_long) {
			f_line_too_long = 0;
			continue;
		}

		/* remove trailing white spaces */
		parse_rtrim(line);

		tm = get_remote_tm();

		/* do parsing */
		/* format: <variable> <value> */
		var_b = line;
		if (parse_get_chset(var_b, &var_e, VAR_CHSET ":", -(int)(sizeof(var) - 1)) &&
		    parse_get_wspace(var_e, &rest)) {
			strncpy(var, var_b, var_e - var_b);
			var[var_e - var_b] = 0;
			parse_tolower(var);
			printf("%lu exec_%s %s\n", (u_long)tm, var, rest);
		}
	}
	pclose(f);
}

/*****************************************************************************/
void do_exec() {
	time_t tm;
	int i;
	pid_t pid;

	msg_debug(1, "Processing of EXEC command started");

	for (i = 0; i < conf.exec_count; i++) {
		tm = get_remote_tm();
		if ((pid = fork()) == 0) { /* child */
			alarm(CHILD_TIMEOUT);
			init_remote_tm(tm);
			setpgid(0, getpid());
			signal(SIGALRM, terminate_pgroup);
			get_exec_stats(&conf.exec_conf[i]);
			exit(EXIT_SUCCESS);
		} else if (pid < 0) {
			msg_syserr(0, "do_exec: can't fork");
			break;
		}
	}

	msg_debug(1, "Processing of EXEC command finished");
}
#ifdef __linux__
void update_hdds_counters() {
	stat_hdd(1);
}
#endif
