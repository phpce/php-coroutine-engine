#ifndef FPM_COROUTINE_H
#define FPM_COROUTINE_H 1

#include <event2/event.h>
/* For sockaddr_in */  
#include <netinet/in.h>

#include "fastcgi.h"
#include "SAPI.h"

void test_log(char *text);

int regist_event(int fcgi_fd,void (*do_accept()));

void checkout_coroutine_context(sapi_coroutine_context* context);
void resume_coroutine_context();
void yield_coroutine_context();
void free_coroutine_context(sapi_coroutine_context* context);

void init_coroutine_context(int idx);
sapi_coroutine_context* use_coroutine_context();
void init_coroutine_set_request(sapi_coroutine_context* context,fcgi_request *request);


void init_coroutine_info();

void init_coroutine_static();

#endif
