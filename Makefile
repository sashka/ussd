#
# Written by Vadim Guchenko <yhw@rambler-co.ru>
#
# $Id: Makefile 112411 2012-01-13 11:15:50Z dark $
#

_ROOT		 = 1

ProjectRoot_Directory ?= ${.CURDIR}/..
.include "../_include/commonNG.init.mk"


.undef HAVE_LIBGEOM_H
.if exists (/usr/include/libgeom.h)
HAVE_LIBGEOM_H	 = 1
.endif


TARGET		 = _EXECUTABLE
DST		 = ussd
DST_SYSLIBS	 = -lwrap -ldevstat -lkvm -lcam
.if defined (HAVE_LIBGEOM_H)
DST_SYSLIBS	+= -lgeom
.endif
SRC		 = ussd.c conf.c stats.c stat_fs.c stat_df.c stat_hdd.c stat_raid.c \
		   stat_smbios.c stat_swap.c stat_sysctl.c stat_version.c \
		   stat_cputemp.c stat_pkginfo.c \
		   ../vg_lib/vg_messages.c \
		   ../vg_lib/vg_parse.c \
		   ../vg_lib/vg_signals.c
AUTOGEN_INCLUDE	 = config.h
CINCLUDES	 = -I$(ProjectRoot_Directory)
IGNORE_GCC_CHECK = 1

#-------------------------------------------------------------------------------
#
# CONFIGURE
#

config.h:
.if defined (HAVE_LIBGEOM_H)
	@echo "#define HAVE_LIBGEOM_H" > config.h
.else
	@echo "#undef HAVE_LIBGEOM_H" > config.h
.endif

#-------------------------------------------------------------------------------
#
# INSTALL
#

INSTALL_TARGETS	 = build own_install

own_install:
	install -c -o root -g wheel -m 0555 ussd /usr/local/sbin
	install -c -o root -g wheel -m 0555 ussd.sh /usr/local/etc/rc.d
	install -d -o nobody -g wheel -m 0755 /var/run/uss
	if [ ! -r /usr/local/etc/ussd.conf ]; then touch /usr/local/etc/ussd.conf; fi
	../_include/postinstall-syslog.sh ussd /var/log/ussd.log
	../_include/postinstall-newsyslog.sh /var/log/ussd.log '644 10 1000 * Z'
	./update_ussd_in_hosts_allow.sh
	@echo "Installed successfully"

#-------------------------------------------------------------------------------
#
# PACKAGE
#

USSD_MAJOR_VERSION	!= cat version.h | grep MAJOR_VERSION | awk '{print $$3}'
USSD_MINOR_VERSION	!= cat version.h | grep MINOR_VERSION | awk '{print $$3}'
USSD_REVISION		!= cat version.h | grep REVISION | awk '{print $$3}'
PACKAGE_VERSION	 = ${USSD_MAJOR_VERSION}.${USSD_MINOR_VERSION}.${USSD_REVISION}
PACKAGE_LIST	 = ussd.c version.h ussd.sh update_ussd_in_hosts_allow.sh
PACKAGE_LIST	+= conf.c conf.h limits.h stat.h
PACKAGE_LIST	+= stats.c stats.h stat_common.h stat_fs.c stat_df.c stat_hdd.c stat_raid.c
PACKAGE_LIST	+= stat_smbios.c stat_swap.c stat_sysctl.c stat_version.c
PACKAGE_LIST	+= stat_cputemp.c stat_pkginfo.c
PACKAGE_LIST	+= ../vg_lib/vg_types.h ../vg_lib/vg_macros.h
PACKAGE_LIST	+= ../vg_lib/vg_messages.c ../vg_lib/vg_messages.h
PACKAGE_LIST	+= ../vg_lib/vg_parse.c ../vg_lib/vg_parse.h
PACKAGE_LIST	+= ../vg_lib/vg_signals.c ../vg_lib/vg_signals.h
PACKAGE_LIST	+= ../_include/postinstall-syslog.sh
PACKAGE_LIST	+= ../_include/postinstall-newsyslog.sh
PACKAGE_LIST	+= documentation.html
PACKAGE_TAG	 = ussd$(_DP)-${PACKAGE_VERSION}


.include "../_include/commonNG.main.mk"
