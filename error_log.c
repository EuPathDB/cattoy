/**

Initial starting code based on examples from
Using SQLite by Jay A. Kreibich. Copyright 2010 O'Reilly Media, Inc., 978-0-596-52118-9
*/
#define _XOPEN_SOURCE

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <time.h>

/**
The expected log format is Apache hTTPD Server's 2.3 error log format 
at "LogLevel warn". Other LogLevels will produce incompatible formats.
(versions before httpd 2.4 do not allow custom log formats).

  [Tue Nov 04 00:26:42 2014] [error] [client 208.65.89.219] which: no inkscape in (/sbin:/usr/sbin:/bin:/usr/bin)
  [Tue Nov 04 00:27:07 2014] [error] [client 208.65.89.219] [Tue Nov  4 00:27:07 2014] gbrowse: DBD::Oracle::db ping failed: ORA-03135: connection lost contact (DBD ERROR: OCISessionServerRelease) at /usr/share/perl5/vendor_perl/CGI/Session/Driver/DBI.pm line 136 during global destruction., referer: http://mheiges.trichdb.org/cgi-bin/gbrowse/trichdb/

Observation: The log level is not always included in older versions of Apache 
(whatever was on CentOS from 2011 (httpd 2.0?)). httpd 2.2 seems to always inlude
it (but I have not looked for official confirmation).

ToDo: 
I believe all log entries first two fields. Beyond that I should check log level
in the line entry and parse the remainder accordingly.

Incompatible example, LogLevel debug:
  [Tue Nov 04 13:14:32 2014] [debug] proxy_util.c(1852): proxy: worker already initialized

 **/

const static char *error_log_sql = 
"    CREATE TABLE error_log (           "
/* Columns parsed directly from log entries */
"        time                  TEXT,           "  /*  0 */
"        log_level             TEXT,           "  /*  1 */
"        client                TEXT HIDDEN,    "  /*  2 */
"        message               TEXT,           "  /*  3 */
/* The following are cols computed from other columns */
"        remote_host           TEXT,           "  /*  4 */
"        remote_host_int       INTEGER,        "  /*  5 */
"        time_day              INTEGER,        "  /*  6 */
"        time_month_s          TEXT HIDDEN,    "  /*  7 */
"        time_month            INTEGER,        "  /*  8 */
"        time_year             INTEGER,        "  /*  9 */
"        time_hour             INTEGER,        "  /* 10 */
"        time_min              INTEGER,        "  /* 11 */
"        time_sec              INTEGER,        "  /* 12 */
"        time_epoch            INTEGER,        "  /* 13 */
"        line                  TEXT HIDDEN     "  /* 14 */
"     );                                       ";

#define TABLE_COLS_SCAN   3 /* number of internal cols parsed from log entry, 
                               not including the message which is everything
                               after the can until the end of line */
#define TABLE_COLS       15 /* total columns in table: direct log + computed */


typedef struct error_log_vtab_s {
    sqlite3_vtab   vtab;
    sqlite3        *db;
    char           *filename;
} error_log_vtab;


#define LINESIZE 4096

/*
Remove leading and trailing quotes.

File names with a hyphen followed by a number, e.g 
    error_log-20141102.gz
need to be quoted, otherwise the sqlite parser seems to treat
the hyphen as a minus math operation and throws an error.
    create virtual table log using error_log('error_log-20141102.gz');
On the other hand, if we do quote then the quotes are retained
as literals in the file name so sqlite errors "missing database". 
Therefore error_log_trimquote() is used to strip the leading and
trailing quotes.
 */
static char * error_log_trimquote(const char *q_str)
{
    int q_str_len = strlen(q_str);
    int start = 0;
    int end = q_str_len -1;

    char *u_str = malloc(q_str_len);
    
    if (q_str[0] == '"' || q_str[0] == '\'' )
        start++;
    
    if (q_str[q_str_len -1] == '"' || q_str[q_str_len -1] == '\'')
        end--;

    int i;
    int j = 0;
    for (i = start; i <= end; i++) {
        u_str[j++] = q_str[i];
    }

    // null terminate string
    if (j > 0) u_str[j]=0;

    return u_str;
}

typedef struct error_log_cursor_s {
    sqlite3_vtab_cursor   cur;               /* this must be first */

    gzFile         *fptr;                    /* used to scan file */
    sqlite_int64   row;                      /* current row count (ROWID) */
    int            eof;                      /* EOF flag */

    /* per-line info */
    char           line[LINESIZE];           /* line buffer */
    int            line_len;                 /* length of data in buffer */
    int            line_ptrs_valid;          /* flag for scan data */
    char           *(line_ptrs[TABLE_COLS]); /* array of pointers */
    int            line_size[TABLE_COLS];    /* length of data for each pointer */
} error_log_cursor;

