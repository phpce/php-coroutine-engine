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
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_coro_http.h"

#include "SAPI.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>

#include "event2/http.h"
#include "event2/http_struct.h"
#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/dns.h"
#include "event2/thread.h"
#include <event.h>
#include <event2/bufferevent.h>

#include "ext/standard/fpm_coroutine.h"

#ifndef WIN32  
#include <sys/socket.h>  
#include <sys/types.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  
#endif  



/* If you declare any globals in php_coro_http.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(coro_http)
*/

/* True global resources - no need for thread safety here */
static int le_coro_http;

typedef struct _coro_http_param
{
    struct evhttp_connection* connection;
    struct evhttp_uri* uri;
    sapi_coroutine_context* context;
} coro_http_param;



/************************************************************************
 *   扩展需要用到的member
 *   SG(coroutine_info).get_event_base();                                   基础event_loop_base
 *   SG(coroutine_info).context                                 上下文
 *   SG(coroutine_info).tmpData                                 是一个void* 类型的空指针，用于临时存放数据
 *   SG(coroutine_info).test_log(char* str);                    输出LOG
 *   SG(coroutine_info).yield_coroutine_context();              释放当前协程
 *   SG(coroutine_info).checkout_coroutine_context(context);    切换到协程
 *   SG(coroutine_info).resume_coroutine_context();             继续执行上下文
 */

void free_request(coro_http_param* http_param){
    // evhttp_connection_free(http_param->connection);
    // evhttp_connection_free_on_completion(http_param->connection);
    evhttp_uri_free(http_param->uri);
    free(http_param);
}

int ReadHeaderDoneCallback(struct evhttp_request* remote_rsp, void* arg)
{
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);

    char txt[1024];
    sprintf(txt, "< HTTP/1.1 %d %s\n", evhttp_request_get_response_code(remote_rsp), evhttp_request_get_response_code_line(remote_rsp));
    struct evkeyvalq* headers = evhttp_request_get_input_headers(remote_rsp);
    // struct evkeyval* header;
    // TAILQ_FOREACH(header, headers, next)
    // {
    //     fprintf(stderr, "< %s: %s\n", header->key, header->value);
    // }
    // fprintf(stderr, "< \n");
    // SG(coroutine_info).test_log(txt);
}

void ReadChunkCallback(struct evhttp_request* remote_rsp, void* arg)
{
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);

    char* tmpStr = (char*)context->tmpData;
    char buf[1024];
    struct evbuffer* evbuf = evhttp_request_get_input_buffer(remote_rsp);
    int n = 0;
    while ((n = evbuffer_remove(evbuf, buf, 1024)) > 0)
    {
        strncat(tmpStr,buf,n);
    }
}

void RemoteReadCallback(struct evhttp_request* remote_rsp, void* arg)
{
    /**
     * 当远程调用成功，立刻切换协程，arg是evhttp新建请求时传递过来的协程context
     * 这里注意，一定要第一时间切换到当前请求的context
     * 这样才能保证emalloc、SG宏、EG宏，等全局宏可以正确拿到当前协程的内存
     */
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);

    // bufferevent_free(remote_rsp);

    /**
     * tmpStatus为0代表正常有结果，否则代表请求出错。返回值已被设置成错误提示
     */
    char* tmpStr = (char*)context->tmpData;

    /**
     * return_value 是扩展函数执行时保存下来的返回值指针
     */
    zval* return_value = context->return_value;

    /**
     * 从全局变量中获取返回结果的buffer,并创建zend_string类型的结果
     */
    zend_string* result = zend_string_init(tmpStr,strlen(tmpStr)*sizeof(char),0);

    /**
     * 设置函数返回值
     */
    RETVAL_STR(result);
    efree(tmpStr);
    context->tmpData = NULL;
    free_request(arg);


    // SG(coroutine_info).test_log("request done\n");

    /**
     * 继续执行当前协程的PHP脚本
     */
    SG(coroutine_info).resume_coroutine_context();
}

void RemoteRequestErrorCallback(enum evhttp_request_error error, void* arg)
{
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);
    char* tmpStr = (char*)context->tmpData;
    switch(error){
        case EVREQ_HTTP_TIMEOUT:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_TIMEOUT!",strlen("remote request error:EVREQ_HTTP_TIMEOUT!")*sizeof(char));
            break;
        case EVREQ_HTTP_EOF:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_EOF!",strlen("remote request error:EVREQ_HTTP_EOF!")*sizeof(char));
            break;
        case EVREQ_HTTP_INVALID_HEADER:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_INVALID_HEADER!",strlen("remote request error:EVREQ_HTTP_INVALID_HEADER!")*sizeof(char));
            break;
        case EVREQ_HTTP_BUFFER_ERROR:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_BUFFER_ERROR!",strlen("remote request error:EVREQ_HTTP_BUFFER_ERROR!")*sizeof(char));
            break;
        case EVREQ_HTTP_REQUEST_CANCEL:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_REQUEST_CANCEL!",strlen("remote request error:EVREQ_HTTP_REQUEST_CANCEL!")*sizeof(char));
            break;
        case EVREQ_HTTP_DATA_TOO_LONG:
            strncat(tmpStr,"remote request error:EVREQ_HTTP_DATA_TOO_LONG!",strlen("remote request error:EVREQ_HTTP_DATA_TOO_LONG!")*sizeof(char));
            break;
        default:
            strncat(tmpStr,"remote request error:unkown error!",strlen("remote request error:unkown error!")*sizeof(char));
            break;
    }
}

