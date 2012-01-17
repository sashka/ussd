/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stat.h 109983 2011-10-21 11:41:42Z e.kurganov $
 */

extern int f_stat_fs_command_fs;
extern int f_stat_fs_command_fs_list;
extern int f_stat_hdd_command_hdd;
extern int f_stat_hdd_command_hdd_list;
extern int f_stat_hdd_command_smart;
extern int f_stat_hdd_smart_attrs_requested;
extern char f_stat_hdd_smart_attrs[256];
extern int f_stat_raid_command_raid;
extern int f_stat_raid_command_raid_list;
extern char *sysctl_vars[];
extern u_int sysctl_n;
void stat_fs(void);
#ifndef __linux__
void stat_hdd(void);
#else
void stat_hdd(int);
#endif
void stat_raid(void);
void stat_smbios(void);
void stat_swap(void);
void stat_sysctl(void);
void stat_version(void);

#ifdef __linux__
void stat_smart(void);
void stat_hdd_list(void);

#endif