static int error_log_get_line( error_log_cursor *c )
{
    char   *cptr;
    int    rc = SQLITE_OK;

    c->row++;                          /* advance row (line) counter */
    c->line_ptrs_valid = 0;            /* reset scan flag */
    cptr = gzgets( c->fptr, c->line, LINESIZE );
    if ( cptr == NULL ) {  /* found the end of the file/error */
        if (gzeof( c->fptr ) ) {
            c->eof = 1;
        } else {
            rc = -1;
        }
        return rc;
    }
    /* find end of buffer and make sure it is the end a line... */
    cptr = c->line + strlen( c->line ) - 1;       /* find end of string */
    if ( ( *cptr != '\n' )&&( *cptr != '\r' ) ) { /* overflow? */
        char   buf[1024], *bufptr;
        /* ... if so, keep reading */
        while ( 1 ) {
            bufptr = gzgets( c->fptr, buf, sizeof( buf ) );
            if ( bufptr == NULL ) {  /* found the end of the file/error */
                if (gzeof( c->fptr ) ) {
                    c->eof = 1;
                } else {
                    rc = -1;
                }
                break;
            }
            bufptr = &buf[ strlen( buf ) - 1 ];
            if ( ( *bufptr == '\n' )||( *bufptr == '\r' ) ) {
                break;               /* found the end of this line */
            }
        }
    }

    while ( ( *cptr == '\n' )||( *cptr == '\r' ) ) {
        *cptr-- = '\0';   /* trim new-line characters off end of line */
    }
    c->line_len = ( cptr - c->line ) + 1;
    return rc;
}

static int error_log_scanline( error_log_cursor *c )
{
    char   *start = c->line, *end = NULL, next = ' ';
    int    i;

    /* clear pointers */
    for ( i = 0; i < TABLE_COLS; i++ ) {
        c->line_ptrs[i] = NULL;
        c->line_size[i] = -1;
    }

    /* process actual fields */
    for ( i = 0; i < TABLE_COLS_SCAN; i++ ) {
        next = ' ';
        while ( *start == ' ' )  start++;     /* trim whitespace */
        if (*start == '\0' )  break;          /* found the end */
        if (*start == '"' ) {
            next = '"';  /* if we started with a quote, end with one */
            start++;
        }
        else if (*start == '[' ) {
            next = ']';  /* if we started with a bracket, end with one */
            start++;
        }
        end = strchr( start, next );    /* find end of this field */
        if ( end == NULL ) {            /* found the end of the line */
            int     len = strlen ( start );
            end = start + len;          /* end now points to '\0' */
        }
        c->line_ptrs[i] = start;        /* record start */
        c->line_size[i] = end - start;  /* record length */
        while ( ( *end != ' ' )&&( *end != '\0' ) )  end++;  /* find end */
        start = end;
    }

    /* Handle entries that do not include client IP field, e.g.                                 */
    /* [Tue Nov 04 13:14:32 2014] [debug] proxy_util.c(1852): proxy: worker already initialized */
    if (strncmp( c->line_ptrs[2], "client", 6 ) != 0 ) {
      c->line_ptrs[2] = "";
      c->line_size[2] = 0;
    }

    /* message to end of line */
    c->line_ptrs[3] = start +1;
    c->line_size[3] = strlen(c->line_ptrs[3]);
    
    /* process special fields */


    /* remote_host: reduce "client 10.10.15.12" to "10.10.15.12" */
    start = strchr(c->line_ptrs[2], ' ');
    end   = strchr(c->line_ptrs[2], ']');
    if(start != NULL) {
      c->line_ptrs[4] = start + 1;
      c->line_size[4] = end - start -1;
    }

    /* remote_host_int. Copy here, convert in column() */
    c->line_ptrs[5] = c->line_ptrs[4];
    c->line_size[5] = c->line_size[4];

    /* split time string into components.   */
    /* assumes: "Tue Nov 04 00:26:42 2014"  */
    /*     idx:  012345678901234567890123   */
    if (( c->line_ptrs[0] != NULL )&&( c->line_size[0] >= 20 )) {
        start = c->line_ptrs[0];
        c->line_ptrs[ 6] = &start[ 8];   c->line_size[ 6] = 2; /* time_day   */
        c->line_ptrs[ 7] = &start[ 4];   c->line_size[ 7] = 3; /* time_mon_s */
        c->line_ptrs[ 8] = &start[ 4];   c->line_size[ 8] = 3; /* time_mon   */
        c->line_ptrs[ 9] = &start[20];   c->line_size[ 9] = 4; /* time_year  */
        c->line_ptrs[10] = &start[11];   c->line_size[10] = 2; /* time_hour  */
        c->line_ptrs[11] = &start[14];   c->line_size[11] = 2; /* time_min   */
        c->line_ptrs[12] = &start[17];   c->line_size[12] = 2; /* time_sec   */
    }

    /* time_epoch   */
       c->line_size[13] = 2;

    /* line */
    c->line_ptrs[14] = c->line;
    c->line_size[14] = c->line_len;

    c->line_ptrs_valid = 1;
    return SQLITE_OK;
}