void RemoteConnectionCloseCallback(struct evhttp_connection* connection, void* arg)
{

}

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("coro_http.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_coro_http_globals, coro_http_globals)
    STD_PHP_INI_ENTRY("coro_http.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_coro_http_globals, coro_http_globals)
PHP_INI_END()
*/
/* }}} */

/* Remove the following function when you have successfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_coro_http_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(coro_http_get)
{
    /**
     * 保存coro_http_get返回值指针到协程中，当evhttp返回时会用到
     */
    SG(coroutine_info).context->return_value = return_value; 

    zval* param;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &param) == FAILURE){
        zend_string* result = zend_string_init("param error!",strlen("param error!")*sizeof(char),0);
        RETURN_STR(result);
    }
    char* url = ZSTR_VAL(zval_get_string(param));
    coro_http_param* coro_param = malloc(sizeof(coro_http_param));
    struct evhttp_uri* uri = evhttp_uri_parse(url);
    coro_param->context = SG(coroutine_info).context;
    coro_param->uri = uri;

    if (!uri)
    {
        zend_string* result = zend_string_init("uri create error!",strlen("uri create error!")*sizeof(char),0);
        RETURN_STR(result);
    }

    /**
     * 这里与正常使用evhttp不同的是，要使用协程里的event_base，并且后面不需要调用loop方法（使用系统的loop）
     */
    struct event_base *base = SG(coroutine_info).get_event_base();

    if (!base)
    {
        zend_string* result = zend_string_init("event_base error!",strlen("event_base error!")*sizeof(char),0);
        RETURN_STR(result);
    }

    struct evdns_base* dnsbase = SG(coroutine_info).get_evdns_base(); 
    if (!dnsbase)
    {
        zend_string* result = zend_string_init("dnsbase error!",strlen("dnsbase error!")*sizeof(char),0);
        RETURN_STR(result);

    }
    assert(dnsbase);
    
    char* tmpStr = (char*)emalloc(sizeof(char)*4096);
    strcpy(tmpStr,"");
    /**
     * 注意，tmpStr是不能放在全局里的，因为多个协程操作会导致不安全
     * 因此，tmpStr放在上下文里比较好
     */
    SG(coroutine_info).context->tmpData = (void*)tmpStr;

    struct evhttp_request* request = evhttp_request_new(RemoteReadCallback, coro_param);
    evhttp_request_set_header_cb(request, ReadHeaderDoneCallback);
    evhttp_request_set_chunked_cb(request, ReadChunkCallback);
    evhttp_request_set_error_cb(request, RemoteRequestErrorCallback);
    
    const char* host = evhttp_uri_get_host(uri);
    if (!host)
    {
        zend_string* result = zend_string_init("host error!",strlen("host error!")*sizeof(char),0);
        RETURN_STR(result);
    }

    int port = evhttp_uri_get_port(uri);
    if (port < 0) port = 80;

    const char* request_url = url;
    const char* path = evhttp_uri_get_path(uri);
    if (path == NULL || strlen(path) == 0)
    {
        request_url = "/";
    }

    struct evhttp_connection* connection =  evhttp_connection_base_new(base, dnsbase, host, port);
    if (!connection)
    {
        zend_string* result = zend_string_init("create connection error!",strlen("create connection error!")*sizeof(char),0);
        RETURN_STR(result);
    }
    coro_param->connection = connection;
    evhttp_connection_set_closecb(connection, RemoteConnectionCloseCallback, base);
    evhttp_add_header(evhttp_request_get_output_headers(request), "Host", host);

    evhttp_make_request(connection, request, EVHTTP_REQ_GET, request_url);

    /**
     * 请求发出后，立刻释放当前协程，将控制权交给协程控制器(php-fpm)
     */
    SG(coroutine_info).yield_coroutine_context();
}


