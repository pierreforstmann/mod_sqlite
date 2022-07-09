/*
 * Copyright (C) 2004 Aaron Patterson.
 *
 * This file is released under the GPL.  Read the COPYING file for details
 * on distribution, or see http://www.gnu.org/copyleft/gpl.html
 *
 * Contact information:
 *  Aaron Patterson <perlfiend@users.sourceforge.net>
 *  http://modsqlite.sourceforge.net/
 *
 * 02-JUL-2022 Pierre Forstmann
 * Adapted to sqlite3.
 */
#include "apr_buckets.h"
#include "util_filter.h"

#include "ap_config.h"
#include "ap_provider.h"
#include "apr_strings.h"
#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_connection.h"
#include "http_protocol.h"
#include "http_request.h"
#include "ap_mmn.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(sqlite);
#endif

#include <sqlite3.h>
#include <sys/stat.h>

#define PREPARED_DATA_PARAM     "p"
#define SQL_STATEMENT_PARAM     "q"
#define DB_FILE_PARAM           "db"

#define PROTOCOL_VERSION        "0.9"
#define PROTOCOL_HEADER         "X-SQLite-Protocol"
#define ERROR_HEADER            "X-SQLite-Error"

module AP_MODULE_DECLARE_DATA sqlite;

typedef struct {
    int enable;
    char *base_dir;
    char *db_file;
    char *query;
} SQLiteConfig;

/* Struct used for passing around in callback functions.
 *
 * *array is used for prepared data.
 * *r is the apache request.
 * flag is a flag used to print out the column titles once.
 */
typedef struct {
    apr_array_header_t *array;
    request_rec *r;
    int flag;
} SQLiteCBStruct;

/*
 * Utility Functions
 */
void util_unescape_plus(char *string) {
    unsigned char * c = (unsigned char *)string;
    while(*c) {
        if(*c == '+') {
            *c = ' ';
        }
        c++;
    }
}

static int util_parse_params(request_rec *r, apr_table_t **params,
        const char *args) {
    const char * val;
    char delim = '&';

    if(! args) return OK;

    if(strstr(args, ";") != NULL) {
        delim = ';';
    }

    if(*params) {
        apr_table_clear(*params);
    } else {
        *params = apr_table_make(r->pool, 4);
    }
    while(*args && (val = ap_getword(r->pool, &args, delim))) {
        const char *key = ap_getword(r->pool, &val, '=');
        util_unescape_plus((char *)key);
        util_unescape_plus((char *)val);
        ap_unescape_url((char *)key);
        ap_unescape_url((char *)val);
     	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "key=<%s> val=<%s>", key, val);
        apr_table_add(*params, key, val);
    }
    return OK;
}

/*  Based off Example 10-4 Writing Apache modules in perl and C. */
static int util_read(request_rec *r, const char **rbuf) {
    int rc;

    /* Force client to use nonchunked transfer. */
    if((rc = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR)) != OK) {
        return rc;
    }

    if(ap_should_client_block(r)) {
        char argsbuffer[HUGE_STRING_LEN];
        int rsize, len_read, rpos = 0;
        long length = r->remaining;
        *rbuf = (char *)apr_pcalloc(r->pool, length + 1);

        while((len_read = ap_get_client_block(
                        r, argsbuffer, sizeof(argsbuffer))) > 0) {
            if((rpos + len_read) > length) {
                rsize = length - rpos;
            } else {
                rsize = len_read;
            }
            memcpy((char*)*rbuf + rpos, argsbuffer, rsize);
            rpos += rsize;
        }
    }
    return rc;
}

static int util_parse_get(request_rec *r, apr_table_t **params) {
    if(r->method_number != M_GET) return DECLINED;
    return util_parse_params(r, params, r->args);
}

static int util_parse_post(request_rec *r, apr_table_t **params) {
    const char *data;
    int rc = OK;

    if(r->method_number != M_POST) return DECLINED;

    if((rc = util_read(r, &data)) != OK) {
        return rc;
    }

    return util_parse_params(r, params, data);
}

