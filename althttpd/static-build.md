Building A Stand-alone Binary
=============================

This document describes how to build a completely self-contained,
statically linked "althttpd" binary for
Linux (or similar unix-like operating systems).

What You Need
-------------

  1.  The `althttpd.c` source code file from this website.
  2.  A tarball of the latest [OpenSSL](https://openssl.org) release.
  3.  The usual unix build tools, "make", "gcc", and friends.
  4.  The OpenSSL build requires Perl.

Build Steps
-----------

<ol type="1">
<li><b>Compiling a static OpenSSL library</b>.
<p>
<ul>
<li> Unpack the OpenSSL tarball.  Rename the top-level directory to "openssl".
<li> CD into the openssl directory.
<li> Run these commands:
<blockquote><pre>
./config no-ssl3 no-weak-ssl-ciphers no-shared no-threads --openssldir=/usr/lib/ssl
CFLAGS=-Os make -e
</pre></blockquote>
<li> Fix a cup of tea while OpenSSL builds....
</ul>

<p>
<li><b>Compiling `althttpd`</b>
<p>
<ul>
<li> CD back up into the top-level directory where the `althttpd.c`
     source file lives.
<li> Run:
<blockquote><pre>
gcc -I./openssl/include -Os -Wall -Wextra -DENABLE_TLS &#92;
  -o althttpd althttpd.c -L./openssl -lssl -lcrypto -ldl
</pre></blockquote>
<li> The command in the previous bullet builds a binary that is statically
     linked against OpenSSL, but is still dynamically linked against
     system libraries. To make the binary completely static, add the
     `-static` option.
<li> To the `gcc` command above, you may optionally add
     `-DALTHTTPD_VERSION='...'` where the quoted text is the version number
     of althttpd that will be reported to CGI scripts.
</ul>
</ol>

Using The Binary
----------------

The above is all you need to do.  After compiling, simply move the
resulting binary into the /usr/bin directory of your server.