void readcb(struct bufferevent* bev, void * arg)  
{  
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);
    char* tmpStr = (char*)emalloc(sizeof(char)*4096);
    strcpy(tmpStr,"");

    char buf[1024];  
    int n;  
    struct evbuffer* input = bufferevent_get_input(bev);  
    while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0)   
    {  
        // fwrite(buf, 1, n, stdout);  
        strncat(tmpStr,buf,n);
    }  



    /**
    * tmpStatus为0代表正常有结果，否则代表请求出错。返回值已被设置成错误提示
    */
    // char* tmpStr = (char*)context->tmpData;

    /**
    * return_value 是扩展函数执行时保存下来的返回值指针
    */
    zval* return_value = context->return_value;

    /**
    * 从全局变量中获取返回结果的buffer,并创建zend_string类型的结果
    */
    zend_string* result = zend_string_init(tmpStr,strlen(tmpStr)*sizeof(char),0);

    /**
    * 设置函数返回值
    */
    RETVAL_STR(result);
    free_request(arg);
    efree(tmpStr);

    /**
    * 继续执行当前协程的PHP脚本
    */
    SG(coroutine_info).resume_coroutine_context();

}  
void eventcb(struct bufferevent* bev, short events, void * arg)  
{  
    sapi_coroutine_context* context = ((coro_http_param*)arg)->context;
    SG(coroutine_info).checkout_coroutine_context(context);


    if (events & BEV_EVENT_CONNECTED)   
    {  
        // printf("Connect okay.\n");  
    }   
    else if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF))  
    {   
        if (events & BEV_EVENT_ERROR)   
        {  
            int err = bufferevent_socket_get_dns_error(bev);  
            if (err)  
            printf("DNS error: %s\n", evutil_gai_strerror(err));  
        }  

        bufferevent_free(bev);   

        
    }  
}  


PHP_FUNCTION(ce_http_get)
{
    /**
     * 保存ce_http_get返回值指针到协程中，当evhttp返回时会用到
     */
    SG(coroutine_info).context->return_value = return_value; 

    zval* param;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &param) == FAILURE){
        zend_string* result = zend_string_init("param error!",strlen("param error!")*sizeof(char),0);
        RETURN_STR(result);
    }
    char* url = ZSTR_VAL(zval_get_string(param));

    struct evhttp_uri* uri = evhttp_uri_parse(url);
    coro_http_param* coro_param = malloc(sizeof(coro_http_param));
    coro_param->context = SG(coroutine_info).context;
    coro_param->uri = uri;

    if (!uri)
    {
        zend_string* result = zend_string_init("uri create error!",strlen("uri create error!")*sizeof(char),0);
        RETURN_STR(result);
    }

    const char* host = evhttp_uri_get_host(uri);
    if (!host)
    {
        zend_string* result = zend_string_init("host error!",strlen("host error!")*sizeof(char),0);
        RETURN_STR(result);
    }

    int port = evhttp_uri_get_port(uri);
    if (port < 0) port = 80;

    // const char* request_url = url;
    const char* path = evhttp_uri_get_path(uri);
    // if (path == NULL || strlen(path) == 0)
    // {
    //     request_url = "/";
    // }


    /**
     * 这里与正常使用不同的是，要使用协程里的event_base，并且后面不需要调用loop方法（使用系统的loop）
     */
    struct event_base *base = SG(coroutine_info).get_event_base();
    struct evdns_base *dnsbase = SG(coroutine_info).get_evdns_base(); 
    struct bufferevent* bev;


    bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);  
    bufferevent_setcb(bev, readcb, NULL, eventcb, coro_param);  
    bufferevent_enable(bev, EV_READ|EV_WRITE);  
    evbuffer_add_printf(bufferevent_get_output(bev), "GET %s\r\n", path);  
    bufferevent_socket_connect_hostname(bev, dnsbase, AF_UNSPEC, host, port);  

    /**
     * 请求发出后，立刻释放当前协程，将控制权交给协程控制器(php-fpm)
     */
    SG(coroutine_info).yield_coroutine_context();
}


PHP_FUNCTION(ce_context_index)
{
  RETURN_LONG(SG(coroutine_info).context->thread_id);
}

PHP_FUNCTION(ce_context_fd)
{
  RETURN_LONG(SG(coroutine_info).context->fd);
}

/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and
   unfold functions in source code. See the corresponding marks just before
   function definition, where the functions purpose is also documented. Please
   follow this convention for the convenience of others editing your code.
*/


/* {{{ php_coro_http_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_coro_http_init_globals(zend_coro_http_globals *coro_http_globals)
{
	coro_http_globals->global_value = 0;
	coro_http_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(coro_http)
{
	/* If you have INI entries, uncomment these lines
	REGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(coro_http)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(coro_http)
{
#if defined(COMPILE_DL_CORO_HTTP) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(coro_http)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(coro_http)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "coro_http support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* {{{ coro_http_functions[]
 *
 * Every user visible function must have an entry in coro_http_functions[].
 */
const zend_function_entry coro_http_functions[] = {
	PHP_FE(coro_http_get,	NULL)	
    PHP_FE(ce_http_get, NULL) 
    PHP_FE(ce_context_index, NULL) 
    PHP_FE(ce_context_fd, NULL) 
	PHP_FE_END	/* Must be the last line in coro_http_functions[] */
};
/* }}} */

/* {{{ coro_http_module_entry
 */
zend_module_entry coro_http_module_entry = {
	STANDARD_MODULE_HEADER,
	"coro_http",
	coro_http_functions,
	PHP_MINIT(coro_http),
	PHP_MSHUTDOWN(coro_http),
	PHP_RINIT(coro_http),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(coro_http),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(coro_http),
	PHP_CORO_HTTP_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_CORO_HTTP
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(coro_http)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
