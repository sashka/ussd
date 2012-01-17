#!/bin/sh -e
#
# Written by Vadim Guchenko <yhw@rambler-co.ru>
#
# $Id: update_ussd_in_hosts_allow.sh 112401 2012-01-12 12:57:01Z dark $
#

cat > /etc/hosts.allow.ussd <<"EOD"
ussd : 127.0.0.1 : allow
ussd : 81.19.75.224/255.255.255.224 : allow
ussd : ALL : deny
EOD
cat /etc/hosts.allow | grep -v '^ussd[[:blank:]]*:' >> /etc/hosts.allow.ussd

cp /etc/hosts.allow /etc/hosts.allow.old
mv /etc/hosts.allow.ussd /etc/hosts.allow
echo "/etc/hosts.allow updated"

