Setup Using Xinetd
==================

Shown below is the complete text of the /etc/xinetd.d/http file on
sqlite.org that configures althttpd to serve unencrypted
HTTP requests on both IPv4 and IPv6.
You can use this as a template to create your own installations.

> ~~~
service http
{
  port = 80
  flags = IPv4
  socket_type = stream
  wait = no
  user = root
  server = /usr/bin/althttpd
  server_args = -logfile /logs/http.log -root /home/www -user www-data
  bind = 45.33.6.223
}
service http
{
  port = 80
  flags = REUSE IPv6
  bind = 2600:3c00::f03c:91ff:fe96:b959
  socket_type = stream
  wait = no
  user = root
  server = /usr/bin/althttpd
  server_args = -logfile /logs/http.log -root /home/www -user www-data
}
~~~
    

The key observation here is that each incoming TCP/IP connection on 
port 80 launches a copy of /usr/bin/althttpd with some additional
arguments that amount to the configuration for the webserver.

Notice that althttpd is run as the superuser. This is not required, but if it
is done, then althttpd will move itself into a chroot jail at the root
of the web document hierarchy (/home/www in the example) and then drop
all superuser privileges prior to reading any content off of the wire.
The -user option tells althttpd to become user www-data after entering
the chroot jail.

The -root option (which should always be an absolute path) tells althttpd
where to find the document hierarchy. In the case of sqlite.org, all
content is served from /home/www. At the top level of this document hierarchy
is a bunch of directories whose names end with ".website".  Each such
directory is a separate website.  The directory is chosen based on the
Host: parameter of the incoming HTTP request.  A partial list of the
directories on sqlite.org is this:

>
    3dcanvas_tcl_lang_org.website
    3dcanvas_tcl_tk.website
    androwish_org.website
    canvas3d_tcl_lang_org.website
    canvas3d_tcl_tk.website
    cvstrac_org.website
    default.website
    fossil_scm_com.website
    fossil_scm_hwaci_com.website
    fossil_scm_org.website
    system_data_sqlite_org.website
    wapp_tcl_lang_org.website
    wapp_tcl_tk.website
    www2_alt_mail_net.website
    www_androwish_org.website
    www_cvstrac_org.website
    www_fossil_scm_com.website
    www_fossil_scm_org.website
    www_sqlite_org.website
    
For each incoming HTTP request, althttpd takes the text of the Host:
parameter in the request header, converts it to lowercase, and changes
all characters other than ASCII alphanumerics into "_".  The result
determines which subdirectory to use for content.  If nothing matches,
the "default.website" directory is used as a fallback.

For example, if the Host parameter is "www.SQLite.org" then the name is
translated into "www\_sqlite\_org.website" and that is the directory
used to serve content.  If the Host parameter is "fossil-scm.org" then
the "fossil\_scm\_org.website" directory is used.  Oftentimes, two or
more names refer to the same website.  For example, fossil-scm.org,
www.fossil-scm.org, fossil-scm.com, and www.fossil-scm.com are all the
same website.  In that case, typically only one of the directories is
a real directory and the others are symbolic links.

On a minimal installation that only hosts a single website, it suffices
to have a single subdirectory named "default.website".

Within the *.website directory, the file to be served is selected by
the HTTP request URI.  Files that are marked as executable are run
as CGI.  Non-executable files with a name that ends with ".scgi"
and that have content of the form "SCGI hostname port" relay an SCGI
request to hostname:port. All other non-executable files are delivered
as-is.

If the request URI specifies the name of a directory within *.website,
then althttpd appends "/home", "/index.html", and "/index.cgi", in
that order, looking for a match.

If a prefix of a URI matches the name of an executable file then that
file is run as CGI.  For as-is content, the request URI must exactly
match the name of the file.

For content delivered as-is, the MIME-type is deduced from the filename
extension using a table that is compiled into althttpd.

Supporting HTTPS using Xinetd
-----------------------------

Beginning with version 2.0 (2022-01-16), althttpd optionally support
TLS-encrypted connections.  Setting up an HTTPS website using Xinetd
is very similar to an HTTP website.  The appropriate configuration for
xinetd is a single file named "https" in the /etc/xinetd.d directory
with content like the following:

> ~~~
service https
{
  port = 443
  flags = IPv4
  socket_type = stream
  wait = no
  user = root
  server = /usr/bin/althttpd
  server_args = -logfile /logs/http.log -root /home/www -user www-data -cert /etc/letsencrypt/live/sqlite.org/fullchain.pem -pkey /etc/letsencrypt/live/sqlite.org/privkey.pem
  bind = 45.33.6.223
}
service https
{
  port = 443
  flags = REUSE IPv6
  bind = 2600:3c00::f03c:91ff:fe96:b959
  socket_type = stream
  wait = no
  user = root
  server = /usr/bin/althttpd
  server_args = -logfile /logs/http.log -root /home/www -user www-data -cert /etc/letsencrypt/live/sqlite.org/fullchain.pem -pkey /etc/letsencrypt/live/sqlite.org/privkey.pem
}
~~~

You will, of course, want to adjust pathnames and IP address so that they
are appropriate for your particular installation.

This https configuration file is the same as the previous http
configuration file with just a few changes:

   *   Change the service name from "http" to "https"
   *   Change the port number from 80 to 443
   *   Add -cert and -pkey options to althttpd so that it will know where
       to find the appropriate certificate and private-key.

After creating the new https configuration file, simply restart
xinetd (usually with the command "`/etc/init.d/xinetd restart`") and
immediately an HTTPS version of your existing website will spring into
existance.
