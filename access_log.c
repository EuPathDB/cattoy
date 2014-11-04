/**

Initial starting code based on examples from
Using SQLite by Jay A. Kreibich. Copyright 2010 O'Reilly Media, Inc., 978-0-596-52118-9
*/

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

/**
The expected log format is NCSA combined with the addition of %D.

"%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-agent}i\"" %D
 
%h:             Remote host
%l:             Remote logname (from identd, if supplied)
%u:             Remote user (from auth; may be bogus if return status (%s) is 401)
%t:             Time, in common log format time format (standard english format)
%r:             First line of request
%s:             Status.  For requests that got internally redirected, this is
                the status of the *original* request --- %...>s for the last.
%b:             Bytes sent, excluding HTTP headers. In CLF format
                i.e. a '-' rather than a 0 when no bytes are sent.
%{Referer}i:    The contents of Referer: header line(s) in the request
                sent to the server.
%{User-agent}i: The contents of User-agent: header line(s) in the request
                sent to the server.
%D:             The time taken to serve the request, in microseconds.
**/

const static char *access_log_sql = 
"    CREATE TABLE access_log (           "
/* Columns parsed directly from log entries */
"        remote_host           TEXT,           "  /*  0 */
"        remote_logname        TEXT HIDDEN,    "  /*  1 */
"        remote_user           TEXT,           "  /*  2 */
"        time                  TEXT,           "  /*  3 */
"        request               TEXT,           "  /*  4 */
"        status                INTEGER,        "  /*  5 */
"        bytes                 INTEGER,        "  /*  6 */
"        referer               TEXT,           "  /*  7 */
"        user_agent            TEXT,           "  /*  8 */
"        response_time         INTEGER,        "  /*  9 */
/* The following are cols computed from other columns */
"        remote_host_int       INTEGER HIDDEN, "  /* 10 */
"        time_day              INTEGER,        "  /* 11 */
"        time_month_s          TEXT HIDDEN,    "  /* 12 */
"        time_month            INTEGER,        "  /* 13 */
"        time_year             INTEGER,        "  /* 14 */
"        time_hour             INTEGER,        "  /* 15 */
"        time_min              INTEGER,        "  /* 16 */
"        time_sec              INTEGER,        "  /* 17 */
"        method                TEXT,           "  /* 18 */
"        url                   TEXT,           "  /* 19 */
"        line                  TEXT HIDDEN     "  /* 20 */
"     );                                       ";

#define TABLE_COLS_SCAN  10 /* number cols read directly from log entry */
#define TABLE_COLS       21 /* total columns in table: direct log + computed */


typedef struct access_log_vtab_s {
    sqlite3_vtab   vtab;
    sqlite3        *db;
    char           *filename;
} access_log_vtab;


#define LINESIZE 4096

/*
Remove leading and trailing quotes.

File names with a hyphen followed by a number, e.g 
    access_log-20141102.gz
need to be quoted, otherwise the sqlite parser seems to treat
the hyphen as a minus math operation and throws an error.
    create virtual table log using access_log('access_log-20141102.gz');
On the other hand, if we do quote then the quotes are retained
as literals in the file name so sqlite errors "missing database". 
Therefore access_log_trimquote() is used to strip the leading and
trailing quotes.
 */
static char * access_log_trimquote(const char *q_str)
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

typedef struct access_log_cursor_s {
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
} access_log_cursor;

static int access_log_get_line( access_log_cursor *c )
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
            bufptr =gzgets( c->fptr, buf, sizeof( buf ) );
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

static int access_log_scanline( access_log_cursor *c )
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

    /* process special fields */

    /* ip_int - just copy */
    c->line_ptrs[10] = c->line_ptrs[0];
    c->line_size[10] = c->line_size[0];

    /* assumes: "DD/MMM/YYYY:HH:MM:SS zone" */
    /*     idx:  012345678901234567890...   */
    if (( c->line_ptrs[3] != NULL )&&( c->line_size[3] >= 20 )) {
    /* timestamp field present, so likely a valid record */
        start = c->line_ptrs[3];
        c->line_ptrs[11] = &start[0];    c->line_size[11] = 2; /* time_day   */
        c->line_ptrs[12] = &start[3];    c->line_size[12] = 3; /* time_mon_s */
        c->line_ptrs[13] = &start[3];    c->line_size[13] = 3; /* time_mon   */
        c->line_ptrs[14] = &start[7];    c->line_size[14] = 4; /* time_year  */
        c->line_ptrs[15] = &start[12];   c->line_size[15] = 2; /* time_hour  */
        c->line_ptrs[16] = &start[15];   c->line_size[16] = 2; /* time_min   */
        c->line_ptrs[17] = &start[18];   c->line_size[17] = 2; /* time_sec   */
    }

    /* req_op, req_url */
    start = c->line_ptrs[4];
    end = ( start == NULL ? NULL : strchr( start, ' ' ) );
    if ( end != NULL ) {
        c->line_ptrs[18] = start; /* req_op */
        c->line_size[18] = end - start;
        start = end + 1;
    }
    end = ( start == NULL ? NULL : strchr( start, ' ' ) );
    if ( end != NULL ) {
        c->line_ptrs[19] = start;  /* req_url */
        c->line_size[19] = end - start;
    }

    /* line */
    c->line_ptrs[20] = c->line;
    c->line_size[20] = c->line_len;

    c->line_ptrs_valid = 1;
    return SQLITE_OK;
}


