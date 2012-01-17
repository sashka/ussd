/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: ussd.c 112401 2012-01-12 12:57:01Z dark $
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <paths.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <tcpd.h>

#include "vg_lib/vg_signals.h"
#include "conf.h"
#include "stats.h"


/* Connection queue length (backlog parameter of listen(2) function) */
#define LISTEN_QUEUE		64

/* Timeout in seconds for select(2) function. Should be small enough to call
   periodic functions between select(2) calls */
#define SELECT_TIMEOUT		1

/* Maximum time in seconds to serve each client connection */
#define CLIENT_TIMEOUT		20


/* This flag shows whether pid file created or not */
int f_pidfile = 0;

/* This flag shows whether ussd started as daemon or not */
int f_started = 0;

/* This flag shows whether current process is client process or parent
   process */
int f_client = 0;


void set_uid_gid(void);
int create_listening_socket(void);
void write_pid(void);
void reap_children(void);
void terminate(void);

/*****************************************************************************/
int main(int argc, char **argv) {
	int listen_fd, conn_fd, max_fd, nready;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	fd_set all_fdset, read_fdset;
	struct timeval timeout;
	struct request_info request;
	pid_t pid;

	/* tune messages */
	strcpy(msg_debug_prefix, "DEBUG");
	strcpy(msg_err_prefix, "ERROR");

	/* parse command line arguments */
	parse_command_line(argc, argv);

	/* initialize syslog */
	openlog("ussd", LOG_PID, LOG_DAEMON);
	/* all further messages should be sent both to stderr and syslog */
	f_msg_syslog = 1;

	/* register function to be called on exit */
	if (atexit(terminate) < 0)
		msg_syserr(1, "%s: atexit", __FUNCTION__);

	/* change UID and GID if requested */
	set_uid_gid();

	/* read configuration file */
	read_config_file();

	/* block all signals */
	sig_block();

	/* open signal pipe */
	sig_pipe_open();

	/* set "catch" action for some signals */
	sig_catch(SIGCHLD);
	sig_catch(SIGHUP);
	sig_catch(SIGTERM);

	/* create listening socket */
	listen_fd = create_listening_socket();

	/* become a daemon */
	if (daemon(0, 0) < 0)
		msg_syserr(1, "%s: daemon", __FUNCTION__);
	f_started = 1;

	/* all further messages should be sent to syslog only */
	f_msg_stderr = 0;
	msg_notice("ussd started");

	/* change working directory */
	if (chdir(conf.workdir) < 0) {
		msg_syswarn("%s: chdir(%s)", __FUNCTION__, conf.workdir);
		chdir(DFL_WORKDIR);
	}

	/* clear file mode creation mask */
	umask(0);

	/* write the process ID to pid file */
	write_pid();

	FD_ZERO(&all_fdset);
	FD_SET(sig_pipe[0], &all_fdset);
	FD_SET(listen_fd, &all_fdset);
	max_fd = VG_MAX(sig_pipe[0], listen_fd);
	bzero(&timeout, sizeof(timeout));

	for (;;) {
		/* unblock all signals */
		sig_unblock();

		/* wait for a new connection, signal or timeout */
		read_fdset = all_fdset;
	    timeout.tv_sec = SELECT_TIMEOUT;
		nready = select(max_fd + 1, &read_fdset, NULL, NULL, &timeout);
		if (nready < 0) {
			if (errno == EINTR)
				continue;
			else
				msg_syserr(1, "%s: select", __FUNCTION__);
		}

		/* block all signals */
		sig_block();
#ifndef __linux__
		/* call periodic functions */
		update_iface_counters();
#if __FreeBSD_version >= 500000
		update_hdds_counters();
#endif
#else
//#define DEBUG printf("Here: %s:%d\n", __FILE__, __LINE__);
		update_hdds_counters();
#endif // __linux__
		update_socket_counters();
		/* select() timeout */
		if (nready == 0)
			continue;

		/* some signals received */
		if (FD_ISSET(sig_pipe[0], &read_fdset)) {
			/* process signals */
			if (f_sig[SIGCHLD]) {
				reap_children();
			} else if (f_sig[SIGHUP]) {
				read_config_file();
			} else if (f_sig[SIGTERM]) {
				exit(EXIT_SUCCESS);
			}
			/* clear signals */
			sig_clear();
		}

		/* new connection available */
		if (FD_ISSET(listen_fd, &read_fdset)) {
			/* accept client connection */
			client_addr_size = sizeof(client_addr);
			if ((conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr,
			    &client_addr_size)) < 0) {
				msg_syserr(0, "%s: accept", __FUNCTION__);
				sleep(1);
				continue;
			}
			/* check hosts access control files before serving client
			   connection */
			if (conf.f_hosts_access) {
				request_init(&request, RQ_DAEMON, "ussd", RQ_FILE, conn_fd, 0);
				fromhost(&request);
				if (!hosts_access(&request)) {
					msg_warn("connection from %s (%s) rejected",
					    eval_hostaddr(request.client),
					    eval_hostname(request.client));
					close(conn_fd);
					continue;
				}
			}
			/* do fork to serve client connection */
			if ((pid = fork()) == 0) { /* child */
				f_client = 1;
				/* close all parent descriptors */
				close(listen_fd);
				sig_pipe_close();
				/* set default action for all modified signals */
				sig_default(SIGCHLD);
				sig_default(SIGHUP);
				sig_default(SIGTERM);
				/* unblock all signals */
				sig_unblock();
				/* set timeout */
				alarm(CLIENT_TIMEOUT);
				/* process client connection */
				process_connection(conn_fd);
				exit(EXIT_SUCCESS);
			} else if (pid > 0) { /* parent */
				msg_info("[%d] connection started from %s",
				    pid, inet_ntoa(client_addr.sin_addr));
				close(conn_fd);
			} else {
				msg_syserr(0, "%s: can't fork", __FUNCTION__);
				close(conn_fd);
				sleep(1);
				continue;
			}
		}
	}
}

