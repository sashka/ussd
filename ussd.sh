#!/bin/sh
#
# Written by Vadim Guchenko <yhw@rambler-co.ru>
#
# $Id: ussd.sh 46846 2008-04-11 11:29:25Z yhw $
#

case $1 in
start)
	/usr/local/sbin/ussd -as -u root && echo "ussd started"
	;;
stop)
	if [ -r /var/run/uss/ussd.pid ]; then
		kill `cat /var/run/uss/ussd.pid` && echo "ussd stopped"
	fi
	;;
restart)
	$0 stop
	sleep 1
	$0 start
	;;
*)
	echo "Usage: $0 (start|stop|restart)"
esac
