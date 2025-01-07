Setting Up A Website Using Althttpd, Linode, and Systemd
========================================================

Here are notes on how I recently (2024-01-16) set up a website on an
inexpensive [Linode](https://linode.com) VPS using alhttpd,
[Let's Encrypt](https://letsencrypt.org) and systemd.

If you have suggestions or error reports about this document,
post a message [on the forum](/forum).

1.0 Setup A Linode Account And Spin Up A VPS
--------------------------------------------

Create an account at Linode and spin up a new VPS.  You can
get them as cheap as $5/month.  For this example, I used a
$12/month linode which is probably overkill.  I'll probably
downgrade to the $5/month plan at some point.

I selected Ubuntu 23.10 as the OS because that was the latest
available Ubuntu at the time.  Use whatever other Linux distro
you are more comfortable with.

2.0 Register Your Domain
------------------------

The system I build uses a subdomain off of a domain I already
owned, so I didn't have to purchase a new domain name.  If that is
not your case, go ahead and purchase the domain name now.  Details
on how to do that are outside the scope of this document, but there
are many good tutorials on the web.

Point your domain at the Linode nameservers.  Make whatever
DNS entries are needed depending on what you are trying to do.
Linode provides an excellent and easy-to-use interface for this.


3.0 Initial System Configuration
--------------------------------

Log into your VPS as root.  I needed to upgrade and install
new software as follows:

> ~~~~
apt update
apt upgrade
apt install letsencrypt
~~~~

4.0 Create The Webserver User and Home Directory
------------------------------------------------

There should already be a user named "www-data".  (Check the
/etc/passwd file.)  That's the user I use for web services.

I also manually edited the /etc/passwd file to change the
default shell for user www-data from /usr/bin/nologin to
/bin/bash.  That change allows me to run the "`su www-data`"
command to become the www-data user, while messing with files that
belong to that user.  But this step is entirely optional.

Create a home directory for this user at /home/www.
Create a subdirectory /home/www/default.website.  Change
the owner of the subdirectory to www-data.  The command
sequence is this:

> ~~~~
mkdir -p /home/www/default.website
chown www-data /home/www/default.website
~~~~

Create a file named /home/www/default.website/index.html
that is readable by the www-data user and put in some
HTML content as a place holder.  Perhaps something like
this:

> ~~~~
<h1>Hello, World</h1>
<p>If you can see this, that means the web server is running.</p>
~~~~

Add whatever additional content you want.  Note, however, that
files that have the execute permission bit set will be run as
CGI.  So make sure none of the files anywhere in the the *.website
folder or in any subfolder are executable unless you really do
intend them to be run as CGI.

5.0 Build A Binary For althttpd
-------------------------------

I built a statically linked althttpd binary on my desktop Linux
machine (using [these instructions](./static-build.md)) and
used scp to transfer the static binary up to the VPS.  Install
the static binary at /usr/bin/althttpd


6.0 Systemd Setup For HTTP Service
----------------------------------

You need to get the simple HTTP (unencrypted) service going first,
as this is a prerequisite for getting a certificate from Let's Encrypt.
Create a file at /etc/systemd/system/http.socket that looks like this:

> ~~~~
> [Unit]
> Description=HTTP socket
>
> [Socket]
> Accept=yes
> ListenStream=80
> NoDelay=true
> 
> [Install]
> WantedBy=sockets.target
> ~~~~

Then create another file named /etc/systemd/system/http@.service
like this:

> ~~~~
> [Unit]
> Description=HTTP socket server
> After=network-online.target
> 
> [Service]
> WorkingDirectory=/home/www
> ExecStart=/usr/bin/althttpd -root /home/www -user www-data
> StandardInput=socket
>
> [Install]
> WantedBy=multi-user.target
> ~~~~

That "@" in the filename is not a typo.  It seems to be needed
by systemd for some reason.  I don't know the details.

Finally, start up the new service using this sequence of commands:

> ~~~~
systemctl daemon-reload
systemctl enable http.socket
systemctl start http.socket
~~~~

At that point you should be able to point a web-browser at port 80 of
your VPS and see the place-hold HTML that you installed at the end of
step 4.0.  You can also check on the status of your service, or shut it
down using commands like these:

> ~~~~
systemctl status http.socket
systemctl stop http.socket
~~~~

7.0 Getting A Let's Encrypt Certificate
---------------------------------------

You need a cert in order to use HTTPS.  Get one using a command
similar to the following:

> ~~~~
letsencrypt certonly --webroot -w /home/www/default.website -d your-domain.org
~~~~

Substitue your actual domain name in place of "`your-domain.org`", of course.
If you want your webserver to serve multiple domains, you can use multiple
"-d" options.  See the letsencrypt documentation for details.

I *believe* that this command sets things up so that the domain will
automatically renew itself.  You shouldn't ever need to run this command
again.  I'll come back and correct this paragraph if I later discover
that I was wrong.

Your cert will be found in files named something like:

> ~~~~
/etc/letsencrypt/live/your-domain.org/fullchain.pem
/etc/letsencrypt/live/your-domain.org/privkey.pem
~~~~

You should make sure the "privkey.pem" file remains secure.  This is you
private key.  This is what the webserver uses to verify your server's
identity to strangers.

8.0 Activating HTTPS
--------------------

Now that you have a cert, you can make additional systemd configuration
entries to serve TLS HTTPS requests arriving on port 443.  First
create a file named /etc/systemd/system/https.socket that looks like this:

> ~~~~
> [Unit]
> Description=HTTPS socket
>
> [Socket]
> Accept=yes
> ListenStream=443
> NoDelay=true
> 
> [Install]
> WantedBy=sockets.target
> ~~~~

Then create another file named /etc/systemd/system/https@.service
like this:

> ~~~~
> [Unit]
> Description=HTTPS socket server
> After=network-online.target
> 
> [Service]
> WorkingDirectory=/home/www
> ExecStart=/usr/bin/althttpd -root /home/www -user www-data -cert /etc/letsencrypt/live/your-domain.org/fullchain.pem -pkey /etc/letsencrypt/live/your-domain.org/privkey.pem
> StandardInput=socket
>
> [Install]
> WantedBy=multi-user.target
> ~~~~

These two files are very similar to the ones created for HTTP service
in step 6.0 above.  The key differences:

  *  The filenames say "`https`" instead of "`http`".
  *  The TCP port is 443 instead of 80
  *  The althttpd command has extra -cert and -pkey arguments to
     identify your certificate.

After creating these two files, run:

> ~~~~
systemctl daemon-reload
systemctl enable https.socket
systemctl start https.socket
~~~~

After doing so, you should then be able to browser your website
using a TLS encrypted connection.

9.0 Logging Web Traffic
-----------------------

If you want to log your web traffic (recommended), create a new
directory named /home/www/log and change the owner to www-data.
Then edit the *.service files created in steps 6.0 and 8.0 above
to add the term "`--log /log/http.log`" to the "`ExecStart=...`" line.

Notice that the filename is "/log/http.log", not "/home/www/log/http.log".
That is because althttpd will do a [chroot](https://en.wikipedia.org/wiki/Chroot)
to the /home/www directory before it does anything else.  This is a security
feature to prevent an error in a CGI script or something from compromising
your system.  Because of the chroot, the /home/www/log/http.log" file
will really be called "/log/http.log" from the point of view of althttpd.

After making these changes run:

> ~~~~
systemctl restart http.socket
systemctl restart https.socket
~~~~

10.0 Other Configuration Changes
--------------------------------

You can now tinker with other configuration changes.  Adding an option
like "`--ipshun /ipshun`" is recommended to help quash malicious spiders.
You might also want to add new "*.website" folders under "/home/www"
for specific domains.  See other althttpd documentation for guidance
and suggestions.  This tutorial should be sufficient to get you started.
