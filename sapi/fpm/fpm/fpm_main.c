/*
   +----------------------------------------------------------------------+
   | PHP Version 7                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2018 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Rasmus Lerdorf <rasmus@lerdorf.on.ca>                       |
   |          Stig Bakken <ssb@php.net>                                   |
   |          Zeev Suraski <zeev@zend.com>                                |
   | FastCGI: Ben Mansell <php@slimyhorror.com>                           |
   |          Shane Caraveo <shane@caraveo.com>                           |
   |          Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

/* $Id: cgi_main.c 291497 2009-11-30 14:43:22Z dmitry $ */

#include "php.h"
#include "php_globals.h"
#include "php_variables.h"
#include "zend_modules.h"
#include "php.h"
#include "zend_ini_scanner.h"
#include "zend_globals.h"
#include "zend_stream.h"

#include "SAPI.h"

#include <stdio.h>
#include "php.h"

#ifdef PHP_WIN32
# include "win32/time.h"
# include "win32/signal.h"
# include <process.h>
#endif

#if HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_SIGNAL_H
# include <signal.h>
#endif

#if HAVE_SETLOCALE
# include <locale.h>
#endif

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif

#include "zend.h"
#include "zend_extensions.h"
#include "php_ini.h"
#include "php_globals.h"
#include "php_main.h"
#include "fopen_wrappers.h"
#include "ext/standard/php_standard.h"

#ifdef PHP_WIN32
# include <io.h>
# include <fcntl.h>
# include "win32/php_registry.h"
#endif

#ifdef __riscos__
# include <unixlib/local.h>
int __riscosify_control = __RISCOSIFY_STRICT_UNIX_SPECS;
#endif

#include "zend_compile.h"
#include "zend_execute.h"
#include "zend_highlight.h"

#include "php_getopt.h"

#include "http_status_codes.h"

#include "fastcgi.h"

#include <php_config.h>
#include "fpm.h"
#include "fpm_request.h"
#include "fpm_status.h"
#include "fpm_conf.h"
#include "fpm_php.h"
#include "fpm_log.h"
#include "zlog.h"



// coroutine begin ====
#include <event2/event.h>
/* For sockaddr_in */  
#include <netinet/in.h>

#define CORO_DEFAULT 0
#define CORO_YIELD 1
#define CORO_END 2
#define CORO_RESUME 3
#define CORO_START 4

typedef struct _php_coroutine_context{
    jmp_buf *buf_ptr;
    zend_execute_data *execute_data;
    zend_execute_data *prev_execute_data;//execute for execute before yield
    struct _php_coroutine_context *next;
    struct _php_coroutine_context *prev;
    int coro_state;
    zend_vm_stack current_vm_stack;
    zval* current_vm_stack_top;
    zval* current_vm_stack_end;
    zend_fcall_info_cache* func_cache;
    zval *ret;
}php_coroutine_context;

static struct _g_coro_stack{
    zend_vm_stack vm_stack;
    zval* vm_stack_top;
    zval* vm_stack_end;
}g_coro_stack;

/* use to stor coroutine context */

static php_coroutine_context* current_coroutine_context = NULL;
static int coroutine_context_count = 0;

static void free_coroutine_context(php_coroutine_context* context){
    current_coroutine_context = context->next;
    if(coroutine_context_count>0){
        coroutine_context_count--;

        //unlink
        context->prev->next = context->next;
        context->next->prev = context->prev;
        //todo free all data
        efree(context->buf_ptr);
        context->buf_ptr = NULL;
        zend_vm_stack_free_call_frame(context->execute_data); //释放execute_data:销毁所有的PHP变量
        context->execute_data = NULL;
        efree(context->func_cache);
        context->func_cache = NULL;
        efree(context);
        context = NULL;
        if(coroutine_context_count == 0){
            current_coroutine_context = NULL;
        }
    }
}

ZEND_API void zend_execute_coro(zend_op_array *op_array, zval *return_value)
{
	zend_execute_data *execute_data;

	if (EG(exception) != NULL) {
		return;
	}

	execute_data = zend_vm_stack_push_call_frame(ZEND_CALL_TOP_CODE | ZEND_CALL_HAS_SYMBOL_TABLE,
		(zend_function*)op_array, 0, zend_get_called_scope(EG(current_execute_data)), zend_get_this_object(EG(current_execute_data)));
	if (EG(current_execute_data)) {
		execute_data->symbol_table = zend_rebuild_symbol_table();
	} else {
		execute_data->symbol_table = &EG(symbol_table);
	}

	zend_init_execute_data(execute_data, op_array, return_value);

	// EX(prev_execute_data) = EG(current_execute_data);
	// i_init_execute_data(execute_data, op_array, return_value);

	zend_execute_ex(execute_data);
	zend_vm_stack_free_call_frame(execute_data);
}

ZEND_API int zend_execute_scripts_coro(int type, zval *retval, int file_count, ...) /* {{{ */
{


	va_list files;
	int i;
	zend_file_handle *file_handle;
	zend_op_array *op_array;

	va_start(files, file_count);
	for (i = 0; i < file_count; i++) {
		file_handle = va_arg(files, zend_file_handle *);
		if (!file_handle) {
			continue;
		}

		op_array = zend_compile_file(file_handle, type);
		if (file_handle->opened_path) {
			zend_hash_add_empty_element(&EG(included_files), file_handle->opened_path);
		}
		zend_destroy_file_handle(file_handle);
		if (op_array) {
			zend_execute_coro(op_array, retval);
			zend_exception_restore();
			zend_try_exception_handler();
			if (EG(exception)) {
				zend_exception_error(EG(exception), E_ERROR);
			}
			destroy_op_array(op_array);
			efree_size(op_array, sizeof(zend_op_array));
		} else if (type==ZEND_REQUIRE) {
			va_end(files);
			return FAILURE;
		}
	}
	va_end(files);

	return SUCCESS;
}
/* }}} */

/* {{{ php_execute_script
 */
PHPAPI int php_execute_script_coro(zend_file_handle *primary_file)
{
	zend_file_handle *prepend_file_p, *append_file_p;
	zend_file_handle prepend_file = {{0}, NULL, NULL, 0, 0}, append_file = {{0}, NULL, NULL, 0, 0};
#if HAVE_BROKEN_GETCWD
	volatile int old_cwd_fd = -1;
#else
	char *old_cwd;
	ALLOCA_FLAG(use_heap)
#endif
	int retval = 0;

	EG(exit_status) = 0;
#ifndef HAVE_BROKEN_GETCWD
# define OLD_CWD_SIZE 4096
	old_cwd = do_alloca(OLD_CWD_SIZE, use_heap);
	old_cwd[0] = '\0';
#endif

	zend_try {
		char realfile[MAXPATHLEN];

#ifdef PHP_WIN32
		if(primary_file->filename) {
			UpdateIniFromRegistry((char*)primary_file->filename);
		}
#endif

		PG(during_request_startup) = 0;

		if (primary_file->filename && !(SG(options) & SAPI_OPTION_NO_CHDIR)) {
#if HAVE_BROKEN_GETCWD
			/* this looks nasty to me */
			old_cwd_fd = open(".", 0);
#else
			php_ignore_value(VCWD_GETCWD(old_cwd, OLD_CWD_SIZE-1));
#endif
			VCWD_CHDIR_FILE(primary_file->filename);
		}

 		/* Only lookup the real file path and add it to the included_files list if already opened
		 *   otherwise it will get opened and added to the included_files list in zend_execute_scripts
		 */
 		if (primary_file->filename &&
 		    (primary_file->filename[0] != '-' || primary_file->filename[1] != 0) &&
 			primary_file->opened_path == NULL &&
 			primary_file->type != ZEND_HANDLE_FILENAME
		) {
			if (expand_filepath(primary_file->filename, realfile)) {
				primary_file->opened_path = zend_string_init(realfile, strlen(realfile), 0);
				zend_hash_add_empty_element(&EG(included_files), primary_file->opened_path);
			}
		}

		if (PG(auto_prepend_file) && PG(auto_prepend_file)[0]) {
			prepend_file.filename = PG(auto_prepend_file);
			prepend_file.opened_path = NULL;
			prepend_file.free_filename = 0;
			prepend_file.type = ZEND_HANDLE_FILENAME;
			prepend_file_p = &prepend_file;
		} else {
			prepend_file_p = NULL;
		}

		if (PG(auto_append_file) && PG(auto_append_file)[0]) {
			append_file.filename = PG(auto_append_file);
			append_file.opened_path = NULL;
			append_file.free_filename = 0;
			append_file.type = ZEND_HANDLE_FILENAME;
			append_file_p = &append_file;
		} else {
			append_file_p = NULL;
		}
		if (PG(max_input_time) != -1) {
#ifdef PHP_WIN32
			zend_unset_timeout();
#endif
			zend_set_timeout(INI_INT("max_execution_time"), 0);
		}


		/*
		   If cli primary file has shabang line and there is a prepend file,
		   the `start_lineno` will be used by prepend file but not primary file,
		   save it and restore after prepend file been executed.
		 */
		if (CG(start_lineno) && prepend_file_p) {
			zlog(ZLOG_DEBUG, "request prepend file file count 1/2 =============\n\n");

			int orig_start_lineno = CG(start_lineno);

			CG(start_lineno) = 0;
			if (zend_execute_scripts_coro(ZEND_REQUIRE, NULL, 1, prepend_file_p) == SUCCESS) {
				CG(start_lineno) = orig_start_lineno;
				retval = (zend_execute_scripts_coro(ZEND_REQUIRE, NULL, 2, primary_file, append_file_p) == SUCCESS);
			}
		} else {

			zlog(ZLOG_DEBUG, "request prepend file file count 3 =============\n\n");

			retval = (zend_execute_scripts_coro(ZEND_REQUIRE, NULL, 3, prepend_file_p, primary_file, append_file_p) == SUCCESS);
		}
	} zend_end_try();

	if (EG(exception)) {
		zend_try {
			zend_exception_error(EG(exception), E_ERROR);
		} zend_end_try();
	}

#if HAVE_BROKEN_GETCWD
	if (old_cwd_fd != -1) {
		fchdir(old_cwd_fd);
		close(old_cwd_fd);
	}
#else
	if (old_cwd[0] != '\0') {
		php_ignore_value(VCWD_CHDIR(old_cwd));
	}
	free_alloca(old_cwd, use_heap);
#endif
	return retval;
}
/* }}} */
static int le_coroutine_php;


#define MAX_LINE 80  

void do_read(evutil_socket_t fd, short events, void *arg);  
void do_write(evutil_socket_t fd, short events, void *arg);  
  
char rot13_char(char c)  
{  
    /* We don't want to use isalpha here; setting the locale would change 
     * which characters are considered alphabetical. */  
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))  
        return c + 13;  
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))  
        return c - 13;  
    else  
        return c;  
}  
  
struct fd_state {  
    char buffer[MAX_LINE];  
    size_t buffer_used;  
  
    size_t n_written;  
    size_t write_upto;  
  
    struct event *read_event;  
    struct event *write_event;  
};  
  
struct fd_state * alloc_fd_state(struct event_base *base, evutil_socket_t fd)  
{  
    struct fd_state *state = malloc(sizeof(struct fd_state));  
    if (!state)  
        return NULL;  
  
    state->read_event = event_new(base, fd, EV_READ|EV_PERSIST, do_read, state);  
    if (!state->read_event)  
    {  
        free(state);  
        return NULL;  
    }  
  
