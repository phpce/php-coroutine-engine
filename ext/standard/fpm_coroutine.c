#include "php.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_TIMES
#include <sys/times.h>
#endif

#include <event2/event.h>
/* For sockaddr_in */  
#include <netinet/in.h>
#include "main/SAPI.h"
#include "fpm_coroutine.h"

/* use to stor coroutine context */

static php_coroutine_context* current_coroutine_context = NULL;
static int coroutine_context_count = 0;

/**
 * 测试输出LOG
 */
void test_log(char *text){

    FILE *pfile;
    size_t result;
    pfile=fopen("/tmp/fpmlog.txt","a+");

    int lsize=strlen(text);//获取文件长度

    result=fwrite(text,sizeof(char),lsize,pfile);//将pfile中内容读入pread指向内存中
    fclose(pfile);
}


/**
 * 注册libevent
 */
int regist_event(int fcgi_fd,void (*do_accept())){
    struct event_base *base;
    struct event *listener_event;
    base = event_base_new();//初始化libevent
    if (!base)  
        return false; //libevent 初始化失败  

	g_accept_arg* arg = malloc(sizeof(g_accept_arg));
	arg->base = base;

    //set coroutineinfo
    init_coroutine_info();
    SG(coroutine_info).base = base;
    SG(coroutine_info).fcgi_fd = fcgi_fd;

    SG(coroutine_info).test_log("========= libevent base loop start ===== \n");

    listener_event = event_new(base, fcgi_fd, EV_READ|EV_PERSIST, do_accept, (void*)arg);
    // evutil_make_socket_nonblocking(fcgi_fd);//todo 这里可能会有问题,造成不稳定

    /* 添加事件 */  
    event_add(listener_event, NULL);
    event_base_dispatch(base);

    return true;
}
/**
 * 获取当前上下文
 */
php_coroutine_context *get_coroutine_context(){

	//输出测试log
    char str[100];
    sprintf(str,"get_coroutine_context() run\n");
    test_log(str);

	php_coroutine_context* context = current_coroutine_context;
	return context;
}

//将Context中的内容载入全局变量
void load_coroutine_context(php_coroutine_context *context){
    EG(vm_stack) = context->vm_stack;
    EG(vm_stack_top) = context->vm_stack_top;
    EG(vm_stack_end) = context->vm_stack_end;
}

//将全局变量中的数据载入Context
void write_coroutine_context(php_coroutine_context *context){
    context->vm_stack = EG(vm_stack);
    context->vm_stack_top = EG(vm_stack_top);
    context->vm_stack_end = EG(vm_stack_end);

    //todo 需要研究一下scoreboard，是否需要将里面的部分变量写入context
}

void resume_coroutine_context(php_coroutine_context* context){
    test_log("yield resume run \n");
    int r = setjmp(*context->buf_ptr);//yield之后的代码段，设置起始标记
    if(r == CORO_DEFAULT){//继续
        test_log("jump set core_default --yield\n");
        zend_vm_stack_free_args(context->execute_data);
        zend_vm_stack_free_call_frame(context->execute_data);

        // EG(vm_stack) = context->current_vm_stack;
        // EG(vm_stack_top) = context->current_vm_stack_top;
        // EG(vm_stack_end) = context->current_vm_stack_end;

        load_coroutine_context(context);
        EG(current_execute_data) = context->execute_data;
        EG(current_execute_data)->opline++;

        zend_execute_ex(EG(current_execute_data));
        context->coro_state = CORO_END;
        //return vm stack
        // EG(vm_stack) = g_coro_stack.vm_stack;
        // EG(vm_stack_top) = g_coro_stack.vm_stack_top;
        // EG(vm_stack_end) = g_coro_stack.vm_stack_end;

        free_coroutine_context(context);
        //结束代码

        //from zend_execute_scripts_coro中的那部分---释放op_array
        zend_exception_restore();
        zend_try_exception_handler();
        if (EG(exception)) {
            zend_exception_error(EG(exception), E_ERROR);
        }
        destroy_op_array(context->op_array);
        efree_size(context->op_array, sizeof(zend_op_array));

        //from php_execute_script_coro ,处理异常
        if (EG(exception)) {
            zend_try {
                zend_exception_error(EG(exception), E_ERROR);
            } zend_end_try();
        }
        test_log("resume ... \n");
#if HAVE_BROKEN_GETCWD
        if ((int)*context->old_cwd_fd != -1) {
            fchdir(*context->old_cwd_fd);
            close(*context->old_cwd_fd);
        }
#else
        //todo 这里是释放old_cwd，但是 没调通
        // if (*context->old_cwd[0] != '\0') {
        //     php_ignore_value(VCWD_CHDIR(*context->old_cwd));
        // }
        // free_alloca(*context->old_cwd, use_heap);
        test_log(context->old_cwd);
        SG(coroutine_info).free_old_cwd(context->old_cwd,context->use_heap);
#endif
        test_log("resume done \n");

        SG(coroutine_info).close_request();

    }else{
        test_log("resume code yield \n");
    }
}

void yield_coroutine_context(){
    test_log("yield_coroutine_context() \n");
    php_coroutine_context* context = SG(coroutine_info).context;
    context->coro_state = CORO_YIELD;
    longjmp(*context->buf_ptr,CORO_YIELD);
}

/**
 * 释放上下文
 */
void free_coroutine_context(php_coroutine_context* context){
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
    SG(coroutine_info).context = current_coroutine_context;
}



/**
 * 初始化上下文
 * todo 上下文池化,不池化，会内存泄漏，100多个开始崩溃
 */
void init_coroutine_context(fcgi_request *request){
    //初始化context 上下文
    php_coroutine_context *context = NULL;
    context = emalloc(sizeof(php_coroutine_context));
    context->coro_state = CORO_DEFAULT;
    context->func_cache = emalloc(sizeof(zend_fcall_info_cache));
    context->request = request;//存储request
    context->buf_ptr = emalloc(sizeof(jmp_buf));
    context->req_ptr = emalloc(sizeof(jmp_buf));

    if(!current_coroutine_context){
        current_coroutine_context = context;
    }
    //context加入链表
    //link linktable
    if(coroutine_context_count == 0){
        context->next = context;
        context->prev = context;
    }else{
        context->prev = current_coroutine_context->prev;
        context->prev->next = context;
        context->next = current_coroutine_context;
        context->next->prev = context; 
    }
    coroutine_context_count++;



    SG(coroutine_info).context = current_coroutine_context;
}



void init_coroutine_info(){
    // sapi_coroutine_info* corotine_info = emalloc(sizeof(sapi_coroutine_info));
    // SG(coroutine_info) = *corotine_info;
    SG(coroutine_info).base = NULL;
    SG(coroutine_info).fcgi_fd = NULL;  
    SG(coroutine_info).test_log = test_log;
    SG(coroutine_info).yield_coroutine_context = yield_coroutine_context;
    SG(coroutine_info).resume_coroutine_context = resume_coroutine_context;
}