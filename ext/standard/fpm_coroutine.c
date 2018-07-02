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
#include "SAPI.h"
#include "fpm_coroutine.h"
#include "fastcgi.h"

/* use to stor coroutine context */

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

    init_coroutine_info();
    evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;
    base = event_base_new();//初始化libevent
    if (!base)  
        return false; /*XXXerr*/  
    sin.sin_family = AF_INET;  
    sin.sin_addr.s_addr = 0;//本机  
    sin.sin_port = htons(9002); 
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0)  
    {  
        php_printf("bind");  
        return false;  
　　 }

    if (listen(listener, 16)<0)  
　　 {  
　　     php_printf("listen");  
　　     return false;  
　　 }

    //set coroutineinfo
    SG(coroutine_info).base = base;
    SG(coroutine_info).fcgi_fd = listener;

    char a[200];
    sprintf(a,"========= libevent base loop start ---fcgi_fd:%d ===== \n",listener);
    SG(coroutine_info).test_log(a);

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
    evutil_make_socket_nonblocking(listener);
    /* 添加事件 */  
    event_add(listener_event, NULL);
    // event_base_dispatch(base);
    event_base_loop(base,0);




    // init_coroutine_info();

    // struct event_base *base;
    // struct event *listener_event;
    // base = event_base_new();//初始化libevent
    // if (!base)  
    //     return false; //libevent 初始化失败  

    // //set coroutineinfo
    // SG(coroutine_info).base = base;
    // SG(coroutine_info).fcgi_fd = fcgi_fd;

    // char a[200];
    // sprintf(a,"========= libevent base loop start ---fcgi_fd:%d ===== \n",fcgi_fd);
    // SG(coroutine_info).test_log(a);

    // listener_event = event_new(base, fcgi_fd, EV_READ|EV_PERSIST, do_accept, base);
    // evutil_make_socket_nonblocking(fcgi_fd);

    // /* 添加事件 */  
    // event_add(listener_event, NULL);
    // // event_base_dispatch(base);
    // event_base_loop(base,0);
    // SG(coroutine_info).test_log("========= libevent base loop done ===== \n");

    return true;
}


//将Context中的内容载入全局变量
void load_coroutine_context(sapi_coroutine_context *context){

    SG(coroutine_info).context = context;//全局当前context指针

    EG(vm_stack) = context->vm_stack;
    EG(vm_stack_top) = context->vm_stack_top;
    EG(vm_stack_end) = context->vm_stack_end;

    // SG(server_context) = (void *)context->request;//load request
    EG(current_execute_data) = context->prev_execute_data;


    SG(coroutine_info).init_request((void *)context->request);//=====tmp
    SG(sapi_headers) = context->sapi_headers;



    // SG(request_info) = context->request_info;

    // EG(symbol_table) = *context->execute_data->symbol_table;

    // EG(symbol_table) = context->symbol_table;

    // zend_hash_copy(&EG(symbol_table),&context->symbol_table,NULL);

}

//将全局变量中的数据载入Context
void write_coroutine_context(sapi_coroutine_context *context){
    context->vm_stack = EG(vm_stack);
    context->vm_stack_top = EG(vm_stack_top);
    context->vm_stack_end = EG(vm_stack_end);



    // SG(server_context) = (void *)context->request;//load request
    // SG(coroutine_info).init_request((void *)context->request);


    //todo 需要研究一下scoreboard，是否需要将里面的部分变量写入context

    context->sapi_headers = SG(sapi_headers);
    // context->request_info = SG(request_info);

    // *context->execute_data->symbol_table = EG(symbol_table);

    // context->symbol_table = EG(symbol_table);

    // zend_hash_copy(&context->symbol_table,&EG(symbol_table),NULL);


}

void resume_coroutine_context(sapi_coroutine_context* context){

    char t[200];
    sprintf(t,"====111=resume_coroutine_context:%d,*buf_ptr:%d,context_count:%d\n",context,*context->buf_ptr,SG(coroutine_info).context_count);
    test_log(t);

    

    int r = setjmp(*context->buf_ptr);//yield之后的代码段，设置起始标记
    if(r == CORO_DEFAULT){//继续

        zend_vm_stack_free_args(context->prev_execute_data);
        zend_vm_stack_free_call_frame(context->prev_execute_data);

        init_executor();

        load_coroutine_context(context);
        EG(current_execute_data)->opline++;

        zend_execute_ex(EG(current_execute_data));
        context->coro_state = CORO_END;

        zend_exception_restore();
        zend_try_exception_handler();
        if (EG(exception)) {
            zend_exception_error(EG(exception), E_ERROR);
        }

#if HAVE_BROKEN_GETCWD
        if ((int)*context->old_cwd_fd != -1) {
            fchdir(*context->old_cwd_fd);
            close(*context->old_cwd_fd);
        }
#else
        SG(coroutine_info).free_old_cwd(context->old_cwd,context->use_heap);
#endif

        //from php_execute_script_coro ,处理异常
        if (EG(exception)) {
            zend_try {
                zend_exception_error(EG(exception), E_ERROR);
            } zend_end_try();
        }


        test_log("resume done \n");

        SG(coroutine_info).close_request();
        test_log("close done \n");
        free_coroutine_context(SG(coroutine_info).context);

    }else{
        test_log("resume code yield \n");
    }
}