/*****************************************************************************
 * Sets UID and GID of the current process to %conf.user% and %conf.group%
 * respectively. %conf.user% and %conf.group% can be names or numeric IDs.
 * If both %conf.user% and %conf.group% are empty strings, does nothing.
 *****************************************************************************/
void set_uid_gid() {
	u_short uid_tmp, gid_tmp;
	uid_t uid;
	gid_t gid;
	int f_uid, f_gid;
	struct passwd *pw;
	struct group *gr;
	char *p;

	uid = 0;
	gid = 0;
	f_uid = 0;
	f_gid = 0;

	/* process user */
	if (*conf.user) {
		if (parse_get_usint(conf.user, &p, &uid_tmp) && !*p) {
			uid = uid_tmp;
			f_uid = 1;
		}
		if (f_uid) {
			if ((pw = getpwuid(uid)) == NULL)
				msg_err(1, "%s: getpwuid: unknown user ID %hu",
				    __FUNCTION__, uid_tmp);
		} else {
			if ((pw = getpwnam(conf.user)) == NULL)
				msg_err(1, "%s: getpwnam: unknown user name '%s'",
				    __FUNCTION__, conf.user);
		}
		uid = pw->pw_uid;
		gid = pw->pw_gid;
		f_uid = 1;
		f_gid = 1;
	}

	/* process group */
	if (*conf.group) {
		f_gid = 0;
		if (parse_get_usint(conf.group, &p, &gid_tmp) && !*p) {
			gid = gid_tmp;
			f_gid = 1;
		}
		if (f_gid) {
			if ((gr = getgrgid(gid)) == NULL)
				msg_err(1, "%s: getgrgid: unknown group ID %hu",
				    __FUNCTION__, gid_tmp);
		} else {
			if ((gr = getgrnam(conf.group)) == NULL)
				msg_err(1, "%s: getgrnam: unknown group name '%s'",
				    __FUNCTION__, conf.group);
		}
		gid = gr->gr_gid;
		f_gid = 1;
	}

	/* try to set new group ID of the current process */
	if (f_gid)
		if (setgid(gid) < 0)
			msg_syserr(1, "%s: setgid", __FUNCTION__);

	/* try to set new user ID of the current process */
	if (f_uid)
		if (setuid(uid) < 0)
			msg_syserr(1, "%s: setuid", __FUNCTION__);
}

/*****************************************************************************
 * Creates listening socket. Returns descriptor of created socket.
 *****************************************************************************/
int create_listening_socket() {
	int fd, optval;
	struct sockaddr_in addr;

	/* create socket */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		msg_syserr(1, "%s: socket", __FUNCTION__);

	/* set SO_REUSEADDR socket option */
	optval = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		msg_syserr(1, "%s: setsockopt(SO_REUSEADDR)", __FUNCTION__);

	/* bind socket */
	bzero(&addr, sizeof(addr));
	addr.sin_family 	= AF_INET;
	addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	addr.sin_port 		= htons(conf.listen_port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		msg_syserr(1, "%s: bind", __FUNCTION__);

	/* make socket listening */
	if (listen(fd, LISTEN_QUEUE) < 0)
		msg_syserr(1, "%s: listen", __FUNCTION__);

	return(fd);
}

/*****************************************************************************
 * Writes the process ID to file %conf.pidfile% and sets %f_pidfile% flag if
 * successful.
 *****************************************************************************/
void write_pid() {
	FILE *pid;

	if ((pid = fopen(conf.pidfile, "w")) == NULL) {
		msg_syswarn("can't create pid file '%s'", conf.pidfile);
		return;
	}
	f_pidfile = 1;
	if (fprintf(pid, "%d\n", getpid()) < 0) {
		msg_syswarn("can't write process ID to file '%s'", conf.pidfile);
		fclose(pid);
		return;
	}
	if (fchmod(fileno(pid), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) < 0) {
		msg_syswarn("can't chmod on pid file '%s'", conf.pidfile);
		fclose(pid);
		return;
	}
	fclose(pid);
}

/*****************************************************************************
 * Reaps any already available children.
 *****************************************************************************/
void reap_children() {
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status))
			msg_info("[%d] connection finished: exited with status %d",
			    pid, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			msg_info("[%d] connection finished: killed by signal %d",
			    pid, WTERMSIG(status));
		else
			msg_info("[%d] connection finished", pid);
	}
}

/*****************************************************************************
 * Function to be called on exit.
 *****************************************************************************/
void terminate() {
	/* clients should be terminated silently */
	if (f_client)
		return;

	/* delete pid file if it was created */
	if (f_pidfile)
		if (unlink(conf.pidfile) < 0)
			msg_syswarn("can't delete pid file '%s'", conf.pidfile);

	/* write notice */
	if (f_started)
		msg_notice("ussd stopped");
	else
		msg_notice("ussd can't start");
}

