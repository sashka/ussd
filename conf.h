/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: conf.h 112401 2012-01-12 12:57:01Z dark $
 */

#include <paths.h>

#include "vg_lib/vg_types.h"
#include "vg_lib/vg_messages.h"
#include "vg_lib/vg_parse.h"
#include "vg_lib/vg_macros.h"
#include "version.h"
#include "limits.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>


/* Default configuration file name */
#define DFL_CONFIGFILE		"/usr/local/etc/ussd.conf"

/* Default port on which ussd accepts connections */
#define DFL_LISTEN_PORT		1957

/* Default file name where to write the process ID */
#define DFL_PIDFILE		_PATH_VARRUN "uss/ussd.pid"

/* Default working directory */
#define DFL_WORKDIR		_PATH_VARTMP


/* Structure for apache configuration */
struct apache_conf {
	/* returned variable name */
	char var[VAR_MAXLEN + 1];
	/* ip address in network byte order */
	uint32_t ip;
	/* string representation of ip address */
	char ip_str[16];
	/* port */
	uint16_t port;
};

/* Structure for nginx configuration */
struct nginx_conf {
	/* returned variable name */
	char var[VAR_MAXLEN + 1];
	/* ip address in network byte order */
	uint32_t ip;
	/* string representation of ip address */
	char ip_str[16];
	/* port */
	uint16_t port;
};

/* Structure for memcache configuration */
struct memcache_conf {
	/* returned variable name */
	char var[VAR_MAXLEN + 1];
	/* This flag shows whether ip address and port used (0)
	   or unix domain socket used (1) */
	uint8_t f_unixsock;
	/* ip address in network byte order */
	uint32_t ip;
	/* port */
	uint16_t port;
	/* unix domain socket name */
	char sockname[SOCKNAME_MAXLEN + 1];
};

/* Structure for memcache configuration */
struct socket_conf {
	/* returned variable name */
	char var[VAR_MAXLEN + 1];
	/* This flag shows whether ip address and port used (0)
	   or unix domain socket used (1) */
	uint8_t type;
	union {
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
		struct sockaddr_un sun;
		struct sockaddr sa;
	} sockaddr;
};

/* Structure for exec configuration */
struct exec_conf {
	/* shell command to execute */
	char command[SHELL_COMMAND_MAXLEN + 1];
};

/* Structure for ussd configuration */
struct conf {
	/* This flag shows whether ussd will use hosts access control files
	   when accepting incoming connections or not */
	int f_hosts_access;

	/* This flag shows whether ussd will enable SMART capabilities
	   on all HDDs while processing SMART command or not */
	int f_enable_smart;

	/* Configuration file name */
	char configfile[FILENAME_MAXLEN + 1];

	/* Port on which ussd will accept connections */
	uint16_t listen_port;

	/* File name where to write the process ID */
	char pidfile[FILENAME_MAXLEN + 1];

	/* Working directory */
	char workdir[FILENAME_MAXLEN + 1];

	/* User name or id for setuid(2) */
	char user[USER_GROUP_MAXLEN + 1];

	/* Group name or id for setgid(2) */
	char group[USER_GROUP_MAXLEN + 1];

	/* Apache configuration */
	struct apache_conf apache_conf[APACHE_MAXN];
	/* Number of elements in %apache_conf% array */
	int apache_count;

	/* Nginx configuration */
	struct nginx_conf nginx_conf[NGINX_MAXN];
	/* Number of elements in %nginx_conf% array */
	int nginx_count;

	/* Memcache configuration */
	struct memcache_conf memcache_conf[MEMCACHE_MAXN];
	/* Number of elements in %memcache_conf% array */
	int memcache_count;

	/* Exec configuration */
	struct exec_conf exec_conf[EXEC_MAXN];
	/* Number of elements in %exec_conf% array */
	int exec_count;

	/* Sockets configuration */
	struct socket_conf socket_conf[SOCKET_MAXN];
	/* Number of elements in %socket_conf% array */
	int socket_count;
	/* Interval for socket LA polling (default to 1) */
	int socket_interval;

	/* This flag shows whether ussd will calculate HDD's load averages */
	int f_disable_hdds_la;

	/* This flag shows whether ussd will watch point-point interfaces */
	int f_skip_p2p_interfaces;
};


extern struct conf conf;


void parse_command_line(int, char **);
void read_config_file(void);

