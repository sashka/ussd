/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: conf.c 112401 2012-01-12 12:57:01Z dark $
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "conf.h"


/* Ussd configuration */
struct conf conf;


extern int f_started;


static void usage(void);
static void version(void);

/*****************************************************************************
 * Parses command line arguments.
 *****************************************************************************/
void parse_command_line(int argc, char **argv) {
	int op;
	char *p;
	u_int debug_level_tmp;

	/* set default values */
	conf.f_hosts_access = 0;
	conf.f_enable_smart = 0;
	snprintf(conf.configfile, sizeof(conf.configfile), DFL_CONFIGFILE);
	conf.listen_port = DFL_LISTEN_PORT;
	snprintf(conf.pidfile, sizeof(conf.pidfile), DFL_PIDFILE);
	snprintf(conf.workdir, sizeof(conf.workdir), DFL_WORKDIR);
	*conf.user = 0;
	*conf.group = 0;

	/* process command line arguments */
	while ((op = getopt(argc, argv, "ac:d:g:hp:r:su:vw:L")) != -1)
		switch(op) {
		case 'a': /* use hosts access control files */
			conf.f_hosts_access = 1;
			break;
		case 's': /* enable SMART on all HDDs */
			conf.f_enable_smart = 1;
			break;
		case 'v': /* print version */
			version();
		case 'c': /* config file */
			snprintf(conf.configfile, sizeof(conf.configfile), "%s", optarg);
			break;
		case 'd': /* debug level */
			if (!(parse_get_uint(optarg, &p, &debug_level_tmp) && !*p &&
			    debug_level_tmp <= 2))
				msg_err(1, "%s: wrong debug level: %s", __FUNCTION__, optarg);
			msg_debug_level = debug_level_tmp;
			break;
		case 'p': /* listen port */
			if (!(parse_get_uint16(optarg, &p, &conf.listen_port) && !*p))
				msg_err(1, "%s: wrong port: %s", __FUNCTION__, optarg);
			break;
		case 'r': /* pid file */
			snprintf(conf.pidfile, sizeof(conf.pidfile), "%s", optarg);
			break;
		case 'w': /* working directory */
			snprintf(conf.workdir, sizeof(conf.workdir), "%s", optarg);
			break;
		case 'u': /* run as user */
			snprintf(conf.user, sizeof(conf.user), "%s", optarg);
			break;
		case 'g': /* run as group */
			snprintf(conf.group, sizeof(conf.group), "%s", optarg);
			break;
		case 'L': /* disable HDD's load average calculating */
			conf.f_disable_hdds_la = 1;
			break;
		case 'h': /* print usage */
		case '?':
		default:
			usage();
		}
}

/*****************************************************************************
 * Prints usage to stderr and terminates the process with status EXIT_FAILURE.
 *****************************************************************************/
static void usage() {
	fprintf(stderr,
	    "usage: ussd [-ahsv] [-c configfile] [-d level] [-p port] [-r pidfile]\n"
	    "            [-w workdir] [-u user] [-g group]\n\n"
	    "options:\n"
	    "  -a             Use hosts access control files when accepting incoming\n"
	    "                 connections. See hosts_access(5).\n"
	    "  -h             Print this help.\n"
	    "  -s             Enable SMART capabilities on all HDDs while processing\n"
	    "  -L             Disable HDD load calculating on all HDDs\n"
	    "                 SMART command.\n"
	    "  -v             Print version information.\n"
	    "  -c configfile  Specify configuration file\n"
	    "                 (default: %s).\n"
	    "  -d level       Specify debug level 0-2 (default: %d).\n"
	    "  -p port        Specify listen port (default: %d).\n"
	    "  -r pidfile     Specify file where to write the process ID\n"
	    "                 (default: %s).\n"
	    "  -w workdir     Specify working directory where ussd will chdir after\n"
	    "                 becoming a daemon (default: %s).\n"
	    "  -u user        Ussd will run with the specified user name or id.\n"
	    "  -g group       Ussd will run with the specified group name or id.\n\n",
	    DFL_CONFIGFILE, 0, DFL_LISTEN_PORT, DFL_PIDFILE, DFL_WORKDIR);
	exit(EXIT_FAILURE);
}

/*****************************************************************************
 * Prints ussd version number to stdout and terminates the process with status
 * EXIT_SUCCESS.
 *****************************************************************************/
static void version() {
	printf("ussd version %u.%u.%u\n", (u_int)MAJOR_VERSION,
	    (u_int)MINOR_VERSION, (u_int)REVISION);
	exit(EXIT_SUCCESS);
}