/* Escape the ampersand. */
static char *escape_amp(request_rec *r, char *string) {
    char *copy = (char *)apr_palloc(r->pool, 3 * strlen(string) + 3);
    const unsigned char *t = (const unsigned char*)string;
    unsigned char *s = (unsigned char *)copy;
    unsigned char c;

    while((c = *t) != '\0') {
        if(c == '&') {
            *s++ = '%';
            *s++ = '2';
            *s++ = '6';
        } else 
            *s++ = c;
        t++;
    }
    *s = '\0';

    return copy;
}


int sqlite_cb(void *p_arg, int argc, char **argv, char **column_names) {
    int i;
    SQLiteCBStruct *cb_struct = (SQLiteCBStruct *)p_arg;
    request_rec *r = cb_struct->r;

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                "sqlite callback called");
    if(argc > 0) {
        /* Print out the column names. */
        if(cb_struct->flag == 0) {
            for(i = 0; i < argc; i++) {
                ap_rprintf(r, "%s",
                        escape_amp(r, ap_escape_uri(r->pool, column_names[i])));
                if(i != argc - 1) 
                    ap_rputs(";", r);
            }
            ap_rputs("\n", r);
            cb_struct->flag = 1;
        }

        /* Print out the row. */
        if(argv != NULL) {
            for(i = 0; i < argc; i++) {
                ap_rprintf(r, "%s",
                        escape_amp(r, ap_escape_uri(r->pool, argv[i])));
                if(i != argc - 1) 
                    ap_rputs(";", r);
            }
        }
        ap_rputs("\n", r);
    }
    return 0;
}

/*
 * Main apache functions
 */
/*static void *create_sqlite_dir_config(apr_pool_t *p, server_rec *s) */
void *create_sqlite_dir_config(apr_pool_t *p, char *path)
{
    SQLiteConfig *config = apr_pcalloc(p, sizeof(SQLiteConfig));
    config->enable = 0;
    config->base_dir = NULL;
    config->db_file = NULL;
    config->query = NULL;

    return (void *)config;
}

static const char *sqlite_on(cmd_parms *cmd, void *mconfig, int arg)
{
    SQLiteConfig *config = (SQLiteConfig *)mconfig;
    config->enable = arg;
    return NULL;
}

