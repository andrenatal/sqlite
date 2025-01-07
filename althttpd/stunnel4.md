Setup For HTTPS Using Stunnel4
==============================

Older versions of althttpd did not support encryption.  The recommended
way of encrypting website using althttpd was to
use [stunnel4](https://www.stunnel.org/).  This advice has now changed.
We now recommend that you update your althttpd to version 2.0 or later
and use the xinetd technique described in the previous section.  This
section is retained for historical reference.

On the sqlite.org website, the relevant lines of the
/etc/stunnel/stunnel.conf file are:

> ~~~
cert = /etc/letsencrypt/live/sqlite.org/fullchain.pem
key = /etc/letsencrypt/live/sqlite.org/privkey.pem
[https]
accept       = :::443
TIMEOUTclose = 0
exec         = /usr/bin/althttpd
execargs     = /usr/bin/althttpd -logfile /logs/http.log -root /home/www -user www-data -https 1
~~~

This setup is very similar to the xinetd setup.  One key difference is
the "-https 1" option is used to tell althttpd that the connection is
encrypted.  This is important so that althttpd will know to set the
HTTPS environment variable for CGI programs.

It is ok to have both xinetd and stunnel4 both configured to
run althttpd, at the same time. In fact, that is the way that the
SQLite.org website worked for many users.  (The SQLite.org website
now using the TLS built into SQLite.) Requests to <http://sqlite.org/> go through
xinetd and requests to <https://sqlite.org/> go through stunnel4.