    state->write_event = event_new(base, fd, EV_WRITE|EV_PERSIST, do_write, state);  
    if (!state->write_event)  
    {  
        event_free(state->read_event);  
        free(state);  
        return NULL;  
    }  
  
    state->buffer_used = state->n_written = state->write_upto = 0;  
  
    assert(state->write_event);  
    return state;  
}  
  
void free_fd_state(struct fd_state *state)  
{  
    event_free(state->read_event);  
    event_free(state->write_event);  
    free(state);  
}  
  
void do_read(evutil_socket_t fd, short events, void *arg)  
{  
    struct fd_state *state = arg;  
    char buf[20];  
    int i;  
    ssize_t result;  
    printf("\ncome in do_read: fd: %d, state->buffer_used: %d, sizeof(state->buffer): %d\n", fd, state->buffer_used, sizeof(state->buffer));  
    while (1)  
    {  
        assert(state->write_event);  
        result = recv(fd, buf, sizeof(buf), 0);  
        if (result <= 0)  
            break;  
        printf("recv once, fd: %d, recv size: %d, recv buff: %s\n", fd, result, buf);  
  
        for (i=0; i < result; ++i)  
        {  
            if (state->buffer_used < sizeof(state->buffer))//如果读事件的缓冲区还未满，将收到的数据做转换  
                state->buffer[state->buffer_used++] = rot13_char(buf[i]);  
//              state->buffer[state->buffer_used++] = buf[i];//接收什么发送什么，不经过转换，测试用  
            if (buf[i] == '\n') //如果遇到换行，添加写事件，并设置写事件的大小  
            {  
                assert(state->write_event);  
                event_add(state->write_event, NULL);  
                state->write_upto = state->buffer_used;  
                printf("遇到换行符，state->write_upto: %d, state->buffer_used: %d\n",state->write_upto, state->buffer_used);  
            }  
        }  
        printf("recv once, state->buffer_used: %d\n", state->buffer_used);  
    }  
    printf("read ok !!!!\n");
    //判断最后一次接收的字节数  
    if (result == 0)  
    {  
        free_fd_state(state);  
    }  
    else if (result < 0)  
    {  
        if (errno == EAGAIN) // XXXX use evutil macro  
            return;  
        perror("recv");  
        free_fd_state(state);  
    }  
}  
  
void do_write(evutil_socket_t fd, short events, void *arg)  
{  
    struct fd_state *state = arg;  
  
    printf("\ncome in do_write, fd: %d, state->n_written: %d, state->write_upto: %d\n",fd, state->n_written, state->write_upto);  
    while (state->n_written < state->write_upto)  
    {  
        ssize_t result = send(fd, state->buffer + state->n_written, state->write_upto - state->n_written, 0);  
        if (result < 0) {  
            if (errno == EAGAIN) // XXX use evutil macro  
                return;  
            free_fd_state(state);  
            return;  
        }  
        assert(result != 0);  
  
        state->n_written += result;  
        printf("send fd: %d, send size: %d, state->n_written: %d\n", fd, result, state->n_written);  
    }  
  
    if (state->n_written == state->buffer_used)  
    {  
        printf("state->n_written == state->buffer_used: %d\n", state->n_written);  
        state->n_written = state->write_upto = state->buffer_used = 1;  
        printf("state->n_written = state->write_upto = state->buffer_used = 1\n");  
        printf("close !!\n");
        close(fd);
    }  
  
    event_del(state->write_event);  
}

// coroutine end ====




#ifndef PHP_WIN32
/* XXX this will need to change later when threaded fastcgi is implemented.  shane */
struct sigaction act, old_term, old_quit, old_int;
#endif

static void (*php_php_import_environment_variables)(zval *array_ptr);

#ifndef PHP_WIN32
/* these globals used for forking children on unix systems */

/**
 * Set to non-zero if we are the parent process
 */
static int parent = 1;
#endif

static int request_body_fd;
static int fpm_is_running = 0;

static char *sapi_cgibin_getenv(char *name, size_t name_len);
static void fastcgi_ini_parser(zval *arg1, zval *arg2, zval *arg3, int callback_type, void *arg);

#define PHP_MODE_STANDARD	1
#define PHP_MODE_HIGHLIGHT	2
#define PHP_MODE_INDENT		3
#define PHP_MODE_LINT		4
#define PHP_MODE_STRIP		5

static char *php_optarg = NULL;
static int php_optind = 1;
static zend_module_entry cgi_module_entry;

static const opt_struct OPTIONS[] = {
	{'c', 1, "php-ini"},
	{'d', 1, "define"},
	{'e', 0, "profile-info"},
	{'h', 0, "help"},
	{'i', 0, "info"},
	{'m', 0, "modules"},
	{'n', 0, "no-php-ini"},
	{'?', 0, "usage"},/* help alias (both '?' and 'usage') */
	{'v', 0, "version"},
	{'y', 1, "fpm-config"},
	{'t', 0, "test"},
	{'p', 1, "prefix"},
	{'g', 1, "pid"},
	{'R', 0, "allow-to-run-as-root"},
	{'D', 0, "daemonize"},
	{'F', 0, "nodaemonize"},
	{'O', 0, "force-stderr"},
	{'-', 0, NULL} /* end of args */
};

typedef struct _php_cgi_globals_struct {
	zend_bool rfc2616_headers;
	zend_bool nph;
	zend_bool fix_pathinfo;
	zend_bool force_redirect;
	zend_bool discard_path;
	zend_bool fcgi_logging;
	char *redirect_status_env;
	HashTable user_config_cache;
	char *error_header;
	char *fpm_config;
} php_cgi_globals_struct;

/* {{{ user_config_cache
 *
 * Key for each cache entry is dirname(PATH_TRANSLATED).
 *
 * NOTE: Each cache entry config_hash contains the combination from all user ini files found in
 *       the path starting from doc_root throught to dirname(PATH_TRANSLATED).  There is no point
 *       storing per-file entries as it would not be possible to detect added / deleted entries
 *       between separate files.
 */
typedef struct _user_config_cache_entry {
	time_t expires;
	HashTable *user_config;
} user_config_cache_entry;

static void user_config_cache_entry_dtor(zval *el)
{
	user_config_cache_entry *entry = (user_config_cache_entry *)Z_PTR_P(el);
	zend_hash_destroy(entry->user_config);
	free(entry->user_config);
	free(entry);
}
/* }}} */

#ifdef ZTS
static int php_cgi_globals_id;
#define CGIG(v) TSRMG(php_cgi_globals_id, php_cgi_globals_struct *, v)
#else
static php_cgi_globals_struct php_cgi_globals;
#define CGIG(v) (php_cgi_globals.v)
#endif

#ifdef PHP_WIN32
#define TRANSLATE_SLASHES(path) \
	{ \
		char *tmp = path; \
		while (*tmp) { \
			if (*tmp == '\\') *tmp = '/'; \
			tmp++; \
		} \
	}
#else
#define TRANSLATE_SLASHES(path)
#endif

static int print_module_info(zval *zv) /* {{{ */
{
	zend_module_entry *module = Z_PTR_P(zv);
	php_printf("%s\n", module->name);
	return 0;
}
/* }}} */

static int module_name_cmp(const void *a, const void *b) /* {{{ */
{
	Bucket *f = (Bucket *) a;
	Bucket *s = (Bucket *) b;

	return strcasecmp(	((zend_module_entry *) Z_PTR(f->val))->name,
						((zend_module_entry *) Z_PTR(s->val))->name);
}
/* }}} */

static void print_modules(void) /* {{{ */
{
	HashTable sorted_registry;

	zend_hash_init(&sorted_registry, 50, NULL, NULL, 1);
	zend_hash_copy(&sorted_registry, &module_registry, NULL);
	zend_hash_sort(&sorted_registry, module_name_cmp, 0);
	zend_hash_apply(&sorted_registry, print_module_info);
	zend_hash_destroy(&sorted_registry);
}
/* }}} */

static int print_extension_info(zend_extension *ext, void *arg) /* {{{ */
{
	php_printf("%s\n", ext->name);
	return 0;
}
/* }}} */

static int extension_name_cmp(const zend_llist_element **f, const zend_llist_element **s) /* {{{ */
{
	zend_extension *fe = (zend_extension*)(*f)->data;
	zend_extension *se = (zend_extension*)(*s)->data;
	return strcmp(fe->name, se->name);
}
/* }}} */

static void print_extensions(void) /* {{{ */
{
	zend_llist sorted_exts;

	zend_llist_copy(&sorted_exts, &zend_extensions);
	sorted_exts.dtor = NULL;
	zend_llist_sort(&sorted_exts, extension_name_cmp);
	zend_llist_apply_with_argument(&sorted_exts, (llist_apply_with_arg_func_t) print_extension_info, NULL);
	zend_llist_destroy(&sorted_exts);
}
/* }}} */

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

static inline size_t sapi_cgibin_single_write(const char *str, uint str_length) /* {{{ */
{
	ssize_t ret;

	/* sapi has started which means everyhting must be send through fcgi */
	if (fpm_is_running) {
		fcgi_request *request = (fcgi_request*) SG(server_context);
		ret = fcgi_write(request, FCGI_STDOUT, str, str_length);
		if (ret <= 0) {
			return 0;
		}
		return (size_t)ret;
	}

	/* sapi has not started, output to stdout instead of fcgi */
#ifdef PHP_WRITE_STDOUT
	ret = write(STDOUT_FILENO, str, str_length);
	if (ret <= 0) {
		return 0;
	}
	return (size_t)ret;
#else
	return fwrite(str, 1, MIN(str_length, 16384), stdout);
#endif
}
/* }}} */

static size_t sapi_cgibin_ub_write(const char *str, size_t str_length) /* {{{ */
{
	const char *ptr = str;
	uint remaining = str_length;
	size_t ret;

	while (remaining > 0) {
		ret = sapi_cgibin_single_write(ptr, remaining);
		if (!ret) {
			php_handle_aborted_connection();
			return str_length - remaining;
		}
		ptr += ret;
		remaining -= ret;
	}

	return str_length;
}
/* }}} */

static void sapi_cgibin_flush(void *server_context) /* {{{ */
{
	/* fpm has started, let use fcgi instead of stdout */
	if (fpm_is_running) {
		fcgi_request *request = (fcgi_request*) server_context;
		if (
#ifndef PHP_WIN32
	      !parent &&
#endif
	      request && !fcgi_flush(request, 0)) {
			php_handle_aborted_connection();
		}
		return;
	}

	/* fpm has not started yet, let use stdout instead of fcgi */
	if (fflush(stdout) == EOF) {
		php_handle_aborted_connection();
	}
}
/* }}} */

#define SAPI_CGI_MAX_HEADER_LENGTH 1024