static const char *sqlite_base_dir(cmd_parms *cmd, void *mconfig, const char *arg)
{
    SQLiteConfig *config = (SQLiteConfig *)mconfig;
    config->base_dir = (char *)apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *sqlite_db_file(cmd_parms *cmd, void *mconfig, const char *arg)
{
    SQLiteConfig *config = (SQLiteConfig *)mconfig;
    config->db_file = (char *)apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *sqlite_query(cmd_parms *cmd, void *mconfig, const char *arg)
{
    SQLiteConfig *config = (SQLiteConfig *)mconfig;
    config->query = (char *)apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static int sqlite_handler(request_rec *r)
{
    char *errmsg, *db_name, *query;
    int err;
    struct stat db_stat;
    int rc;
    sqlite3_stmt *res;

    sqlite3 *db;
    apr_table_t *args = NULL;
    SQLiteCBStruct sqlite_cb_s;
    SQLiteConfig *config = (SQLiteConfig *)
        ap_get_module_config(r->per_dir_config, &sqlite_module);

    if (strcmp(r->handler, "sqlite-handler")) {
        return DECLINED;
    }

    /* Handle the module configuration. */
    if(config != NULL) {
        if (!config->enable) {
           return DECLINED;
        }
    } else {
        return DECLINED;
    }
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                "base_dir: %s", config->base_dir);

    /* Finish up if it's just a HEAD request. */
    if (r->header_only) {
        return OK;
    }

    /* We only handle GET's and POST's. */
    if(util_parse_get(r, &args) == OK) {
    } else if(util_parse_post(r, &args) == OK) {
    } else {
        return DECLINED;
    }
    /*
     * ! This is not SQLite version.
     *
     *ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
     *           "SQLite version: %s", VERSION);
     */

    /* Set the content type. */
    ap_set_content_type(r, "text");

    /* Set the protocol version in the headers. */
    apr_table_set(r->err_headers_out, PROTOCOL_HEADER,
        PROTOCOL_VERSION);

    /* If we're configured with a database file, *only* use that file. */
    if(config->db_file != NULL) {
        db_name = config->db_file;
    } else {
        db_name = (char *)apr_table_get(args, DB_FILE_PARAM);
        if(! db_name) {
            apr_table_set(r->err_headers_out, ERROR_HEADER,
                    "No Database name specified");
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        /* Strip out any relative path components. */
        ap_getparents(db_name);
    }

    /* If there is a base directory, prepend it to the db_name. */
    if(config->base_dir != NULL) {
        char *new_db_name = (char *)apr_pstrcat(
                r->pool, config->base_dir, "/", db_name, NULL);
        ap_no2slash(new_db_name);
        db_name = new_db_name;
    }

    /* See if the file actually exists. */

    if(stat(db_name, &db_stat) != 0) {
    	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                "db_name: %s not found errno=%d", db_name, errno);
        return HTTP_NOT_FOUND;
    }

    if(config->query != NULL) {
        query = config->query;
    } else {
        query = (char *)apr_table_get(args, SQL_STATEMENT_PARAM);
    }

    if(! query) {
        apr_table_set(r->err_headers_out, ERROR_HEADER,
                "No query specified");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    sqlite_cb_s.r = r;
    sqlite_cb_s.flag = 0;  /* Set a flag for printing header info once. */

    /* Open the database connection. */
    rc = sqlite3_open(db_name, &db);
    if(rc != SQLITE_OK) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                "Error opening db %s: %s", db_name, sqlite3_errmsg(db));
        apr_table_set(r->err_headers_out, ERROR_HEADER, sqlite3_errmsg(db));
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                "Connected to database: %s", db_name);

    /*
     * retrieve SQLite version
     */

    rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);    
    if (rc != SQLITE_OK) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                "Error executing query: %s", "SELECT SQLITE VERSION()");
        return HTTP_INTERNAL_SERVER_ERROR;
    }    

    rc = sqlite3_step(res);
    if (rc == SQLITE_ROW) {
    	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "Connected to SQLite version: %s",
                     sqlite3_column_text(res, 0));
    } else {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                "Failed to get SQLite version: %s", sqlite3_errmsg(db));
        return HTTP_INTERNAL_SERVER_ERROR;
    }


    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "query=<%s>",
            query);

    /* Execute the processed SQL statement. */
    if((err =
            sqlite3_exec(db, query, sqlite_cb , &sqlite_cb_s, &errmsg))
            != SQLITE_OK) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                "Error executing query: %s", sqlite3_errstr(err));
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                "Error executing query: %s", errmsg);
        apr_table_set(r->err_headers_out, ERROR_HEADER, errmsg);
        sqlite3_free(errmsg);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "before free(pq)");
        sqlite3_free(query);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "after free(pq)");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    
    return OK;
}

static void register_hooks(apr_pool_t *p)
{
  ap_hook_handler(sqlite_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

static const command_rec sqlite_cmds[] = 
{
    AP_INIT_FLAG("SQLite", sqlite_on, NULL, ACCESS_CONF,
                 "Run an SQLite server on this host"),
    AP_INIT_TAKE1("SQLiteBaseDir", sqlite_base_dir, NULL, ACCESS_CONF,
                 "Base directory for SQLite to find databases"),
    AP_INIT_TAKE1("SQLiteDB", sqlite_db_file, NULL, ACCESS_CONF,
                 "Specify a database file that SQLite will always use"),
    AP_INIT_TAKE1("SQLiteQuery", sqlite_query, NULL, ACCESS_CONF,
                 "Specify a query that SQLite will use"),
    { NULL }
};

module AP_MODULE_DECLARE_DATA sqlite_module = {
    STANDARD20_MODULE_STUFF,
    create_sqlite_dir_config,		/* create per-directory config structure */
    NULL,		/* merge per-directory config structures */
    NULL,	/* create per-server config structure */
    NULL,			/* merge per-server config structures */
    sqlite_cmds,			/* command apr_table_t */
    register_hooks		/* register hooks */
};