/*****************************************************************************
 * Reads configuration file.
 *****************************************************************************/
void read_config_file() {
	char line[INPUT_LINE_MAXLEN + 1], *p, *q, *r;
	int f_line_too_long, f_used, line_number, i;
	char var[VAR_MAXLEN + 1], *var_b, *var_e;
	char command[SHELL_COMMAND_MAXLEN + 1];
	uint32_t ip;
	struct in_addr in_addr;
	uint16_t port;
	uint8_t f_unixsock;
	FILE *f;
	char ipv6_any[] = "::";

	/* set default values */
	conf.apache_count = 0;
	conf.nginx_count = 0;
	conf.memcache_count = 0;
	conf.socket_count = 0;
	conf.socket_interval = 0;
	conf.exec_count = 0;

	/* open config file */
	if ((f = fopen(conf.configfile, "r")) == NULL) {
		msg_syswarn("%s: fopen(%s)", __FUNCTION__, conf.configfile);
		return;
	}

	f_line_too_long = 0;
	line_number = 0;
	while (fgets(line, sizeof(line), f)) {
		/* remove end of line for easy parsing */
		parse_chomp(line);

		/* line too long, ignoring */
		if (strlen(line) == (sizeof(line) - 1)) {
			f_line_too_long = 1;
			continue;
		}

		line_number++;

		/* skip the rest of too long line */
		if (f_line_too_long) {
			f_line_too_long = 0;
			msg_err(0, "%s: line %d: line is too long", __FUNCTION__, line_number);
			continue;
		}

		/* remove trailing white spaces for easy parsing */
		parse_rtrim(line);

		/* do parsing */
		/* empty lines and comments */
		if (!*line || parse_get_ch(line, &p, '#')) {
			continue;
		} else if (parse_get_str(line, &p, "apache")) {
			/* format: apache <variable> <ip> <port> */
			if (parse_get_wspace(p, &var_b) &&
			    parse_get_chset(var_b, &var_e, VAR_CHSET, -(int)(sizeof(var) - 1)) &&
			    parse_get_wspace(var_e, &p) &&
			    parse_get_ip4(p, &p, &ip) &&
			    parse_get_wspace(p, &p) &&
			    parse_get_uint16(p, &p, &port) && !*p) {
				strncpy(var, var_b, var_e - var_b);
				var[var_e - var_b] = 0;
				parse_tolower(var);

				/* check if variable is already used */
				f_used = 0;
				for (i = 0; i < conf.apache_count; i++)
					if (strcmp(conf.apache_conf[i].var, var) == 0) {
						f_used = 1;
						break;
					}
				if (f_used) {
					msg_err(0, "%s: line %d: dublicated variable '%s'", __FUNCTION__, line_number, var);
					continue;
				}

				/* check if too many apache directives */
				if (conf.apache_count == APACHE_MAXN) {
					msg_err(0, "%s: line %d: too many 'apache' directives (maximum %d allowed)", __FUNCTION__, line_number, APACHE_MAXN);
					continue;
				}

				/* add line to apache configuration */
				strcpy(conf.apache_conf[conf.apache_count].var, var);
				conf.apache_conf[conf.apache_count].ip = ip;
				in_addr.s_addr = ip;
				strcpy(conf.apache_conf[conf.apache_count].ip_str, inet_ntoa(in_addr));
				conf.apache_conf[conf.apache_count].port = port;
				conf.apache_count++;
			} else
				msg_err(0, "%s: line %d: can't parse 'apache' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "nginx")) {
			/* format: nginx <variable> <ip> <port> */
			if (parse_get_wspace(p, &var_b) &&
			    parse_get_chset(var_b, &var_e, VAR_CHSET, -(int)(sizeof(var) - 1)) &&
			    parse_get_wspace(var_e, &p) &&
			    parse_get_ip4(p, &p, &ip) &&
			    parse_get_wspace(p, &p) &&
			    parse_get_uint16(p, &p, &port) && !*p) {
				strncpy(var, var_b, var_e - var_b);
				var[var_e - var_b] = 0;
				parse_tolower(var);

				/* check if variable is already used */
				f_used = 0;
				for (i = 0; i < conf.nginx_count; i++)
					if (strcmp(conf.nginx_conf[i].var, var) == 0) {
						f_used = 1;
						break;
					}
				if (f_used) {
					msg_err(0, "%s: line %d: dublicated variable '%s'", __FUNCTION__, line_number, var);
					continue;
				}

				/* check if too many nginx directives */
				if (conf.nginx_count == NGINX_MAXN) {
					msg_err(0, "%s: line %d: too many 'nginx' directives (maximum %d allowed)", __FUNCTION__, line_number, NGINX_MAXN);
					continue;
				}

				/* add line to nginx configuration */
				strcpy(conf.nginx_conf[conf.nginx_count].var, var);
				conf.nginx_conf[conf.nginx_count].ip = ip;
				in_addr.s_addr = ip;
				strcpy(conf.nginx_conf[conf.nginx_count].ip_str, inet_ntoa(in_addr));
				conf.nginx_conf[conf.nginx_count].port = port;
				conf.nginx_count++;
			} else
				msg_err(0, "%s: line %d: can't parse 'nginx' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "memcache")) {
			/* format 1: memcache <variable> <ip> <port> */
			/* format 2: memcache <variable> <sockname> */
			if (parse_get_wspace(p, &var_b) &&
			    parse_get_chset(var_b, &var_e, VAR_CHSET, -(int)(sizeof(var) - 1)) &&
			    parse_get_wspace(var_e, &p) &&
			    ((parse_get_ip4(p, &q, &ip) && parse_get_wspace(q, &q) &&
			    parse_get_uint16(q, &q, &port) && (f_unixsock = 0, 1)) ||
			    (parse_get_chset(p, &q, "^ \t", -SOCKNAME_MAXLEN) &&
			    (f_unixsock = 1, 1))) &&
			    !*q) {
				strncpy(var, var_b, var_e - var_b);
				var[var_e - var_b] = 0;
				parse_tolower(var);

				/* check if variable is already used */
				f_used = 0;
				for (i = 0; i < conf.memcache_count; i++)
					if (strcmp(conf.memcache_conf[i].var, var) == 0) {
						f_used = 1;
						break;
					}
				if (f_used) {
					msg_err(0, "%s: line %d: dublicated variable '%s'", __FUNCTION__, line_number, var);
					continue;
				}

				/* check if too many memcache directives */
				if (conf.memcache_count == MEMCACHE_MAXN) {
					msg_err(0, "%s: line %d: too many 'memcache' directives (maximum %d allowed)", __FUNCTION__, line_number, MEMCACHE_MAXN);
					continue;
				}

				/* add line to memcache configuration */
				strcpy(conf.memcache_conf[conf.memcache_count].var, var);
				conf.memcache_conf[conf.memcache_count].f_unixsock = f_unixsock;
				if (f_unixsock) {
					strncpy(conf.memcache_conf[conf.memcache_count].sockname,
					    p, q - p);
					conf.memcache_conf[conf.memcache_count].sockname[q - p] = 0;
				} else {
					conf.memcache_conf[conf.memcache_count].ip = ip;
					conf.memcache_conf[conf.memcache_count].port = port;
				}
				conf.memcache_count++;
			} else
				msg_err(0, "%s: line %d: can't parse 'memcache' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "socket")) {
			/* format 1: socket tcp|udp <variable> <ip> <port> */
			/* format 2: socket (tcp|udp)6 <variable> <ip6> <port> */
			/* format 3: socket unix <variable> <path> */
			if (parse_get_wspace(p, &var_b) &&
			parse_get_chset(var_b, &var_e, VAR_CHSET, -(int)(sizeof(var) - 1)) &&
			parse_get_wspace(var_e, &p) && (
			(((parse_get_str(p, &q, "tcp") && (f_unixsock = 0, 1)) ||
			 (parse_get_str(p, &q, "udp") && (f_unixsock = 1, 1))) &&
			 parse_get_wspace(q, &q) &&
			 (parse_get_ip4(q, &q, &ip) ||
			  (parse_get_ch(q, &q, '*') && (ip = 0, 1))) &&
			 parse_get_wspace(q, &q) &&
			 parse_get_uint16(q, &q, &port)) ||
			(((parse_get_str(p, &q, "tcp6") && (f_unixsock = 2, 1)) ||
			 (parse_get_str(p, &q, "udp6") && (f_unixsock = 3, 1))) &&
			 parse_get_wspace(q, &p) &&
			 (parse_get_chset(p, &r, "0123456789aAbBcCdDeEfF:.", -64) ||
			  (parse_get_ch(p, &r, '*') && (p = ipv6_any, 1))) &&
			 parse_get_wspace(r, &q) && parse_get_uint16(q, &q, &port)) ||
			(parse_get_str(p, &r, "unix") && (f_unixsock = 4, 1) &&
			 parse_get_wspace(r, &r) &&
			 parse_get_chset(r, &q, "^ \t", -SOCKNAME_MAXLEN))) &&
			!*q) {
				strncpy(var, var_b, var_e - var_b);
				var[var_e - var_b] = 0;
				parse_tolower(var);

				/* check if variable is already used */
				f_used = 0;
				for (i = 0; i < conf.socket_count; i++)
					if(strcmp(conf.socket_conf[i].var, var) == 0) {
						f_used = 1;
						break;
					}
				if (f_used) {
					msg_err(0, "%s: line %d: dublicated variable '%s'", __FUNCTION__, line_number, var);
					continue;
				}

				/* check if too many socket directives */
				if (conf.socket_count == SOCKET_MAXN) {
					msg_err(0, "%s: line %d: too many 'socket' directives (maximum %d allowed)", __FUNCTION__, line_number, SOCKET_MAXN);
					continue;
				}

				/* add line to socket configuration */
				strcpy(conf.socket_conf[conf.socket_count].var, var);
				conf.socket_conf[conf.socket_count].type = f_unixsock;
				bzero(&conf.socket_conf[conf.socket_count].sockaddr, sizeof(conf.socket_conf[conf.socket_count].sockaddr));
				if (f_unixsock == 4) {
					conf.socket_conf[conf.socket_count].sockaddr.sun.sun_family = AF_LOCAL;
					strncpy(conf.socket_conf[conf.socket_count].sockaddr.sun.sun_path,
					r, q - r);
					conf.socket_conf[conf.socket_count].sockaddr.sun.sun_path[q - p] = 0;
				} else if (f_unixsock == 0 || f_unixsock == 1) {
					conf.socket_conf[conf.socket_count].sockaddr.sin.sin_family = AF_INET;
					conf.socket_conf[conf.socket_count].sockaddr.sin.sin_port = htons(port);
					conf.socket_conf[conf.socket_count].sockaddr.sin.sin_addr.s_addr = ip;
				} else {
					conf.socket_conf[conf.socket_count].sockaddr.sin6.sin6_family = AF_INET6;
					conf.socket_conf[conf.socket_count].sockaddr.sin6.sin6_port = htons(port);
					*r = 0;
					if (inet_pton(AF_INET6, p, &conf.socket_conf[conf.socket_count].sockaddr.sin6.sin6_addr) != 1) {
						msg_err(0, "%s: line %d: can't parse ipv6 addr %s", __FUNCTION__, line_number, p);
						continue;
					}
				}
				conf.socket_count++;
			} else
				msg_err(0, "%s: line %d: can't parse 'socket' directive, error at (%d) ^%s, q=%s, r=%s", __FUNCTION__, line_number, q-p, p, q, r);
		} else if (parse_get_str(line, &p, "exec")) {
			/* format: exec <command> */
			if (parse_get_wspace(p, &p) &&
			    (strlen(p) < sizeof(command))) {
				strcpy(command, p);

				/* check if too many exec directives */
				if (conf.exec_count == EXEC_MAXN) {
					msg_err(0, "%s: line %d: too many 'exec' directives (maximum %d allowed)", __FUNCTION__, line_number, EXEC_MAXN);
					continue;
				}

				/* add line to exec configuration */
				strcpy(conf.exec_conf[conf.exec_count].command, command);
				conf.exec_count++;
			} else
				msg_err(0, "%s: line %d: can't parse 'exec' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "sock_la_interval")) {
			/* format: sock_la_interval <seconds> */
			if (!parse_get_wspace(p, &p) ||
			    !parse_get_int(p, &p, &conf.socket_interval) ||
			    (*p))
				msg_err(0, "%s: line %d: can't parse 'sock_la_interval' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "no_smart_enable")) {
			if (!*p) {
				conf.f_enable_smart = 0;
			} else
				msg_err(0, "%s: line %d: can't parse 'no_smart_enable' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "no_hdds_la")) {
			if (!*p) {
				conf.f_disable_hdds_la = 1;
			} else
				msg_err(0, "%s: line %d: can't parse 'no_hdds_la' directive", __FUNCTION__, line_number);
		} else if (parse_get_str(line, &p, "no_p2p_interfaces")) {
			if (!*p) {
				conf.f_skip_p2p_interfaces = 1;
			} else
				msg_err(0, "%s: line %d: can't parse 'no_p2p_interfaces' directive", __FUNCTION__, line_number);
		} else {
			msg_err(0, "%s: line %d: can't parse line", __FUNCTION__, line_number);
		}
	}
	if (ferror(f)) {
		msg_syserr(0, "%s: fgets", __FUNCTION__);
		fclose(f);
		return;
	}

	fclose(f);
	if (f_started)
		msg_info("configuration file processed");
}