static int sapi_cgi_send_headers(sapi_headers_struct *sapi_headers) /* {{{ */
{
	char buf[SAPI_CGI_MAX_HEADER_LENGTH];
	sapi_header_struct *h;
	zend_llist_position pos;
	zend_bool ignore_status = 0;
	int response_status = SG(sapi_headers).http_response_code;

	if (SG(request_info).no_headers == 1) {
		return  SAPI_HEADER_SENT_SUCCESSFULLY;
	}

	if (CGIG(nph) || SG(sapi_headers).http_response_code != 200)
	{
		int len;
		zend_bool has_status = 0;

		if (CGIG(rfc2616_headers) && SG(sapi_headers).http_status_line) {
			char *s;
			len = slprintf(buf, SAPI_CGI_MAX_HEADER_LENGTH, "%s\r\n", SG(sapi_headers).http_status_line);
			if ((s = strchr(SG(sapi_headers).http_status_line, ' '))) {
				response_status = atoi((s + 1));
			}

			if (len > SAPI_CGI_MAX_HEADER_LENGTH) {
				len = SAPI_CGI_MAX_HEADER_LENGTH;
			}

		} else {
			char *s;

			if (SG(sapi_headers).http_status_line &&
				(s = strchr(SG(sapi_headers).http_status_line, ' ')) != 0 &&
				(s - SG(sapi_headers).http_status_line) >= 5 &&
				strncasecmp(SG(sapi_headers).http_status_line, "HTTP/", 5) == 0
			) {
				len = slprintf(buf, sizeof(buf), "Status:%s\r\n", s);
				response_status = atoi((s + 1));
			} else {
				h = (sapi_header_struct*)zend_llist_get_first_ex(&sapi_headers->headers, &pos);
				while (h) {
					if (h->header_len > sizeof("Status:") - 1 &&
						strncasecmp(h->header, "Status:", sizeof("Status:") - 1) == 0
					) {
						has_status = 1;
						break;
					}
					h = (sapi_header_struct*)zend_llist_get_next_ex(&sapi_headers->headers, &pos);
				}
				if (!has_status) {
					http_response_status_code_pair *err = (http_response_status_code_pair*)http_status_map;

					while (err->code != 0) {
						if (err->code == SG(sapi_headers).http_response_code) {
							break;
						}
						err++;
					}
					if (err->str) {
						len = slprintf(buf, sizeof(buf), "Status: %d %s\r\n", SG(sapi_headers).http_response_code, err->str);
					} else {
						len = slprintf(buf, sizeof(buf), "Status: %d\r\n", SG(sapi_headers).http_response_code);
					}
				}
			}
		}

		if (!has_status) {
			PHPWRITE_H(buf, len);
			ignore_status = 1;
		}
	}

	h = (sapi_header_struct*)zend_llist_get_first_ex(&sapi_headers->headers, &pos);
	while (h) {
		/* prevent CRLFCRLF */
		if (h->header_len) {
			if (h->header_len > sizeof("Status:") - 1 &&
				strncasecmp(h->header, "Status:", sizeof("Status:") - 1) == 0
			) {
				if (!ignore_status) {
					ignore_status = 1;
					PHPWRITE_H(h->header, h->header_len);
					PHPWRITE_H("\r\n", 2);
				}
			} else if (response_status == 304 && h->header_len > sizeof("Content-Type:") - 1 &&
				strncasecmp(h->header, "Content-Type:", sizeof("Content-Type:") - 1) == 0
			) {
				h = (sapi_header_struct*)zend_llist_get_next_ex(&sapi_headers->headers, &pos);
				continue;
			} else {
				PHPWRITE_H(h->header, h->header_len);
				PHPWRITE_H("\r\n", 2);
			}
		}
		h = (sapi_header_struct*)zend_llist_get_next_ex(&sapi_headers->headers, &pos);
	}
	PHPWRITE_H("\r\n", 2);

	return SAPI_HEADER_SENT_SUCCESSFULLY;
}
/* }}} */

#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif

#ifndef HAVE_ATTRIBUTE_WEAK
static void fpm_fcgi_log(int type, const char *fmt, ...) /* {{{ */
#else
void fcgi_log(int type, const char *fmt, ...)
#endif
{
	va_list args;
	va_start(args, fmt);
	vzlog("", 0, type, fmt, args);
	va_end(args);
}
/* }}} */

static size_t sapi_cgi_read_post(char *buffer, size_t count_bytes) /* {{{ */
{
	uint read_bytes = 0;
	int tmp_read_bytes;
	size_t remaining = SG(request_info).content_length - SG(read_post_bytes);

	if (remaining < count_bytes) {
		count_bytes = remaining;
	}
	while (read_bytes < count_bytes) {
		fcgi_request *request = (fcgi_request*) SG(server_context);
		if (request_body_fd == -1) {
			char *request_body_filename = FCGI_GETENV(request, "REQUEST_BODY_FILE");

			if (request_body_filename && *request_body_filename) {
				request_body_fd = open(request_body_filename, O_RDONLY);

				if (0 > request_body_fd) {
					php_error(E_WARNING, "REQUEST_BODY_FILE: open('%s') failed: %s (%d)",
							request_body_filename, strerror(errno), errno);
					return 0;
				}
			}
		}

		/* If REQUEST_BODY_FILE variable not available - read post body from fastcgi stream */
		if (request_body_fd < 0) {
			tmp_read_bytes = fcgi_read(request, buffer + read_bytes, count_bytes - read_bytes);
		} else {
			tmp_read_bytes = read(request_body_fd, buffer + read_bytes, count_bytes - read_bytes);
		}
		if (tmp_read_bytes <= 0) {
			break;
		}
		read_bytes += tmp_read_bytes;
	}
	return read_bytes;
}
/* }}} */

static char *sapi_cgibin_getenv(char *name, size_t name_len) /* {{{ */
{
	/* if fpm has started, use fcgi env */
	if (fpm_is_running) {
		fcgi_request *request = (fcgi_request*) SG(server_context);
		return fcgi_getenv(request, name, name_len);
	}

	/* if fpm has not started yet, use std env */
	return getenv(name);
}
/* }}} */

#if 0
static char *_sapi_cgibin_putenv(char *name, char *value) /* {{{ */
{
	int name_len;

	if (!name) {
		return NULL;
	}
	name_len = strlen(name);

	fcgi_request *request = (fcgi_request*) SG(server_context);
	return fcgi_putenv(request, name, name_len, value);
}
/* }}} */
#endif

static char *sapi_cgi_read_cookies(void) /* {{{ */
{
	fcgi_request *request = (fcgi_request*) SG(server_context);

	return FCGI_GETENV(request, "HTTP_COOKIE");
}
/* }}} */

static void cgi_php_load_env_var(char *var, unsigned int var_len, char *val, unsigned int val_len, void *arg) /* {{{ */
{
	zval *array_ptr = (zval*)arg;
	int filter_arg = (Z_ARR_P(array_ptr) == Z_ARR(PG(http_globals)[TRACK_VARS_ENV]))?PARSE_ENV:PARSE_SERVER;
	size_t new_val_len;

	if (sapi_module.input_filter(filter_arg, var, &val, strlen(val), &new_val_len)) {
		php_register_variable_safe(var, val, new_val_len, array_ptr);
	}
}
/* }}} */

void cgi_php_import_environment_variables(zval *array_ptr) /* {{{ */
{
	fcgi_request *request = NULL;

	if (Z_TYPE(PG(http_globals)[TRACK_VARS_ENV]) == IS_ARRAY &&
		Z_ARR_P(array_ptr) != Z_ARR(PG(http_globals)[TRACK_VARS_ENV]) &&
		zend_hash_num_elements(Z_ARRVAL(PG(http_globals)[TRACK_VARS_ENV])) > 0
	) {
		zval_dtor(array_ptr);
		ZVAL_DUP(array_ptr, &PG(http_globals)[TRACK_VARS_ENV]);
		return;
	} else if (Z_TYPE(PG(http_globals)[TRACK_VARS_SERVER]) == IS_ARRAY &&
		Z_ARR_P(array_ptr) != Z_ARR(PG(http_globals)[TRACK_VARS_SERVER]) &&
		zend_hash_num_elements(Z_ARRVAL(PG(http_globals)[TRACK_VARS_SERVER])) > 0
	) {
		zval_dtor(array_ptr);
		ZVAL_DUP(array_ptr, &PG(http_globals)[TRACK_VARS_SERVER]);
		return;
	}

	/* call php's original import as a catch-all */
	php_php_import_environment_variables(array_ptr);

	request = (fcgi_request*) SG(server_context);
	fcgi_loadenv(request, cgi_php_load_env_var, array_ptr);
}
/* }}} */

static void sapi_cgi_register_variables(zval *track_vars_array) /* {{{ */
{
	size_t php_self_len;
	char *php_self;

	/* In CGI mode, we consider the environment to be a part of the server
	 * variables
	 */
	php_import_environment_variables(track_vars_array);

	if (CGIG(fix_pathinfo)) {
		char *script_name = SG(request_info).request_uri;
		unsigned int script_name_len = script_name ? strlen(script_name) : 0;
		char *path_info = sapi_cgibin_getenv("PATH_INFO", sizeof("PATH_INFO") - 1);
		unsigned int path_info_len = path_info ? strlen(path_info) : 0;

		php_self_len = script_name_len + path_info_len;
		php_self = emalloc(php_self_len + 1);

		/* Concat script_name and path_info into php_self */
		if (script_name) {
			memcpy(php_self, script_name, script_name_len + 1);
		}
		if (path_info) {
			memcpy(php_self + script_name_len, path_info, path_info_len + 1);
		}

		/* Build the special-case PHP_SELF variable for the CGI version */
		if (sapi_module.input_filter(PARSE_SERVER, "PHP_SELF", &php_self, php_self_len, &php_self_len)) {
			php_register_variable_safe("PHP_SELF", php_self, php_self_len, track_vars_array);
		}
		efree(php_self);
	} else {
		php_self = SG(request_info).request_uri ? SG(request_info).request_uri : "";
		php_self_len = strlen(php_self);
		if (sapi_module.input_filter(PARSE_SERVER, "PHP_SELF", &php_self, php_self_len, &php_self_len)) {
			php_register_variable_safe("PHP_SELF", php_self, php_self_len, track_vars_array);
		}
	}
}
/* }}} */

/* {{{ sapi_cgi_log_fastcgi
 *
 * Ignore level, we want to send all messages through fastcgi
 */
void sapi_cgi_log_fastcgi(int level, char *message, size_t len)
{

	fcgi_request *request = (fcgi_request*) SG(server_context);

	/* ensure we want:
	 * - to log (fastcgi.logging in php.ini)
	 * - we are currently dealing with a request
	 * - the message is not empty
	 */
	if (CGIG(fcgi_logging) && request && message && len > 0) {
		ssize_t ret;
		char *buf = malloc(len + 2);
		memcpy(buf, message, len);
		memcpy(buf + len, "\n", sizeof("\n"));
		ret = fcgi_write(request, FCGI_STDERR, buf, len + 1);
		free(buf);
		if (ret < 0) {
			php_handle_aborted_connection();
		}
	}
}
/* }}} */

/* {{{ sapi_cgi_log_message
 */
static void sapi_cgi_log_message(char *message, int syslog_type_int)
{
	zlog(ZLOG_NOTICE, "PHP message: %s", message);
}
/* }}} */

/* {{{ php_cgi_ini_activate_user_config
 */
