## Prerequisites to installation on AlmaLinux 8.6

    1. sqlite 3.0.26
    2. sqlite-devel 3.0.26 
    3. httpd 2.4.37
    4. httpd-devel 2.4.37

## Installing mod_sqlite

    make
    sudo make install

## Configuring Apache with mod_sqlite

Edit your `/etc/httpd/conf/httpd.conf` and add the following line to tell Apache to load mod_sqlite:

    LoadModule sqlite_module /usr/lib64/httpd/modules/mod_sqlite.so

Once Apache has the module loaded, you can set up a location to host your
SQLite database.  Here is an example of the simplest mod_sqlite location
configuration:

    <Location /test>
        SetHandler sqlite-handler
        SQLite On
    </Location>

Access control can be used for different location through Apache's built in
user control system.  Check out Apache documentation for Authentication if you
need more information ( http://httpd.apache.org/docs-2.0/howto/auth.html ).
Here is an example of mod_sqlite using basic authentication:

    <Location /test>
        AuthType Basic
        AuthName "Test"
        AuthUserFile /usr/local/apache2/conf/passwords
        Require valid-user

        SetHandler sqlite-handler
        SQLite On
    </Location>

## Using mod_sqlite:

mod_sqlite allows you to access your SQLite database over HTTP.  To do this,
the Location that you have configured mod_sqlite to use will take query
parameters that allow the user to query the database.  Here is a list of those
parameters, and what they do:

    'db' -- The 'db' parameter specifies what file the SQLite database resides
            in.

    'q'  -- The 'q' parameter specifies what SQL query should be executed.

Usage examples:

To query the database `/var/tmp/foo.txt` with the SQL `select * from test`, the
query would look like this:

    http://localhost/test?db=/var/tmp/foo.txt&q=select%20*%20from%20test

Return values:

After executing a query, mod_sqlite will return the results URI encoded and
separated by semicolons.  The first line of the results will always be the
column names corresponding to the data.  Each row is separated by a newline.
A result set might look something like this:

    id;name;password
    1;Aaron;1234
    2;Bill;secret

Error handling:

If any errors occur with SQLlite mod_sqlite returns HTTP_OK with SQLite error message starting with "ERROR:".

### Advanced Apache Configuration

mod_sqlite provides a few other Apache configuration directives that allow you
to tweak the way mod_sqlite performs queries.  Here is a list of directives
and what they do:

    SQLiteDB /foo/bar.txt

SQLiteDB specifies what database mod_sqlite should read from.  mod_sqlite will
read from that specified database and ignore the 'db' query parameter if
SQLiteDB is set.

    SQLiteQuery "select * from test"

SQLiteQuery specifies what query mod_sqlite should execute.  mod_sqlite will
ignore the 'q' query parameter if SQLiteQuery is set in the Apache
configuration.

    SQLiteBaseDir /home/aaron

SQLiteBaseDir specifies where mod_sqlite will look for databases.  So if the
'db' parameter is set to 'foo.txt', then mod_sqlite will try to read
'/home/aaron/foo.txt'.  If SQLiteBaseDir is not set, mod_sqlite assumes that the
'db' parameter is a complete file path to the database.

Example configuration:

The following configuration will only execute "select * from test" on the
database "/tmp/foo.txt" no matter what the user passes in:

    <Location /test>
        SetHandler sqlite-handler
        SQLite On
        SQLiteDB /tmp/foo.txt
        SQLiteQuery "select * from test"
    </Location>