static int error_log_connect( sqlite3 *db, void *udp, int argc, 
        const char *const *argv, sqlite3_vtab **vtab, char **errmsg )
{
    error_log_vtab  *v = NULL;
    const char   *filename = error_log_trimquote(argv[3]);
    gzFile         *ftest;

    if ( argc != 4 ) return SQLITE_ERROR;

    *vtab = NULL;
    *errmsg = NULL;

    /* test to see if filename is valid */
    ftest = gzopen( filename, "rb" );
    if ( ftest == NULL ) {
      return SQLITE_ERROR;
    }
    gzclose( ftest );

    /* alloccate structure and set data */
    v = sqlite3_malloc( sizeof( error_log_vtab ) );
    if ( v == NULL ) return SQLITE_NOMEM;
    ((sqlite3_vtab*)v)->zErrMsg = NULL; /* need to init this */

    v->filename = sqlite3_mprintf( "%s", filename );
    if ( v->filename == NULL ) {
        sqlite3_free( v );
        return SQLITE_NOMEM;
    }
    v->db = db;

    sqlite3_declare_vtab( db, error_log_sql );
    *vtab = (sqlite3_vtab*)v;
    return SQLITE_OK;
}

static int error_log_disconnect( sqlite3_vtab *vtab )
{
    sqlite3_free( ((error_log_vtab*)vtab)->filename );
    sqlite3_free( vtab );
    return SQLITE_OK;
}

static int error_log_bestindex( sqlite3_vtab *vtab, sqlite3_index_info *info )
{
    return SQLITE_OK;
}

static int error_log_open( sqlite3_vtab *vtab, sqlite3_vtab_cursor **cur )
{
    error_log_vtab     *v = (error_log_vtab*)vtab;
    error_log_cursor   *c;
    gzFile            *fptr;
    char            *cmd;

    *cur = NULL;

    fptr = gzopen( v->filename, "rb" );

    if ( fptr == NULL ) return SQLITE_ERROR;

    c = sqlite3_malloc( sizeof( error_log_cursor ) );
    if ( c == NULL ) {
        gzclose( fptr );
        return SQLITE_NOMEM;
    }
    
    c->fptr = fptr;
    *cur = (sqlite3_vtab_cursor*)c;
    return SQLITE_OK;
}

static int error_log_close( sqlite3_vtab_cursor *cur )
{
    if ( ((error_log_cursor*)cur)->fptr != NULL ) {
        gzclose( ((error_log_cursor*)cur)->fptr );
    }
    sqlite3_free( cur );
    return SQLITE_OK;
}

static int error_log_filter( sqlite3_vtab_cursor *cur,
        int idxnum, const char *idxstr,
        int argc, sqlite3_value **value )
{
    error_log_cursor   *c = (error_log_cursor*)cur;

   gzseek( c->fptr, 0, SEEK_SET );
    c->row = 0;
    c->eof = 0;
    return error_log_get_line( (error_log_cursor*)cur );
}

static int error_log_next( sqlite3_vtab_cursor *cur )
{
    return error_log_get_line( (error_log_cursor*)cur );
}

static int error_log_eof( sqlite3_vtab_cursor *cur )
{
    return ((error_log_cursor*)cur)->eof;
}

static int error_log_rowid( sqlite3_vtab_cursor *cur, sqlite3_int64 *rowid )
{
    *rowid = ((error_log_cursor*)cur)->row;
    return SQLITE_OK;
}