void yield_coroutine_context(){

    test_log("=========== yield =========\n");

    

    sapi_coroutine_context* context = SG(coroutine_info).context;
    context->coro_state = CORO_YIELD;

    context->prev_execute_data = EG(current_execute_data)->prev_execute_data;
    write_coroutine_context(SG(coroutine_info).context);


    char t[200];
    sprintf(t,"====111=yield_coroutine_context:%d,*buf_ptr:%d,context_count:%d\n",SG(coroutine_info).context,*context->buf_ptr,SG(coroutine_info).context_count);
    test_log(t);

    longjmp(*context->buf_ptr,CORO_YIELD);
}

/**
 * 释放上下文  todo 内存泄漏，需要进一步处理
 */
void free_coroutine_context(sapi_coroutine_context* context){
    return;

    if(SG(coroutine_info).context_count>0){

        test_log("free === 1 ===\n");

        SG(coroutine_info).context_count--;
        test_log("free === 1.1 ===\n");
        //unlink
        context->prev->next = context->next;
        context->next->prev = context->prev;
        test_log("free === 1.2 ===\n");
        //todo free all data
        char a[200];

        sprintf(a,"free === 1.3 === context->buf_ptr:%d,context->req_ptr:%d,*context->buf_ptr:%d,*context->req_ptr:%d   ===\n",context->buf_ptr,context->req_ptr,*context->buf_ptr,*context->req_ptr);
        test_log(a);

        // efree(context->buf_ptr);
        // efree(context->req_ptr);
        test_log("free === 1.4 ===\n");
        // context->buf_ptr = NULL;
        // context->req_ptr = NULL;

        test_log("free === 2 ===\n");
        zend_vm_stack_free_call_frame(context->execute_data); //释放execute_data:销毁所有的PHP变量
        context->execute_data = NULL;
        test_log("free === 2.1 ===\n");
        // efree(context->func_cache);
        // context->func_cache = NULL;


        test_log("free === 3 ===\n");

        destroy_op_array(context->op_array);
        efree_size(context->op_array, sizeof(zend_op_array));

        test_log("free === 4 ===\n");

        test_log("free === 5 ===\n");
        // efree(context);
        // context = NULL;
        test_log("free === 6 ===\n");
        if(SG(coroutine_info).context_count == 0){
            SG(coroutine_info).context = NULL;
        }
    }
}



/**
 * 初始化上下文
 * todo 上下文池化,不池化，会内存泄漏，100多个开始崩溃
 * 在这个函数执行之后，会适用 load_coroutine_context write_coroutine_context将context 中保存的信息导入导出
 */
void init_coroutine_context(fcgi_request *request){
    //初始化context 上下文
    sapi_coroutine_context *context = emalloc(sizeof(sapi_coroutine_context));
    context->coro_state = CORO_DEFAULT;
    context->func_cache = emalloc(sizeof(zend_fcall_info_cache));
    context->request = request;//存储request
    context->buf_ptr = emalloc(sizeof(jmp_buf));
    context->req_ptr = emalloc(sizeof(jmp_buf));

    if(!SG(coroutine_info).context){
        SG(coroutine_info).context = context;
    }
    //context加入链表
    //link linktable
    if(SG(coroutine_info).context_count == 0){
        context->next = context;
        context->prev = context;
    }else{
        context->prev = SG(coroutine_info).context->prev;
        context->prev->next = context;
        context->next = SG(coroutine_info).context;
        context->next->prev = context; 
    }
    SG(coroutine_info).context_count++;

    /*
    初始化参数说明====
    //这三个是需要处理的
    fpm_scoreboard  计分板  todo 需要分析一下
    SG(sapi_headers)   头信息
    SG(request_info)   请求信息

    其他不需要处理的全局宏
    PG(XXXX)    header_is_being_sent   struct _php_core_globals  PHP全局配置，与上下文无关
    CWDG(XXX)   ？？？？？
    OG(XXX)     output_globals,输出相关，目前看只有输出错误的时候用到。代码执行时应该自动处理
    EG(XXX)     executor_globals Zend/zend_execute_API.c 执行相关的全局变量，在存储在execute_data里
    CG(XXX)     compiler_globals 编译相关，暂时未用到
    */

    SG(coroutine_info).context = context;




    context->sapi_headers = SG(sapi_headers);
    context->request_info = SG(request_info);



    // //独立指针的内存(这里应该是用不到。mimetype http_status_line应该是未被声明)
    // context->sapi_headers.mimetype = emalloc(sizeof(SG(sapi_headers).mimetype));
    // context->sapi_headers.http_status_line = emalloc(sizeof(SG(sapi_headers).http_status_line));
    // memcpy(context->sapi_headers.mimetype,SG(sapi_headers).mimetype,sizeof(SG(sapi_headers).mimetype));
    // memcpy(context->sapi_headers.http_status_line,SG(sapi_headers).http_status_line,sizeof(SG(sapi_headers).http_status_line));

    //将数据写回,相当于擦掉mimetype http_status_line ,这里需要注意的是,
    //整个进程的第一个sapi_headers的指针可能会造成内存泄漏,可忽略


}



void init_coroutine_info(){
    // sapi_coroutine_info* corotine_info = emalloc(sizeof(sapi_coroutine_info));
    // SG(coroutine_info) = *corotine_info;
    SG(coroutine_info).base = NULL;
    SG(coroutine_info).fcgi_fd = NULL;
    SG(coroutine_info).context_count = 0;
    SG(coroutine_info).context = NULL;
    SG(coroutine_info).test_log = test_log;
    SG(coroutine_info).yield_coroutine_context = yield_coroutine_context;
    SG(coroutine_info).resume_coroutine_context = resume_coroutine_context;
}