static int access_log_connect( sqlite3 *db, void *udp, int argc, 
        const char *const *argv, sqlite3_vtab **vtab, char **errmsg )
{
    access_log_vtab  *v = NULL;
    const char   *filename = access_log_trimquote(argv[3]);
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
    v = sqlite3_malloc( sizeof( access_log_vtab ) );
    if ( v == NULL ) return SQLITE_NOMEM;
    ((sqlite3_vtab*)v)->zErrMsg = NULL; /* need to init this */

    v->filename = sqlite3_mprintf( "%s", filename );
    if ( v->filename == NULL ) {
        sqlite3_free( v );
        return SQLITE_NOMEM;
    }
    v->db = db;

    sqlite3_declare_vtab( db, access_log_sql );
    *vtab = (sqlite3_vtab*)v;
    return SQLITE_OK;
}

static int access_log_disconnect( sqlite3_vtab *vtab )
{
    sqlite3_free( ((access_log_vtab*)vtab)->filename );
    sqlite3_free( vtab );
    return SQLITE_OK;
}

static int access_log_bestindex( sqlite3_vtab *vtab, sqlite3_index_info *info )
{
    return SQLITE_OK;
}

static int access_log_open( sqlite3_vtab *vtab, sqlite3_vtab_cursor **cur )
{
    access_log_vtab     *v = (access_log_vtab*)vtab;
    access_log_cursor   *c;
    gzFile            *fptr;
    char            *cmd;

    *cur = NULL;

    fptr = gzopen( v->filename, "rb" );

    if ( fptr == NULL ) return SQLITE_ERROR;

    c = sqlite3_malloc( sizeof( access_log_cursor ) );
    if ( c == NULL ) {
        gzclose( fptr );
        return SQLITE_NOMEM;
    }
    
    c->fptr = fptr;
    *cur = (sqlite3_vtab_cursor*)c;
    return SQLITE_OK;
}

static int access_log_close( sqlite3_vtab_cursor *cur )
{
    if ( ((access_log_cursor*)cur)->fptr != NULL ) {
        gzclose( ((access_log_cursor*)cur)->fptr );
    }
    sqlite3_free( cur );
    return SQLITE_OK;
}

static int access_log_filter( sqlite3_vtab_cursor *cur,
        int idxnum, const char *idxstr,
        int argc, sqlite3_value **value )
{
    access_log_cursor   *c = (access_log_cursor*)cur;

   gzseek( c->fptr, 0, SEEK_SET );
    c->row = 0;
    c->eof = 0;
    return access_log_get_line( (access_log_cursor*)cur );
}

static int access_log_next( sqlite3_vtab_cursor *cur )
{
    return access_log_get_line( (access_log_cursor*)cur );
}

static int access_log_eof( sqlite3_vtab_cursor *cur )
{
    return ((access_log_cursor*)cur)->eof;
}

static int access_log_rowid( sqlite3_vtab_cursor *cur, sqlite3_int64 *rowid )
{
    *rowid = ((access_log_cursor*)cur)->row;
    return SQLITE_OK;
}

static int access_log_column( sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int cidx )
{
    access_log_cursor    *c = (access_log_cursor*)cur;

    if ( c->line_ptrs_valid == 0 ) {
        access_log_scanline( c );         /* scan line, if required */
    }
    if ( c->line_size[cidx] < 0 ) {   /* field not scanned and set */
        sqlite3_result_null( ctx );
        return SQLITE_OK;
    }

    switch( cidx ) {
    case 10: { /* convert IP address string to signed 64 bit integer */
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
    case 13: { 
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
    case 5:    /* result code */
    case 6:    /* bytes transfered */
    case 11:   /* day-of-month */
    case 14:   /* year */
    case 15:   /* hour */
    case 16:   /* minute */
    case 17:   /* second */
        sqlite3_result_int( ctx, atoi( c->line_ptrs[cidx] ) );
        return SQLITE_OK;
    default:
        break;
    }
    sqlite3_result_text( ctx, c->line_ptrs[cidx],
                              c->line_size[cidx], SQLITE_STATIC );
    return SQLITE_OK;
}

static int access_log_rename( sqlite3_vtab *vtab, const char *newname )
{
    return SQLITE_OK;
}


static sqlite3_module access_log_mod = {
    1,                   /* iVersion        */
    access_log_connect,      /* xCreate()       */
    access_log_connect,      /* xConnect()      */
    access_log_bestindex,    /* xBestIndex()    */
    access_log_disconnect,   /* xDisconnect()   */
    access_log_disconnect,   /* xDestroy()      */
    access_log_open,         /* xOpen()         */
    access_log_close,        /* xClose()        */
    access_log_filter,       /* xFilter()       */
    access_log_next,         /* xNext()         */
    access_log_eof,          /* xEof()          */
    access_log_column,       /* xColumn()       */
    access_log_rowid,        /* xRowid()        */
    NULL,                /* xUpdate()       */
    NULL,                /* xBegin()        */
    NULL,                /* xSync()         */
    NULL,                /* xCommit()       */
    NULL,                /* xRollback()     */
    NULL,                /* xFindFunction() */
    access_log_rename        /* xRename()       */
};

int sqlite3_extension_init( sqlite3 *db, char **error, const sqlite3_api_routines *api )
{
    SQLITE_EXTENSION_INIT2(api);
    return sqlite3_create_module( db, "access_log", &access_log_mod, NULL );
}