static void php_cgi_ini_activate_user_config(char *path, int path_len, const char *doc_root, int doc_root_len, int start)
{
	char *ptr;
	time_t request_time = sapi_get_request_time();
	user_config_cache_entry *entry = zend_hash_str_find_ptr(&CGIG(user_config_cache), path, path_len);

	/* Find cached config entry: If not found, create one */
	if (!entry) {
		entry = pemalloc(sizeof(user_config_cache_entry), 1);
		entry->expires = 0;
		entry->user_config = (HashTable *) pemalloc(sizeof(HashTable), 1);
		zend_hash_init(entry->user_config, 0, NULL, config_zval_dtor, 1);
		zend_hash_str_update_ptr(&CGIG(user_config_cache), path, path_len, entry);
	}

	/* Check whether cache entry has expired and rescan if it is */
	if (request_time > entry->expires) {
		char * real_path;
		int real_path_len;
		char *s1, *s2;
		int s_len;

		/* Clear the expired config */
		zend_hash_clean(entry->user_config);

		if (!IS_ABSOLUTE_PATH(path, path_len)) {
			real_path = tsrm_realpath(path, NULL);
			if (real_path == NULL) {
				return;
			}
			real_path_len = strlen(real_path);
			path = real_path;
			path_len = real_path_len;
		}

		if (path_len > doc_root_len) {
			s1 = (char *) doc_root;
			s2 = path;
			s_len = doc_root_len;
		} else {
			s1 = path;
			s2 = (char *) doc_root;
			s_len = path_len;
		}

		/* we have to test if path is part of DOCUMENT_ROOT.
		  if it is inside the docroot, we scan the tree up to the docroot
			to find more user.ini, if not we only scan the current path.
		  */
#ifdef PHP_WIN32
		if (strnicmp(s1, s2, s_len) == 0) {
#else
		if (strncmp(s1, s2, s_len) == 0) {
#endif
			ptr = s2 + start;  /* start is the point where doc_root ends! */
			while ((ptr = strchr(ptr, DEFAULT_SLASH)) != NULL) {
				*ptr = 0;
				php_parse_user_ini_file(path, PG(user_ini_filename), entry->user_config);
				*ptr = '/';
				ptr++;
			}
		} else {
			php_parse_user_ini_file(path, PG(user_ini_filename), entry->user_config);
		}

		entry->expires = request_time + PG(user_ini_cache_ttl);
	}

	/* Activate ini entries with values from the user config hash */
	php_ini_activate_config(entry->user_config, PHP_INI_PERDIR, PHP_INI_STAGE_HTACCESS);
}
/* }}} */

static int sapi_cgi_activate(void) /* {{{ */
{
	fcgi_request *request = (fcgi_request*) SG(server_context);
	char *path, *doc_root, *server_name;
	uint path_len, doc_root_len, server_name_len;

	/* PATH_TRANSLATED should be defined at this stage but better safe than sorry :) */
	if (!SG(request_info).path_translated) {
		return FAILURE;
	}

	if (php_ini_has_per_host_config()) {
		/* Activate per-host-system-configuration defined in php.ini and stored into configuration_hash during startup */
		server_name = FCGI_GETENV(request, "SERVER_NAME");
		/* SERVER_NAME should also be defined at this stage..but better check it anyway */
		if (server_name) {
			server_name_len = strlen(server_name);
			server_name = estrndup(server_name, server_name_len);
			zend_str_tolower(server_name, server_name_len);
			php_ini_activate_per_host_config(server_name, server_name_len);
			efree(server_name);
		}
	}

	if (php_ini_has_per_dir_config() ||
		(PG(user_ini_filename) && *PG(user_ini_filename))
	) {
		/* Prepare search path */
		path_len = strlen(SG(request_info).path_translated);

		/* Make sure we have trailing slash! */
		if (!IS_SLASH(SG(request_info).path_translated[path_len])) {
			path = emalloc(path_len + 2);
			memcpy(path, SG(request_info).path_translated, path_len + 1);
			path_len = zend_dirname(path, path_len);
			path[path_len++] = DEFAULT_SLASH;
		} else {
			path = estrndup(SG(request_info).path_translated, path_len);
			path_len = zend_dirname(path, path_len);
		}
		path[path_len] = 0;

		/* Activate per-dir-system-configuration defined in php.ini and stored into configuration_hash during startup */
		php_ini_activate_per_dir_config(path, path_len); /* Note: for global settings sake we check from root to path */

		/* Load and activate user ini files in path starting from DOCUMENT_ROOT */
		if (PG(user_ini_filename) && *PG(user_ini_filename)) {
			doc_root = FCGI_GETENV(request, "DOCUMENT_ROOT");
			/* DOCUMENT_ROOT should also be defined at this stage..but better check it anyway */
			if (doc_root) {
				doc_root_len = strlen(doc_root);
				if (doc_root_len > 0 && IS_SLASH(doc_root[doc_root_len - 1])) {
					--doc_root_len;
				}
#ifdef PHP_WIN32
				/* paths on windows should be case-insensitive */
				doc_root = estrndup(doc_root, doc_root_len);
				zend_str_tolower(doc_root, doc_root_len);
#endif
				php_cgi_ini_activate_user_config(path, path_len, doc_root, doc_root_len, doc_root_len - 1);
			}
		}

#ifdef PHP_WIN32
		efree(doc_root);
#endif
		efree(path);
	}

	return SUCCESS;
}
/* }}} */

static int sapi_cgi_deactivate(void) /* {{{ */
{
	/* flush only when SAPI was started. The reasons are:
		1. SAPI Deactivate is called from two places: module init and request shutdown
		2. When the first call occurs and the request is not set up, flush fails on FastCGI.
	*/
	if (SG(sapi_started)) {
		if (
#ifndef PHP_WIN32
		    !parent &&
#endif
		    !fcgi_finish_request((fcgi_request*)SG(server_context), 0)) {
			php_handle_aborted_connection();
		}
	}
	return SUCCESS;
}
/* }}} */

static int php_cgi_startup(sapi_module_struct *sapi_module) /* {{{ */
{
	if (php_module_startup(sapi_module, &cgi_module_entry, 1) == FAILURE) {
		return FAILURE;
	}
	return SUCCESS;
}
/* }}} */

/* {{{ sapi_module_struct cgi_sapi_module
 */
static sapi_module_struct cgi_sapi_module = {
	"fpm-fcgi",						/* name */
	"FPM/FastCGI",					/* pretty name */

	php_cgi_startup,				/* startup */
	php_module_shutdown_wrapper,	/* shutdown */

	sapi_cgi_activate,				/* activate */
	sapi_cgi_deactivate,			/* deactivate */

	sapi_cgibin_ub_write,			/* unbuffered write */
	sapi_cgibin_flush,				/* flush */
	NULL,							/* get uid */
	sapi_cgibin_getenv,				/* getenv */

	php_error,						/* error handler */

	NULL,							/* header handler */
	sapi_cgi_send_headers,			/* send headers handler */
	NULL,							/* send header handler */

	sapi_cgi_read_post,				/* read POST data */
	sapi_cgi_read_cookies,			/* read Cookies */

	sapi_cgi_register_variables,	/* register server variables */
	sapi_cgi_log_message,			/* Log message */
	NULL,							/* Get request time */
	NULL,							/* Child terminate */

	STANDARD_SAPI_MODULE_PROPERTIES
};
/* }}} */

/* {{{ php_cgi_usage
 */
static void php_cgi_usage(char *argv0)
{
	char *prog;

	prog = strrchr(argv0, '/');
	if (prog) {
		prog++;
	} else {
		prog = "php";
	}

	php_printf(	"Usage: %s [-n] [-e] [-h] [-i] [-m] [-v] [-t] [-p <prefix>] [-g <pid>] [-c <file>] [-d foo[=bar]] [-y <file>] [-D] [-F [-O]]\n"
				"  -c <path>|<file> Look for php.ini file in this directory\n"
				"  -n               No php.ini file will be used\n"
				"  -d foo[=bar]     Define INI entry foo with value 'bar'\n"
				"  -e               Generate extended information for debugger/profiler\n"
				"  -h               This help\n"
				"  -i               PHP information\n"
				"  -m               Show compiled in modules\n"
				"  -v               Version number\n"
				"  -p, --prefix <dir>\n"
				"                   Specify alternative prefix path to FastCGI process manager (default: %s).\n"
				"  -g, --pid <file>\n"
				"                   Specify the PID file location.\n"
				"  -y, --fpm-config <file>\n"
				"                   Specify alternative path to FastCGI process manager config file.\n"
				"  -t, --test       Test FPM configuration and exit\n"
				"  -D, --daemonize  force to run in background, and ignore daemonize option from config file\n"
				"  -F, --nodaemonize\n"
				"                   force to stay in foreground, and ignore daemonize option from config file\n"
                                "  -O, --force-stderr\n"
                                "                   force output to stderr in nodaemonize even if stderr is not a TTY\n"
				"  -R, --allow-to-run-as-root\n"
				"                   Allow pool to run as root (disabled by default)\n",
				prog, PHP_PREFIX);
}
/* }}} */

/* {{{ is_valid_path
 *
 * some server configurations allow '..' to slip through in the
 * translated path.   We'll just refuse to handle such a path.
 */
static int is_valid_path(const char *path)
{
	const char *p;

	if (!path) {
		return 0;
	}
	p = strstr(path, "..");
	if (p) {
		if ((p == path || IS_SLASH(*(p-1))) &&
			(*(p+2) == 0 || IS_SLASH(*(p+2)))
		) {
			return 0;
		}
		while (1) {
			p = strstr(p+1, "..");
			if (!p) {
				break;
			}
			if (IS_SLASH(*(p-1)) &&
				(*(p+2) == 0 || IS_SLASH(*(p+2)))
			) {
					return 0;
			}
		}
	}
	return 1;
}
/* }}} */



void test_log(char *text){

    FILE *pfile;
    size_t result;
    pfile=fopen("/Users/sioomy/work/php-src/tests/fpmtext/fpmlog.txt","a+");

    int lsize=strlen(text);//获取文件长度

    result=fwrite(text,sizeof(char),lsize,pfile);//将pfile中内容读入pread指向内存中
    fclose(pfile);
}


/* {{{ init_request_info

  initializes request_info structure

  specificly in this section we handle proper translations
  for:

  PATH_INFO
	derived from the portion of the URI path following
	the script name but preceding any query data
	may be empty

  PATH_TRANSLATED
    derived by taking any path-info component of the
	request URI and performing any virtual-to-physical
	translation appropriate to map it onto the server's
	document repository structure

	empty if PATH_INFO is empty

	The env var PATH_TRANSLATED **IS DIFFERENT** than the
	request_info.path_translated variable, the latter should
	match SCRIPT_FILENAME instead.

  SCRIPT_NAME
    set to a URL path that could identify the CGI script
	rather than the interpreter.  PHP_SELF is set to this

  REQUEST_URI
    uri section following the domain:port part of a URI

  SCRIPT_FILENAME
    The virtual-to-physical translation of SCRIPT_NAME (as per
	PATH_TRANSLATED)

  These settings are documented at
  http://cgi-spec.golux.com/


  Based on the following URL request:

  http://localhost/info.php/test?a=b

  should produce, which btw is the same as if
  we were running under mod_cgi on apache (ie. not
  using ScriptAlias directives):

  PATH_INFO=/test
  PATH_TRANSLATED=/docroot/test
  SCRIPT_NAME=/info.php
  REQUEST_URI=/info.php/test?a=b
  SCRIPT_FILENAME=/docroot/info.php
  QUERY_STRING=a=b

  but what we get is (cgi/mod_fastcgi under apache):

  PATH_INFO=/info.php/test
  PATH_TRANSLATED=/docroot/info.php/test
  SCRIPT_NAME=/php/php-cgi  (from the Action setting I suppose)
  REQUEST_URI=/info.php/test?a=b
  SCRIPT_FILENAME=/path/to/php/bin/php-cgi  (Action setting translated)
  QUERY_STRING=a=b

  Comments in the code below refer to using the above URL in a request

 */
static void init_request_info(void)
{
	fcgi_request *request = (fcgi_request*) SG(server_context);
	char *env_script_filename = FCGI_GETENV(request, "SCRIPT_FILENAME");
	char *env_path_translated = FCGI_GETENV(request, "PATH_TRANSLATED");
	char *script_path_translated = env_script_filename;
	char *ini;
	int apache_was_here = 0;


	/* some broken servers do not have script_filename or argv0
	 * an example, IIS configured in some ways.  then they do more
	 * broken stuff and set path_translated to the cgi script location */
	if (!script_path_translated && env_path_translated) {
		script_path_translated = env_path_translated;
	}

	/* initialize the defaults */
	SG(request_info).path_translated = NULL;
	SG(request_info).request_method = NULL;
	SG(request_info).proto_num = 1000;
	SG(request_info).query_string = NULL;
	SG(request_info).request_uri = NULL;
	SG(request_info).content_type = NULL;
	SG(request_info).content_length = 0;
	SG(sapi_headers).http_response_code = 200;

	/* script_path_translated being set is a good indication that
	 * we are running in a cgi environment, since it is always
	 * null otherwise.  otherwise, the filename
	 * of the script will be retreived later via argc/argv */
	if (script_path_translated) {
		const char *auth;
		char *content_length = FCGI_GETENV(request, "CONTENT_LENGTH");
		char *content_type = FCGI_GETENV(request, "CONTENT_TYPE");
		char *env_path_info = FCGI_GETENV(request, "PATH_INFO");
		char *env_script_name = FCGI_GETENV(request, "SCRIPT_NAME");

		/* Hack for buggy IIS that sets incorrect PATH_INFO */
		char *env_server_software = FCGI_GETENV(request, "SERVER_SOFTWARE");
		if (env_server_software &&
			env_script_name &&
			env_path_info &&
			strncmp(env_server_software, "Microsoft-IIS", sizeof("Microsoft-IIS") - 1) == 0 &&
			strncmp(env_path_info, env_script_name, strlen(env_script_name)) == 0
		) {
			env_path_info = FCGI_PUTENV(request, "ORIG_PATH_INFO", env_path_info);
			env_path_info += strlen(env_script_name);
			if (*env_path_info == 0) {
				env_path_info = NULL;
			}
			env_path_info = FCGI_PUTENV(request, "PATH_INFO", env_path_info);
		}

#define APACHE_PROXY_FCGI_PREFIX "proxy:fcgi://"
#define APACHE_PROXY_BALANCER_PREFIX "proxy:balancer://"
		/* Fix proxy URLs in SCRIPT_FILENAME generated by Apache mod_proxy_fcgi and mod_proxy_balancer:
		 *     proxy:fcgi://localhost:9000/some-dir/info.php/test?foo=bar
		 *     proxy:balancer://localhost:9000/some-dir/info.php/test?foo=bar
		 * should be changed to:
		 *     /some-dir/info.php/test
		 * See: http://bugs.php.net/bug.php?id=54152
		 *      http://bugs.php.net/bug.php?id=62172
		 *      https://issues.apache.org/bugzilla/show_bug.cgi?id=50851
		 */
		if (env_script_filename &&
			strncasecmp(env_script_filename, APACHE_PROXY_FCGI_PREFIX, sizeof(APACHE_PROXY_FCGI_PREFIX) - 1) == 0) {
			/* advance to first character of hostname */
			char *p = env_script_filename + (sizeof(APACHE_PROXY_FCGI_PREFIX) - 1);
			while (*p != '\0' && *p != '/') {
				p++;	/* move past hostname and port */
			}
			if (*p != '\0') {
				/* Copy path portion in place to avoid memory leak.  Note
				 * that this also affects what script_path_translated points
				 * to. */
				memmove(env_script_filename, p, strlen(p) + 1);
				apache_was_here = 1;
			}
			/* ignore query string if sent by Apache (RewriteRule) */
			p = strchr(env_script_filename, '?');
			if (p) {
				*p =0;
			}
		}

		if (env_script_filename &&
			strncasecmp(env_script_filename, APACHE_PROXY_BALANCER_PREFIX, sizeof(APACHE_PROXY_BALANCER_PREFIX) - 1) == 0) {
			/* advance to first character of hostname */
			char *p = env_script_filename + (sizeof(APACHE_PROXY_BALANCER_PREFIX) - 1);
			while (*p != '\0' && *p != '/') {
				p++;	/* move past hostname and port */
			}
			if (*p != '\0') {
				/* Copy path portion in place to avoid memory leak.  Note
				 * that this also affects what script_path_translated points
				 * to. */
				memmove(env_script_filename, p, strlen(p) + 1);
				apache_was_here = 1;
			}
			/* ignore query string if sent by Apache (RewriteRule) */
			p = strchr(env_script_filename, '?');
			if (p) {
				*p =0;
			}
		}

		if (CGIG(fix_pathinfo)) {
			struct stat st;
			char *real_path = NULL;
			char *env_redirect_url = FCGI_GETENV(request, "REDIRECT_URL");
			char *env_document_root = FCGI_GETENV(request, "DOCUMENT_ROOT");
			char *orig_path_translated = env_path_translated;
			char *orig_path_info = env_path_info;
			char *orig_script_name = env_script_name;
			char *orig_script_filename = env_script_filename;
			int script_path_translated_len;

			if (!env_document_root && PG(doc_root)) {
				env_document_root = FCGI_PUTENV(request, "DOCUMENT_ROOT", PG(doc_root));
				/* fix docroot */
				TRANSLATE_SLASHES(env_document_root);
			}

			if (!apache_was_here && env_path_translated != NULL && env_redirect_url != NULL &&
			    env_path_translated != script_path_translated &&
			    strcmp(env_path_translated, script_path_translated) != 0) {
				/*
				 * pretty much apache specific.  If we have a redirect_url
				 * then our script_filename and script_name point to the
				 * php executable
				 * we don't want to do this for the new mod_proxy_fcgi approach,
				 * where redirect_url may also exist but the below will break
				 * with rewrites to PATH_INFO, hence the !apache_was_here check
				 */
				script_path_translated = env_path_translated;
				/* we correct SCRIPT_NAME now in case we don't have PATH_INFO */
				env_script_name = env_redirect_url;
			}

#ifdef __riscos__
			/* Convert path to unix format*/
			__riscosify_control |= __RISCOSIFY_DONT_CHECK_DIR;
			script_path_translated = __unixify(script_path_translated, 0, NULL, 1, 0);
#endif

			/*
			 * if the file doesn't exist, try to extract PATH_INFO out
			 * of it by stat'ing back through the '/'
			 * this fixes url's like /info.php/test
			 */
			if (script_path_translated &&
				(script_path_translated_len = strlen(script_path_translated)) > 0 &&
				(script_path_translated[script_path_translated_len-1] == '/' ||
#ifdef PHP_WIN32
				script_path_translated[script_path_translated_len-1] == '\\' ||
#endif
				(real_path = tsrm_realpath(script_path_translated, NULL)) == NULL)
			) {
				char *pt = estrndup(script_path_translated, script_path_translated_len);
				int len = script_path_translated_len;
				char *ptr;

				if (pt) {
					while ((ptr = strrchr(pt, '/')) || (ptr = strrchr(pt, '\\'))) {
						*ptr = 0;
						if (stat(pt, &st) == 0 && S_ISREG(st.st_mode)) {
							/*
							 * okay, we found the base script!
							 * work out how many chars we had to strip off;
							 * then we can modify PATH_INFO
							 * accordingly
							 *
							 * we now have the makings of
							 * PATH_INFO=/test
							 * SCRIPT_FILENAME=/docroot/info.php
							 *
							 * we now need to figure out what docroot is.
							 * if DOCUMENT_ROOT is set, this is easy, otherwise,
							 * we have to play the game of hide and seek to figure
							 * out what SCRIPT_NAME should be
							 */
							int ptlen = strlen(pt);
							int slen = len - ptlen;
							int pilen = env_path_info ? strlen(env_path_info) : 0;
							int tflag = 0;
							char *path_info;
							if (apache_was_here) {
								/* recall that PATH_INFO won't exist */
								path_info = script_path_translated + ptlen;
								tflag = (slen != 0 && (!orig_path_info || strcmp(orig_path_info, path_info) != 0));
							} else {
								path_info = env_path_info ? env_path_info + pilen - slen : NULL;
								tflag = (orig_path_info != path_info);
							}

							if (tflag) {
								if (orig_path_info) {
									char old;

									FCGI_PUTENV(request, "ORIG_PATH_INFO", orig_path_info);
									old = path_info[0];
									path_info[0] = 0;
									if (!orig_script_name ||
										strcmp(orig_script_name, env_path_info) != 0) {
										if (orig_script_name) {
											FCGI_PUTENV(request, "ORIG_SCRIPT_NAME", orig_script_name);
										}
										SG(request_info).request_uri = FCGI_PUTENV(request, "SCRIPT_NAME", env_path_info);
									} else {
										SG(request_info).request_uri = orig_script_name;
									}
									path_info[0] = old;
								} else if (apache_was_here && env_script_name) {
									/* Using mod_proxy_fcgi and ProxyPass, apache cannot set PATH_INFO
									 * As we can extract PATH_INFO from PATH_TRANSLATED
									 * it is probably also in SCRIPT_NAME and need to be removed
									 */
									int snlen = strlen(env_script_name);
									if (snlen>slen && !strcmp(env_script_name+snlen-slen, path_info)) {
										FCGI_PUTENV(request, "ORIG_SCRIPT_NAME", orig_script_name);
										env_script_name[snlen-slen] = 0;
										SG(request_info).request_uri = FCGI_PUTENV(request, "SCRIPT_NAME", env_script_name);
									}
								}
								env_path_info = FCGI_PUTENV(request, "PATH_INFO", path_info);
							}
							if (!orig_script_filename ||
								strcmp(orig_script_filename, pt) != 0) {
								if (orig_script_filename) {
									FCGI_PUTENV(request, "ORIG_SCRIPT_FILENAME", orig_script_filename);
								}
								script_path_translated = FCGI_PUTENV(request, "SCRIPT_FILENAME", pt);
							}
							TRANSLATE_SLASHES(pt);

							/* figure out docroot
							 * SCRIPT_FILENAME minus SCRIPT_NAME
							 */
							if (env_document_root) {
								int l = strlen(env_document_root);
								int path_translated_len = 0;
								char *path_translated = NULL;

								if (l && env_document_root[l - 1] == '/') {
									--l;
								}

								/* we have docroot, so we should have:
								 * DOCUMENT_ROOT=/docroot
								 * SCRIPT_FILENAME=/docroot/info.php
								 */

								/* PATH_TRANSLATED = DOCUMENT_ROOT + PATH_INFO */
								path_translated_len = l + (env_path_info ? strlen(env_path_info) : 0);
								path_translated = (char *) emalloc(path_translated_len + 1);
								memcpy(path_translated, env_document_root, l);
								if (env_path_info) {
									memcpy(path_translated + l, env_path_info, (path_translated_len - l));
								}
								path_translated[path_translated_len] = '\0';
								if (orig_path_translated) {
									FCGI_PUTENV(request, "ORIG_PATH_TRANSLATED", orig_path_translated);
								}
								env_path_translated = FCGI_PUTENV(request, "PATH_TRANSLATED", path_translated);
								efree(path_translated);
							} else if (	env_script_name &&
										strstr(pt, env_script_name)
							) {
								/* PATH_TRANSLATED = PATH_TRANSLATED - SCRIPT_NAME + PATH_INFO */
								int ptlen = strlen(pt) - strlen(env_script_name);
								int path_translated_len = ptlen + (env_path_info ? strlen(env_path_info) : 0);
								char *path_translated = NULL;

								path_translated = (char *) emalloc(path_translated_len + 1);
								memcpy(path_translated, pt, ptlen);
								if (env_path_info) {
									memcpy(path_translated + ptlen, env_path_info, path_translated_len - ptlen);
								}
								path_translated[path_translated_len] = '\0';
								if (orig_path_translated) {
									FCGI_PUTENV(request, "ORIG_PATH_TRANSLATED", orig_path_translated);
								}
								env_path_translated = FCGI_PUTENV(request, "PATH_TRANSLATED", path_translated);
								efree(path_translated);
							}
							break;
						}
					}
				} else {
					ptr = NULL;
				}
				if (!ptr) {
					/*
					 * if we stripped out all the '/' and still didn't find
					 * a valid path... we will fail, badly. of course we would
					 * have failed anyway... we output 'no input file' now.
					 */
					if (orig_script_filename) {
						FCGI_PUTENV(request, "ORIG_SCRIPT_FILENAME", orig_script_filename);
					}
					script_path_translated = FCGI_PUTENV(request, "SCRIPT_FILENAME", NULL);
					SG(sapi_headers).http_response_code = 404;
				}
				if (!SG(request_info).request_uri) {
					if (!orig_script_name ||
						strcmp(orig_script_name, env_script_name) != 0) {
						if (orig_script_name) {
							FCGI_PUTENV(request, "ORIG_SCRIPT_NAME", orig_script_name);
						}
						SG(request_info).request_uri = FCGI_PUTENV(request, "SCRIPT_NAME", env_script_name);
					} else {
						SG(request_info).request_uri = orig_script_name;
					}
				}
				if (pt) {
					efree(pt);
				}
			} else {
				/* make sure original values are remembered in ORIG_ copies if we've changed them */
				if (!orig_script_filename ||
					(script_path_translated != orig_script_filename &&
					strcmp(script_path_translated, orig_script_filename) != 0)) {
					if (orig_script_filename) {
						FCGI_PUTENV(request, "ORIG_SCRIPT_FILENAME", orig_script_filename);
					}
					script_path_translated = FCGI_PUTENV(request, "SCRIPT_FILENAME", script_path_translated);
				}
				if (!apache_was_here && env_redirect_url) {
					/* if we used PATH_TRANSLATED to work around Apache mod_fastcgi (but not mod_proxy_fcgi,
					 * hence !apache_was_here) weirdness, strip info accordingly */
					if (orig_path_info) {
						FCGI_PUTENV(request, "ORIG_PATH_INFO", orig_path_info);
						FCGI_PUTENV(request, "PATH_INFO", NULL);
					}
					if (orig_path_translated) {
						FCGI_PUTENV(request, "ORIG_PATH_TRANSLATED", orig_path_translated);
						FCGI_PUTENV(request, "PATH_TRANSLATED", NULL);
					}
				}
				if (env_script_name != orig_script_name) {
					if (orig_script_name) {
						FCGI_PUTENV(request, "ORIG_SCRIPT_NAME", orig_script_name);
					}
					SG(request_info).request_uri = FCGI_PUTENV(request, "SCRIPT_NAME", env_script_name);
				} else {
					SG(request_info).request_uri = env_script_name;
				}
				efree(real_path);
			}
		} else {
			/* pre 4.3 behaviour, shouldn't be used but provides BC */
			if (env_path_info) {
				SG(request_info).request_uri = env_path_info;
			} else {
				SG(request_info).request_uri = env_script_name;
			}
			if (!CGIG(discard_path) && env_path_translated) {
				script_path_translated = env_path_translated;
			}
		}

		if (is_valid_path(script_path_translated)) {
			SG(request_info).path_translated = estrdup(script_path_translated);
		}

		SG(request_info).request_method = FCGI_GETENV(request, "REQUEST_METHOD");
		/* FIXME - Work out proto_num here */
		SG(request_info).query_string = FCGI_GETENV(request, "QUERY_STRING");
		SG(request_info).content_type = (content_type ? content_type : "" );
		SG(request_info).content_length = (content_length ? atol(content_length) : 0);

		/* The CGI RFC allows servers to pass on unvalidated Authorization data */
		auth = FCGI_GETENV(request, "HTTP_AUTHORIZATION");
		php_handle_auth_data(auth);
	}

	/* INI stuff */
	ini = FCGI_GETENV(request, "PHP_VALUE");
	if (ini) {
		int mode = ZEND_INI_USER;
		char *tmp;
		spprintf(&tmp, 0, "%s\n", ini);
		zend_parse_ini_string(tmp, 1, ZEND_INI_SCANNER_NORMAL, (zend_ini_parser_cb_t)fastcgi_ini_parser, &mode);
		efree(tmp);
	}

	ini = FCGI_GETENV(request, "PHP_ADMIN_VALUE");
	if (ini) {
		int mode = ZEND_INI_SYSTEM;
		char *tmp;
		spprintf(&tmp, 0, "%s\n", ini);
		zend_parse_ini_string(tmp, 1, ZEND_INI_SCANNER_NORMAL, (zend_ini_parser_cb_t)fastcgi_ini_parser, &mode);
		efree(tmp);
	}
}
/* }}} */

static fcgi_request *fpm_init_request(int listen_fd) /* {{{ */ {
	fcgi_request *req = fcgi_init_request(listen_fd,
		fpm_request_accepting,
		fpm_request_reading_headers,
		fpm_request_finished);
	return req;
}
/* }}} */

static void fastcgi_ini_parser(zval *arg1, zval *arg2, zval *arg3, int callback_type, void *arg) /* {{{ */
{
	int *mode = (int *)arg;
	char *key;
	char *value = NULL;
	struct key_value_s kv;

	if (!mode || !arg1) return;

	if (callback_type != ZEND_INI_PARSER_ENTRY) {
		zlog(ZLOG_ERROR, "Passing INI directive through FastCGI: only classic entries are allowed");
		return;
	}

	key = Z_STRVAL_P(arg1);

	if (!key || strlen(key) < 1) {
		zlog(ZLOG_ERROR, "Passing INI directive through FastCGI: empty key");
		return;
	}

	if (arg2) {
		value = Z_STRVAL_P(arg2);
	}

	if (!value) {
		zlog(ZLOG_ERROR, "Passing INI directive through FastCGI: empty value for key '%s'", key);
		return;
	}

	kv.key = key;
	kv.value = value;
	kv.next = NULL;
	if (fpm_php_apply_defines_ex(&kv, *mode) == -1) {
		zlog(ZLOG_ERROR, "Passing INI directive through FastCGI: unable to set '%s'", key);
	}
}
/* }}} */

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("cgi.rfc2616_headers",     "0",  PHP_INI_ALL,    OnUpdateBool,   rfc2616_headers, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("cgi.nph",                 "0",  PHP_INI_ALL,    OnUpdateBool,   nph, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("cgi.force_redirect",      "1",  PHP_INI_SYSTEM, OnUpdateBool,   force_redirect, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("cgi.redirect_status_env", NULL, PHP_INI_SYSTEM, OnUpdateString, redirect_status_env, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("cgi.fix_pathinfo",        "1",  PHP_INI_SYSTEM, OnUpdateBool,   fix_pathinfo, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("cgi.discard_path",        "0",  PHP_INI_SYSTEM, OnUpdateBool,   discard_path, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("fastcgi.logging",         "1",  PHP_INI_SYSTEM, OnUpdateBool,   fcgi_logging, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("fastcgi.error_header",    NULL, PHP_INI_SYSTEM, OnUpdateString, error_header, php_cgi_globals_struct, php_cgi_globals)
	STD_PHP_INI_ENTRY("fpm.config",    NULL, PHP_INI_SYSTEM, OnUpdateString, fpm_config, php_cgi_globals_struct, php_cgi_globals)
PHP_INI_END()

/* {{{ php_cgi_globals_ctor
 */
static void php_cgi_globals_ctor(php_cgi_globals_struct *php_cgi_globals)
{
	php_cgi_globals->rfc2616_headers = 0;
	php_cgi_globals->nph = 0;
	php_cgi_globals->force_redirect = 1;
	php_cgi_globals->redirect_status_env = NULL;
	php_cgi_globals->fix_pathinfo = 1;
	php_cgi_globals->discard_path = 0;
	php_cgi_globals->fcgi_logging = 1;
	zend_hash_init(&php_cgi_globals->user_config_cache, 0, NULL, user_config_cache_entry_dtor, 1);
	php_cgi_globals->error_header = NULL;
	php_cgi_globals->fpm_config = NULL;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
static PHP_MINIT_FUNCTION(cgi)
{
#ifdef ZTS
	ts_allocate_id(&php_cgi_globals_id, sizeof(php_cgi_globals_struct), (ts_allocate_ctor) php_cgi_globals_ctor, NULL);
#else
	php_cgi_globals_ctor(&php_cgi_globals);
#endif
	REGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
static PHP_MSHUTDOWN_FUNCTION(cgi)
{
	zend_hash_destroy(&CGIG(user_config_cache));

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
static PHP_MINFO_FUNCTION(cgi)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "php-fpm", "active");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

PHP_FUNCTION(fastcgi_finish_request) /* {{{ */
{
	fcgi_request *request = (fcgi_request*) SG(server_context);

	if (!fcgi_is_closed(request)) {
		php_output_end_all();
		php_header();

		fcgi_end(request);
		fcgi_close(request, 0, 0);
		RETURN_TRUE;
	}

	RETURN_FALSE;

}
/* }}} */

static const zend_function_entry cgi_fcgi_sapi_functions[] = {
	PHP_FE(fastcgi_finish_request,              NULL)
	PHP_FE_END
};

static zend_module_entry cgi_module_entry = {
	STANDARD_MODULE_HEADER,
	"cgi-fcgi",
	cgi_fcgi_sapi_functions,
	PHP_MINIT(cgi),
	PHP_MSHUTDOWN(cgi),
	NULL,
	NULL,
	PHP_MINFO(cgi),
	NO_VERSION_YET,
	STANDARD_MODULE_PROPERTIES
};





/**
 * 用于传递request等信息
 */
typedef struct _g_accept_arg{
    struct event_base *base;
    fcgi_request *request;
    zend_file_handle file_handle;
    int max_requests;
	int *requests;
}g_accept_arg; 



void do_accept(evutil_socket_t listener, short event, void *arg)  
{  


	zlog(ZLOG_DEBUG, "do_accept run!!!!!!");
    struct event_base *base = ((g_accept_arg*) arg)->base;  
    struct sockaddr_storage ss;  
    socklen_t slen = sizeof(ss);  
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);  

    fcgi_request *request = ((g_accept_arg*) arg)->request;
    zend_file_handle file_handle = ((g_accept_arg*) arg)->file_handle;
    int max_requests = ((g_accept_arg*) arg)->max_requests;
	int requests = ((g_accept_arg*) arg)->requests;





    fcgi_set_fd(request,fd);
    
    //初始化回调函数（来源于）while (EXPECTED(fcgi_accept_request(request) >= 0))
    init_request_callback(request);
    
    //输出测试log
    char str[100];
    sprintf(str,"测试 do_accept() ok:%s\n",FCGI_GETENV(request, "SCRIPT_FILENAME"));
    test_log(str);

    sprintf(str,"测试 request:%d\n",request);
    test_log(str);




    // sprintf(str,"fpm_php_request_uri:%s\n",fpm_php_request_uri());
    


    // char test_str[] = "hello world!\n";

    // ssize_t result = send(fd, test_str, strlen(test_str), 0);

    // close(fd);

    // if (fd < 0)  
    // { // XXXX eagain??  
    //     perror("accept");  
    // }  
    // else if (fd > FD_SETSIZE)  
    // {  
    //     close(fd); // XXX replace all closes with EVUTIL_CLOSESOCKET */  
    // }  
    // else  
    // {  
    //     php_printf("accept!");
    //     struct fd_state *state;  
    //     evutil_make_socket_nonblocking(fd);  
    //     state = alloc_fd_state(base, fd);  
    //     php_printf("alloc_fd_state ====== done!");
    //     assert(state); /*XXX err*/  
    //     assert(state->write_event);  
    //     event_add(state->read_event, NULL);  
    // }  



	// SG(server_context) = (void *) request;
	// init_request_info();
	// fpm_request_info();

	// fcgi_set_fd(SG(server_context),fd);

 //    //输出测试log
 //    char str[100];
 //    sprintf(str,"fd:%d",fd);
 //    test_log(str);



 //    SG(sapi_headers).http_response_code = 404;
 //    PUTS("File not found.\n");

 //    // SG(sapi_headers).http_response_code = 200;
 //    // PUTS("File not found12345666.\n");

 //    // fcgi_write(SG(server_context),FCGI_GET_VALUES_RESULT,"hello",strlen("hello"));

 //    fcgi_finish_request(SG(server_context), 1);



	/* request startup only after we've done all we can to
	 *            get path_translated */
	// if (UNEXPECTED(php_request_startup() == FAILURE)) {
	// 	fcgi_finish_request(request, 1);
	// 	SG(server_context) = NULL;
	// 	php_module_shutdown();
	// 	return;
	// }

	// zlog(ZLOG_DEBUG, "Primary script unknown");
	

	// // php_request_shutdown((void *) 0);
 //    sapi_cgi_deactivate();



    //------读取二进制文件内容模拟输出
    // FILE *pfile;
    // char *pread;
    // size_t result;
    // // pfile=fopen("/Users/sioomy/work/php-src/tests/fpmtext/fpmoutput.txt","rb");
    // pfile=fopen("/Users/sioomy/work/php-src/tests/fpmtext/t2.txt","rb");

    // fseek(pfile,0,SEEK_END);//将文件内部的指针指向文件末尾
    // int lsize=ftell(pfile);//获取文件长度
    // rewind(pfile);//将文件内部的指针重新指向一个流的开头

    // pread=(char *) malloc((lsize+1)*sizeof(char));//申请内存空间，lsize*sizeof(char)是为了更严谨，16位上char占一个字符，其他机器上可能变化
    // memset(pread,0,lsize*sizeof(char)+1);//将内存空间都赋值为‘\0’
    
    // result=fread(pread,sizeof(char),lsize,pfile);//将pfile中内容读入pread指向内存中
    // send(fd, pread, lsize, 0);
    // fclose(pfile);
    // free(pread);
    // pread=NULL;
    // close(fd);



	// return;




    char *primary_script = NULL;
	request_body_fd = -1;
	SG(server_context) = (void *) request;

	init_request_info();

	fpm_request_info();

	/* request startup only after we've done all we can to
	 *            get path_translated */
	if (UNEXPECTED(php_request_startup() == FAILURE)) {
		fcgi_finish_request(request, 1);
		SG(server_context) = NULL;
		php_module_shutdown();
		// return FPM_EXIT_SOFTWARE;
		return;
	}

	/* check if request_method has been sent.
	 * if not, it's certainly not an HTTP over fcgi request */
	if (UNEXPECTED(!SG(request_info).request_method)) {
		goto fastcgi_request_done2;
	}
	if (UNEXPECTED(fpm_status_handle_request())) {
		goto fastcgi_request_done2;
	}
	/* If path_translated is NULL, terminate here with a 404 */
	if (UNEXPECTED(!SG(request_info).path_translated)) {
		zend_try {
			zlog(ZLOG_DEBUG, "Primary script unknown");
			SG(sapi_headers).http_response_code = 404;
			PUTS("File not found.\n");
		} zend_catch {
		} zend_end_try();
		goto fastcgi_request_done2;
	}
	if (UNEXPECTED(fpm_php_limit_extensions(SG(request_info).path_translated))) {
		SG(sapi_headers).http_response_code = 403;
		PUTS("Access denied.\n");
		goto fastcgi_request_done2;
	}

	/*
	 * have to duplicate SG(request_info).path_translated to be able to log errrors
	 * php_fopen_primary_script seems to delete SG(request_info).path_translated on failure
	 */
	primary_script = estrdup(SG(request_info).path_translated);

	// path_translated exists, we can continue ! 
	if (UNEXPECTED(php_fopen_primary_script(&file_handle) == FAILURE)) {
		zend_try {
			zlog(ZLOG_ERROR, "Unable to open primary script: %s (%s)", primary_script, strerror(errno));
			if (errno == EACCES) {
				SG(sapi_headers).http_response_code = 403;
				PUTS("Access denied.\n");
			} else {
				SG(sapi_headers).http_response_code = 404;
				PUTS("No input file specified.\n");
			}
		} zend_catch {
		} zend_end_try();
		/* we want to serve more requests if this is fastcgi
		 * so cleanup and continue, request shutdown is
		 * handled later */

		goto fastcgi_request_done2;
	}

	fpm_request_executing();

	php_execute_script_coro(&file_handle);

fastcgi_request_done2:
	if (EXPECTED(primary_script)) {
		efree(primary_script);
	}

	if (UNEXPECTED(request_body_fd != -1)) {
		close(request_body_fd);
	}
	request_body_fd = -2;

	if (UNEXPECTED(EG(exit_status) == 255)) {
		if (CGIG(error_header) && *CGIG(error_header)) {
			sapi_header_line ctr = {0};

			ctr.line = CGIG(error_header);
			ctr.line_len = strlen(CGIG(error_header));
			sapi_header_op(SAPI_HEADER_REPLACE, &ctr);
		}
	}

	fpm_request_end();
	fpm_log_write(NULL);

	efree(SG(request_info).path_translated);
	SG(request_info).path_translated = NULL;

	php_request_shutdown((void *) 0);

	requests++;
	if (UNEXPECTED(max_requests && (requests == max_requests))) {
		fcgi_request_set_keep(request, 0);
		fcgi_finish_request(request, 0);
		//break;
		return;
	}
	/* end of fastcgi loop */







}  



/* {{{ main
 */
int main(int argc, char *argv[])
{
	int exit_status = FPM_EXIT_OK;
	int cgi = 0, c, use_extended_info = 0;
	zend_file_handle file_handle;

	/* temporary locals */
	int orig_optind = php_optind;
	char *orig_optarg = php_optarg;
	int ini_entries_len = 0;
	/* end of temporary locals */

#ifdef ZTS
	void ***tsrm_ls;
#endif

	int max_requests = 500;
	int requests = 0;
	int fcgi_fd = 0;
	fcgi_request *request;
	char *fpm_config = NULL;
	char *fpm_prefix = NULL;
	char *fpm_pid = NULL;
	int test_conf = 0;
	int force_daemon = -1;
	int force_stderr = 0;
	int php_information = 0;
	int php_allow_to_run_as_root = 0;

#ifdef HAVE_SIGNAL_H
#if defined(SIGPIPE) && defined(SIG_IGN)
	signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE in standalone mode so
								that sockets created via fsockopen()
								don't kill PHP if the remote site
								closes it.  in apache|apxs mode apache
								does that for us!  thies@thieso.net
								20000419 */
#endif
#endif

#ifdef ZTS
	tsrm_startup(1, 1, 0, NULL);
	tsrm_ls = ts_resource(0);
#endif

	zend_signal_startup();

	sapi_startup(&cgi_sapi_module);
	cgi_sapi_module.php_ini_path_override = NULL;
	cgi_sapi_module.php_ini_ignore_cwd = 1;

#ifndef HAVE_ATTRIBUTE_WEAK
	fcgi_set_logger(fpm_fcgi_log);
#endif

	fcgi_init();

#ifdef PHP_WIN32
	_fmode = _O_BINARY; /* sets default for file streams to binary */
	setmode(_fileno(stdin),  O_BINARY);	/* make the stdio mode be binary */
	setmode(_fileno(stdout), O_BINARY);	/* make the stdio mode be binary */
	setmode(_fileno(stderr), O_BINARY);	/* make the stdio mode be binary */
#endif

	while ((c = php_getopt(argc, argv, OPTIONS, &php_optarg, &php_optind, 0, 2)) != -1) {
		switch (c) {
			case 'c':
				if (cgi_sapi_module.php_ini_path_override) {
					free(cgi_sapi_module.php_ini_path_override);
				}
				cgi_sapi_module.php_ini_path_override = strdup(php_optarg);
				break;

			case 'n':
				cgi_sapi_module.php_ini_ignore = 1;
				break;

			case 'd': {
				/* define ini entries on command line */
				int len = strlen(php_optarg);
				char *val;

				if ((val = strchr(php_optarg, '='))) {
					val++;
					if (!isalnum(*val) && *val != '"' && *val != '\'' && *val != '\0') {
						cgi_sapi_module.ini_entries = realloc(cgi_sapi_module.ini_entries, ini_entries_len + len + sizeof("\"\"\n\0"));
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len, php_optarg, (val - php_optarg));
						ini_entries_len += (val - php_optarg);
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len, "\"", 1);
						ini_entries_len++;
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len, val, len - (val - php_optarg));
						ini_entries_len += len - (val - php_optarg);
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len, "\"\n\0", sizeof("\"\n\0"));
						ini_entries_len += sizeof("\n\0\"") - 2;
					} else {
						cgi_sapi_module.ini_entries = realloc(cgi_sapi_module.ini_entries, ini_entries_len + len + sizeof("\n\0"));
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len, php_optarg, len);
						memcpy(cgi_sapi_module.ini_entries + ini_entries_len + len, "\n\0", sizeof("\n\0"));
						ini_entries_len += len + sizeof("\n\0") - 2;
					}
				} else {
					cgi_sapi_module.ini_entries = realloc(cgi_sapi_module.ini_entries, ini_entries_len + len + sizeof("=1\n\0"));
					memcpy(cgi_sapi_module.ini_entries + ini_entries_len, php_optarg, len);
					memcpy(cgi_sapi_module.ini_entries + ini_entries_len + len, "=1\n\0", sizeof("=1\n\0"));
					ini_entries_len += len + sizeof("=1\n\0") - 2;
				}
				break;
			}

			case 'y':
				fpm_config = php_optarg;
				break;

			case 'p':
				fpm_prefix = php_optarg;
				break;

			case 'g':
				fpm_pid = php_optarg;
				break;

			case 'e': /* enable extended info output */
				use_extended_info = 1;
				break;

			case 't':
				test_conf++;
				break;

			case 'm': /* list compiled in modules */
				cgi_sapi_module.startup(&cgi_sapi_module);
				php_output_activate();
				SG(headers_sent) = 1;
				php_printf("[PHP Modules]\n");
				print_modules();
				php_printf("\n[Zend Modules]\n");
				print_extensions();
				php_printf("\n");
				php_output_end_all();
				php_output_deactivate();
				fcgi_shutdown();
				exit_status = FPM_EXIT_OK;
				goto out;

			case 'i': /* php info & quit */
				php_information = 1;
				break;

			case 'R': /* allow to run as root */
				php_allow_to_run_as_root = 1;
				break;

			case 'D': /* daemonize */
				force_daemon = 1;
				break;

			case 'F': /* nodaemonize */
				force_daemon = 0;
				break;

			case 'O': /* force stderr even on non tty */
				force_stderr = 1;
				break;

			default:
			case 'h':
			case '?':
				cgi_sapi_module.startup(&cgi_sapi_module);
				php_output_activate();
				SG(headers_sent) = 1;
				php_cgi_usage(argv[0]);
				php_output_end_all();
				php_output_deactivate();
				fcgi_shutdown();
				exit_status = (c == 'h') ? FPM_EXIT_OK : FPM_EXIT_USAGE;
				goto out;

			case 'v': /* show php version & quit */
				cgi_sapi_module.startup(&cgi_sapi_module);
				if (php_request_startup() == FAILURE) {
					SG(server_context) = NULL;
					php_module_shutdown();
					return FPM_EXIT_SOFTWARE;
				}
				SG(headers_sent) = 1;
				SG(request_info).no_headers = 1;

#if ZEND_DEBUG
				php_printf("PHP %s (%s) (built: %s %s) (DEBUG)\nCopyright (c) 1997-2018 The PHP Group\n%s", PHP_VERSION, sapi_module.name, __DATE__,        __TIME__, get_zend_version());
#else
				php_printf("PHP %s (%s) (built: %s %s)\nCopyright (c) 1997-2018 The PHP Group\n%s", PHP_VERSION, sapi_module.name, __DATE__, __TIME__,      get_zend_version());
#endif
				php_request_shutdown((void *) 0);
				fcgi_shutdown();
				exit_status = FPM_EXIT_OK;
				goto out;
		}
	}

	if (php_information) {
		cgi_sapi_module.phpinfo_as_text = 1;
		cgi_sapi_module.startup(&cgi_sapi_module);
		if (php_request_startup() == FAILURE) {
			SG(server_context) = NULL;
			php_module_shutdown();
			return FPM_EXIT_SOFTWARE;
		}
		SG(headers_sent) = 1;
		SG(request_info).no_headers = 1;
		php_print_info(0xFFFFFFFF);
		php_request_shutdown((void *) 0);
		fcgi_shutdown();
		exit_status = FPM_EXIT_OK;
		goto out;
	}

	/* No other args are permitted here as there is no interactive mode */
	if (argc != php_optind) {
		cgi_sapi_module.startup(&cgi_sapi_module);
		php_output_activate();
		SG(headers_sent) = 1;
		php_cgi_usage(argv[0]);
		php_output_end_all();
		php_output_deactivate();
		fcgi_shutdown();
		exit_status = FPM_EXIT_USAGE;
		goto out;
	}

	php_optind = orig_optind;
	php_optarg = orig_optarg;

#ifdef ZTS
	SG(request_info).path_translated = NULL;
#endif

	cgi_sapi_module.additional_functions = NULL;
	cgi_sapi_module.executable_location = argv[0];

	/* startup after we get the above ini override se we get things right */
	if (cgi_sapi_module.startup(&cgi_sapi_module) == FAILURE) {
#ifdef ZTS
		tsrm_shutdown();
#endif
		return FPM_EXIT_SOFTWARE;
	}

	if (use_extended_info) {
		CG(compiler_options) |= ZEND_COMPILE_EXTENDED_INFO;
	}

	/* check force_cgi after startup, so we have proper output */
	if (cgi && CGIG(force_redirect)) {
		/* Apache will generate REDIRECT_STATUS,
		 * Netscape and redirect.so will generate HTTP_REDIRECT_STATUS.
		 * redirect.so and installation instructions available from
		 * http://www.koehntopp.de/php.
		 *   -- kk@netuse.de
		 */
		if (!getenv("REDIRECT_STATUS") &&
			!getenv ("HTTP_REDIRECT_STATUS") &&
			/* this is to allow a different env var to be configured
			 * in case some server does something different than above */
			(!CGIG(redirect_status_env) || !getenv(CGIG(redirect_status_env)))
		) {
			zend_try {
				SG(sapi_headers).http_response_code = 400;
				PUTS("<b>Security Alert!</b> The PHP CGI cannot be accessed directly.\n\n\
<p>This PHP CGI binary was compiled with force-cgi-redirect enabled.  This\n\
means that a page will only be served up if the REDIRECT_STATUS CGI variable is\n\
set, e.g. via an Apache Action directive.</p>\n\
<p>For more information as to <i>why</i> this behaviour exists, see the <a href=\"http://php.net/security.cgi-bin\">\
manual page for CGI security</a>.</p>\n\
<p>For more information about changing this behaviour or re-enabling this webserver,\n\
consult the installation file that came with this distribution, or visit \n\
<a href=\"http://php.net/install.windows\">the manual page</a>.</p>\n");
			} zend_catch {
			} zend_end_try();
#if defined(ZTS) && !defined(PHP_DEBUG)
			/* XXX we're crashing here in msvc6 debug builds at
			 * php_message_handler_for_zend:839 because
			 * SG(request_info).path_translated is an invalid pointer.
			 * It still happens even though I set it to null, so something
			 * weird is going on.
			 */
			tsrm_shutdown();
#endif
			return FPM_EXIT_SOFTWARE;
		}
	}

	if (0 > fpm_init(argc, argv, fpm_config ? fpm_config : CGIG(fpm_config), fpm_prefix, fpm_pid, test_conf, php_allow_to_run_as_root, force_daemon, force_stderr)) {

		if (fpm_globals.send_config_pipe[1]) {
			int writeval = 0;
			zlog(ZLOG_DEBUG, "Sending \"0\" (error) to parent via fd=%d", fpm_globals.send_config_pipe[1]);
			zend_quiet_write(fpm_globals.send_config_pipe[1], &writeval, sizeof(writeval));
			close(fpm_globals.send_config_pipe[1]);
		}
		return FPM_EXIT_CONFIG;
	}

	if (fpm_globals.send_config_pipe[1]) {
		int writeval = 1;
		zlog(ZLOG_DEBUG, "Sending \"1\" (OK) to parent via fd=%d", fpm_globals.send_config_pipe[1]);
		zend_quiet_write(fpm_globals.send_config_pipe[1], &writeval, sizeof(writeval));
		close(fpm_globals.send_config_pipe[1]);
	}
	fpm_is_running = 1;

	fcgi_fd = fpm_run(&max_requests);
	parent = 0;

	/* onced forked tell zlog to also send messages through sapi_cgi_log_fastcgi() */
	zlog_set_external_logger(sapi_cgi_log_fastcgi);

	/* make php call us to get _ENV vars */
	php_php_import_environment_variables = php_import_environment_variables;
	php_import_environment_variables = cgi_php_import_environment_variables;

	/* library is already initialized, now init our request */
	request = fpm_init_request(fcgi_fd);




//-----开始libevent
	evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;
    base = event_base_new();//初始化libevent
    if (!base)  
        return FPM_EXIT_SOFTWARE; /*XXXerr*/  

    printf("Event Run 1 ==!!!!!!");
	zend_first_try {
		g_accept_arg* arg = malloc(sizeof(g_accept_arg));
		arg->base = base;
		arg->request = request;
		arg->file_handle = file_handle;

		arg->max_requests = max_requests;
		arg->requests = requests;

	    listener_event = event_new(base, fcgi_fd, EV_READ|EV_PERSIST, do_accept, (void*)arg);
	    evutil_make_socket_nonblocking(listener);

	    /*XXX check it */  
	    event_add(listener_event, NULL);
	    event_base_dispatch(base);
    } zend_catch {
		exit_status = FPM_EXIT_SOFTWARE;
	} zend_end_try();

    printf("Event Run!!!!!!");

//-----结束libevent






	zend_first_try {
		while (EXPECTED(fcgi_accept_request(request) >= 0)) {

			char *primary_script = NULL;
			request_body_fd = -1;
			SG(server_context) = (void *) request;
			init_request_info();

			fpm_request_info();

			/* request startup only after we've done all we can to
			 *            get path_translated */
			if (UNEXPECTED(php_request_startup() == FAILURE)) {
				fcgi_finish_request(request, 1);
				SG(server_context) = NULL;
				php_module_shutdown();
				return FPM_EXIT_SOFTWARE;
			}

			/* check if request_method has been sent.
			 * if not, it's certainly not an HTTP over fcgi request */
			if (UNEXPECTED(!SG(request_info).request_method)) {
				goto fastcgi_request_done;
			}

			if (UNEXPECTED(fpm_status_handle_request())) {
				goto fastcgi_request_done;
			}

			/* If path_translated is NULL, terminate here with a 404 */
			if (UNEXPECTED(!SG(request_info).path_translated)) {
				zend_try {
					zlog(ZLOG_DEBUG, "Primary script unknown");
					SG(sapi_headers).http_response_code = 404;
					PUTS("File not found.\n");
				} zend_catch {
				} zend_end_try();
				goto fastcgi_request_done;
			}

			if (UNEXPECTED(fpm_php_limit_extensions(SG(request_info).path_translated))) {
				SG(sapi_headers).http_response_code = 403;
				PUTS("Access denied.\n");
				goto fastcgi_request_done;
			}

			/*
			 * have to duplicate SG(request_info).path_translated to be able to log errrors
			 * php_fopen_primary_script seems to delete SG(request_info).path_translated on failure
			 */
			primary_script = estrdup(SG(request_info).path_translated);

			// path_translated exists, we can continue ! 
			if (UNEXPECTED(php_fopen_primary_script(&file_handle) == FAILURE)) {
				zend_try {
					zlog(ZLOG_ERROR, "Unable to open primary script: %s (%s)", primary_script, strerror(errno));
					if (errno == EACCES) {
						SG(sapi_headers).http_response_code = 403;
						PUTS("Access denied.\n");
					} else {
						SG(sapi_headers).http_response_code = 404;
						PUTS("No input file specified.\n");
					}
				} zend_catch {
				} zend_end_try();
				/* we want to serve more requests if this is fastcgi
				 * so cleanup and continue, request shutdown is
				 * handled later */

				goto fastcgi_request_done;
			}

			fpm_request_executing();

			php_execute_script(&file_handle);

fastcgi_request_done:
			if (EXPECTED(primary_script)) {
				efree(primary_script);
			}

			if (UNEXPECTED(request_body_fd != -1)) {
				close(request_body_fd);
			}
			request_body_fd = -2;

			if (UNEXPECTED(EG(exit_status) == 255)) {
				if (CGIG(error_header) && *CGIG(error_header)) {
					sapi_header_line ctr = {0};

					ctr.line = CGIG(error_header);
					ctr.line_len = strlen(CGIG(error_header));
					sapi_header_op(SAPI_HEADER_REPLACE, &ctr);
				}
			}

			fpm_request_end();
			fpm_log_write(NULL);

			efree(SG(request_info).path_translated);
			SG(request_info).path_translated = NULL;

			php_request_shutdown((void *) 0);

			requests++;
			if (UNEXPECTED(max_requests && (requests == max_requests))) {
				fcgi_request_set_keep(request, 0);
				fcgi_finish_request(request, 0);
				break;
			}
			/* end of fastcgi loop */
		}
		fcgi_destroy_request(request);
		fcgi_shutdown();

		if (cgi_sapi_module.php_ini_path_override) {
			free(cgi_sapi_module.php_ini_path_override);
		}
		if (cgi_sapi_module.ini_entries) {
			free(cgi_sapi_module.ini_entries);
		}
	} zend_catch {
		exit_status = FPM_EXIT_SOFTWARE;
	} zend_end_try();

out:

	SG(server_context) = NULL;
	php_module_shutdown();

	if (parent) {
		sapi_shutdown();
	}

#ifdef ZTS
	tsrm_shutdown();
#endif

#if defined(PHP_WIN32) && ZEND_DEBUG && 0
	_CrtDumpMemoryLeaks();
#endif

	return exit_status;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
