# cattoy

cattoy loads website logs into an SQLite virtual table where they can be played with using SQL queries.

It requires a website that follows EuPathDB's file naming and location conventions so that the log file can be located for a given hostname.

It currently only supports the Apache HTTPD access log. Support for Tomcat's catalina and WDK application logs are future desires.

### Requirements

- sqlite3 with support for external modules. The stock CentOS sqlite package meets this requirement.

- a website that follows EuPathDB's file naming and location conventions so that the log file can be located for a given hostname. Alternatively the `access_log.so` module can be manually loaded into an `sqlite3` session and a virtual table manually created for an apache log file.

### Build

    $ make

This should generate a `httpd.so` file. The `cattoy` shell script will load this into an `sqlite3` session.

### Usage

Invoke the `cattoy` script with the desired website hostname.

    cattoy <hostname>

Invoke desired SQL queries against the `access_log` table.

    cattoy> SELECT url, status
       ...> FROM access_log
       ...> WHERE remote_host = '208.65.89.219'
       ...> AND request like '%gbrowse%'
       ...> AND method = 'POST';
    /cgi-bin/gbrowse/trichdb/|200
    /cgi-bin/gbrowse/trichdb/|200
    /cgi-bin/gbrowse/trichdb/|200
    /cgi-bin/gbrowse/trichdb/|200

See SQLite documentation for supported SQL syntax.

The columns of the`access_log` table can be listed using the `table_info` pragma.

    cattoy> PRAGMA table_info(access_log);

    0|remote_host|TEXT|0||0
    1|remote_user|TEXT|0||0
    2|time|TEXT|0||0
    3|request|TEXT|0||0
    4|status|INTEGER|0||0
    5|bytes|INTEGER|0||0
    ...

### Alternative Usage

If you want to forego the `cattoy` script, the `access_log.so` module can be manually loaded into an `sqlite3` session and a virtual table manually created for an apache log file.

Start the CLI client

    $ sqlite3

Load the module,

    sqlite> .load httpd.so

Create a virtual table. Gzip compressed logs are supported.

    sqlite> create virtual table access_log using weblog("/var/log/httpd/dev.trichdb.org/access_log-20140101.gz");

