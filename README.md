mod_sqlite is an Apache module which provides access to SQLite databases over HTTP.  
See README2004.txt for original README.

Please see INSTALL.md for how to install and configure mod_sqlite.

In July 2022 I have adapted mod_sqlite to sqlite3 and published it on GitHub.

First tests have been run on Alma Linux 8.6 with Apache 2.4.37:
<ul>
<li>SQLite database can been only created in /tmp after disabling PrivateTmp=True in /usr/lib/systemd/system/httpd.service.</li>
<li>If SQLite database is created in /var and SE Linux is enabled Apache may not be able to access database.</li>
<ul>