static int error_log_column( sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int cidx )
{
    error_log_cursor    *c = (error_log_cursor*)cur;
    struct tm tm;
    time_t epoch;

    if ( c->line_ptrs_valid == 0 ) {
        error_log_scanline( c );         /* scan line, if required */
    }
    if ( c->line_size[cidx] < 0 ) {   /* field not scanned and set */
        sqlite3_result_null( ctx );
        return SQLITE_OK;
    }

    switch( cidx ) {
    case 5: { /* convert IP address string to signed 64 bit integer */
        int            i;
        sqlite_int64   v = 0;
        char          *start = c->line_ptrs[cidx], *end, *oct[4];

        for ( i = 0; i < 4; i++ ) {
            oct[i] = start;
            end = ( start == NULL ? NULL : strchr( start, '.' ) );
            if ( end != NULL ) {
                start = end + 1;
            }
        }
        v += ( oct[3] == NULL ? 0 : atoi( oct[3] ) ); v *= 256;
        v += ( oct[2] == NULL ? 0 : atoi( oct[2] ) ); v *= 256;
        v += ( oct[1] == NULL ? 0 : atoi( oct[1] ) ); v *= 256;
        v += ( oct[0] == NULL ? 0 : atoi( oct[0] ) );
        sqlite3_result_int64( ctx, v );
        return SQLITE_OK;
    }
    case 8: {
        int m = 0;
             if ( strncmp( c->line_ptrs[cidx], "Jan", 3 ) == 0 ) m =  1;
        else if ( strncmp( c->line_ptrs[cidx], "Feb", 3 ) == 0 ) m =  2;
        else if ( strncmp( c->line_ptrs[cidx], "Mar", 3 ) == 0 ) m =  3;
        else if ( strncmp( c->line_ptrs[cidx], "Apr", 3 ) == 0 ) m =  4;
        else if ( strncmp( c->line_ptrs[cidx], "May", 3 ) == 0 ) m =  5;
        else if ( strncmp( c->line_ptrs[cidx], "Jun", 3 ) == 0 ) m =  6;
        else if ( strncmp( c->line_ptrs[cidx], "Jul", 3 ) == 0 ) m =  7;
        else if ( strncmp( c->line_ptrs[cidx], "Aug", 3 ) == 0 ) m =  8;
        else if ( strncmp( c->line_ptrs[cidx], "Sep", 3 ) == 0 ) m =  9;
        else if ( strncmp( c->line_ptrs[cidx], "Oct", 3 ) == 0 ) m = 10;
        else if ( strncmp( c->line_ptrs[cidx], "Nov", 3 ) == 0 ) m = 11;
        else if ( strncmp( c->line_ptrs[cidx], "Dec", 3 ) == 0 ) m = 12;
        else break;    /* give up, return text */
        sqlite3_result_int( ctx, m );
        return SQLITE_OK;
    }
    case  6:   /* day-of-month */
    case  9:   /* year */
    case 10:   /* hour */
    case 11:   /* minute */
    case 12:   /* second */
        sqlite3_result_int( ctx, atoi( c->line_ptrs[cidx] ) );
        return SQLITE_OK;
    case 13: { /* time_epoch */
      
      char ts[25];
      memcpy(ts, c->line_ptrs[0], 24); ts[24] = '\0';    
      
      //if ( strptime("Tue Nov 04 21:20:00 2014", "%a %b %d %H:%M:%S %Y", &tm) != NULL )
      if ( strptime(ts, "%a %b %d %H:%M:%S %Y", &tm) != NULL )
        epoch = mktime(&tm);
      else
        epoch = -1;

      sqlite3_result_int( ctx,  (epoch) );
      return SQLITE_OK;
    }
    default:
        break;
    }
    sqlite3_result_text( ctx, c->line_ptrs[cidx],
                              c->line_size[cidx], SQLITE_STATIC );
    return SQLITE_OK;
}

static int error_log_rename( sqlite3_vtab *vtab, const char *newname )
{
    return SQLITE_OK;
}


static sqlite3_module error_log_mod = {
    1,                   /* iVersion        */
    error_log_connect,      /* xCreate()       */
    error_log_connect,      /* xConnect()      */
    error_log_bestindex,    /* xBestIndex()    */
    error_log_disconnect,   /* xDisconnect()   */
    error_log_disconnect,   /* xDestroy()      */
    error_log_open,         /* xOpen()         */
    error_log_close,        /* xClose()        */
    error_log_filter,       /* xFilter()       */
    error_log_next,         /* xNext()         */
    error_log_eof,          /* xEof()          */
    error_log_column,       /* xColumn()       */
    error_log_rowid,        /* xRowid()        */
    NULL,                /* xUpdate()       */
    NULL,                /* xBegin()        */
    NULL,                /* xSync()         */
    NULL,                /* xCommit()       */
    NULL,                /* xRollback()     */
    NULL,                /* xFindFunction() */
    error_log_rename        /* xRename()       */
};

int sqlite3_extension_init( sqlite3 *db, char **error, const sqlite3_api_routines *api )
{
    SQLITE_EXTENSION_INIT2(api);
    return sqlite3_create_module( db, "error_log", &error_log_mod, NULL );
}
