#ifndef FPM_COROUTINE_H
#define FPM_COROUTINE_H 1

#include <event2/event.h>
/* For sockaddr_in */  
#include <netinet/in.h>

#include "fastcgi.h"

#define CORO_DEFAULT 0
#define CORO_YIELD 1
#define CORO_END 2
#define CORO_RESUME 3
#define CORO_START 4

typedef struct _php_coroutine_context{
    jmp_buf *buf_ptr;
    jmp_buf *req_ptr;
    zend_execute_data *execute_data;
    zend_op_array *op_array;
    zend_execute_data *prev_execute_data;//execute for execute before yield
    struct _php_coroutine_context *next;
    struct _php_coroutine_context *prev;
    int coro_state;
    zend_vm_stack vm_stack;
    zval* vm_stack_top;
    zval* vm_stack_end;
    zend_fcall_info_cache* func_cache;
    zval *ret;
    //请求过程中用到的全局变量
#if HAVE_BROKEN_GETCWD
    volatile int *old_cwd_fd;
#else
    char *old_cwd;
    zend_bool use_heap;
#endif
    fcgi_request *request;//用于保存请求信息



}php_coroutine_context;


// static struct _g_coro_stack{
//     zend_vm_stack vm_stack;
//     zval* vm_stack_top;
//     zval* vm_stack_end;
// }g_coro_stack;

/**
 * 用于传递request等信息
 */
typedef struct _g_accept_arg{
    struct event_base *base;
} g_accept_arg;


void test_log(char *text);

int regist_event(int fcgi_fd,void (*do_accept()));

php_coroutine_context *get_coroutine_context();
void resume_coroutine_context(php_coroutine_context* context);
void yield_coroutine_context();
void free_coroutine_context(php_coroutine_context* context);

void init_coroutine_context(fcgi_request *request);
void load_coroutine_context(php_coroutine_context *context);
void write_coroutine_context(php_coroutine_context *context);


/**
 * 用于存储coroutine libevent
 */
typedef struct _g_sapi_coroutine_info{
    struct event_base *base;
    int fcgi_fd;
    php_coroutine_context* context;
    void(*test_log)(char *text);//output test log
    void(*resume_coroutine_context)(php_coroutine_context* context);
    void(*yield_coroutine_context)();
    void(*close_request)();
    void(*free_old_cwd)(char *old_cwd,zend_bool use_heap);
} sapi_coroutine_info;

void init_coroutine_info();

#endif
