# 项目说明[中文]

本项目是php-fpm的分支，已经实现php原生协程。

这个项目是从php官方的github中fork出来的版本，基于php7.1.17版本。

协程是一种可以支持高并发服务器的设计模式。

现在主流的服务器端语言和框架均支持协程调用，包括golang、openresty、java、swoole等。

协程可以降低服务器阻塞，对于需要使用远程调用的服务如使用rpc、mysql、redis等场景，使用协程可以显著提升服务器性能。

协程本质是一种服务器端异步非阻塞IO模型的实现方式之一。

传统php-fpm的设计模式主要是通过多进程来进行并发处理请求。对于服务器资源的使用不充分。

本项目就是通过对php-fpm源码进行改造，实现了协程模式的php-fpm，最终可以像nginx一样，几个进程即可处理大量的并发请求，充分的利用CPU资源，成倍提升php服务器性能。

提示：本项目未经过完整测试，请小心，不建议使用在生产环境。

## 协程原理

![avatar](/tutorial/coroutine.jpg)

协程本质是异步非阻塞IO的实现方式之一。php-fpm要想实现协程，需要分成两部分，一部分是调度器，另外一部分就是在PHP扩展中实现协程逻辑。

PHP-FPM就是充当着调度器的角色。

当有请求进来之后，调度器会触发一次执行。在这次请求的执行过程中，如果遇到php代码中有远程调用，则会在请求发送之后，php自动将控制权交给调度器。调度器这个时候可以决定处理其他新进来请求，还是处理之前远程调用返回来的可以继续执行的请求。

每个请求会从协程池中取出一个协程上下文进行绑定，请求结束后，则会将协程上下文放回去，以便其他请求复用。

协程上下文主要存放的是php运行环境中用到的全局和私有变量。这块的实现主要是利用php的多线程安全存储(注意不是多线程)，既TSRM相关库。

TSRM库会将所有php全局变量获取宏以及php内存池进行隔离。

所以在开发协程扩展的时候，要注意全局变量获取宏如EG()、SG()、emalloc、efree等的使用。

支持协程的php-fpm在编译过程中需要增加 --enable-maintainer-zts 参数告诉编译器，要求支持zts多线程安全存储。

在多线程安全存储模式下，php使用的内部内存的存储方式大致可以理解为一个矩阵。矩阵的纵轴是每个线程（协程）。矩阵的横轴存储的是所有运行环境中需要用到的内存。也就是说每个php协程都有一套自己的内存。

协程版本的php因为利用了多线程安全存储(TSRM)。所以此版本的php-fpm用到的扩展以及应用层的php代码是不支持多线程的。

## 压测报告

只做了简单压测，系统并没有做优化。以后会优化

机器是2016款的MAC PRO，配置是2核，8G内存

压测工具使用的是wrk,命令如下
```
./wrk -c 800 -t 800 -d 120s http://localhost/test.php
```
开启800个线程 800个连接压测，相当于800个并发请求。

理论上每个请求2秒返回，极限的压测效果是400qps

请求流程是:
```
wrk >> nginx >> php-fpm >> nodejs
```
wrk客户端访问NGINX FASTCGI反向代理，nginx访问php-fpm, php-fpm通过协程函数访问NODEJS程序

nodejs程序在8080端口，模拟2秒钟返回结果的接口。

test.js程序位置: tutorial/test.js

nodejs 启动命令:
```
 node tutorial/test.js
```
test.php位置:   tutorial/test.php

test.php 通过coro_http_get(实现协程的http客户端)调用test.js
```
 var_dump(coro_http_get("http://localhost:8080/"));
```
这个测试中，php-fpm 启动了8个进程，PHP-FPM中每进程设置的最大内存使用大小为128M

NGINX启动了8个进程

压测结果如下：

![avatar](/tutorial/pic/rt.png)

系统占用情况抓图1：

![avatar](/tutorial/pic/top1.png)

系统占用情况抓图2：

![avatar](/tutorial/pic/top2.png)

在压测过程中，通过curl 访问是可以正常访问的,如下图：

![avatar](/tutorial/pic/curl.png)

总结：

总体压测结果，虽然进程数只有8个，并且远端接口2秒才返回的情况。Qps 为185。因为是800并发，每个请求2秒，极限Qps应该是400

这个结果比我想象的要低很多。估计需要优化。但是从这个压测结果可以观察出协程和传统php-fpm的区别还是很大的。传统php-fpm一般需要多开启很多个进程，一般服务器会开启128个进程，即使忽略php-fpm多进程对效率的影响。那么传统php-fpm能够提供的qps理论最大值，不会超过64个（因为每个请求是2秒返回，fpm进程会阻塞2秒）。

压测结果中超时比较多。这个和系统task占用的cpu有关

分析一下TOP命令的资源占用情况，例如第一个资源图。kernel_task占用的资源达到43%，相当高。不知道LINUX下面表现会不会更好

kernel_task较高，应该是整个压测过程中  wrk nginx php-fpm nodejs  这4组程序的网络传输调度都是由它来搞定，比较繁忙

php-fpm的cpu占用相对较低，说明调度都交给内核来执行了。这个符合协程的特性。

图中，NODEJS占用18%左右，WRK占用10%

nginx 占用cpu略低于php-fpm，换个角度来讲，php-fpm协程的性能已经接近于nginx

最后，因为本机压测本机，服务也比较多。如果分开，效果会更好一些。


## 注意事项


1.请牢记这一点，php-fpm协程中，固定每个php-fpm进程的协程池大小为128个.超出的并发会等待。要想机器拥有更好的并发，请合理配置进程数量。服务能承受的总并发数为所有进程协程池大小的总和。

2.启动时，请注意调整好PHP-FPM进程数量,php-fpm.d/www.conf中
```
 pm = static  
 pm.max_children = 4
```
3.注意,调整每个进程使用的内存大小.在php.ini中。请根据进程数量和系统可用内存合理计算
```
 memory_limit = 128M
```
4.测试时，请注意有些浏览器同一个链接是阻塞的，但是改变参数就不会阻塞了，例如chrome（这个比较坑，最开始我还以为是nginx的问题定位了很久）。建议使用curl命令测试，这样可以更好的观察协程效果

## 扩展说明

ext/coro_http 目录是为了测试协程开发的PHP扩展，提供了coro_http_get方法。  
里面的核心文件是coro_http.c,已包含详尽的注释。  
这个扩展可以当成一个协程实现的demo,里面内容非常简单，开发者可以参照demo开发更多支持协程的扩展。  
因为php-fpm中调度器的部分已经实现，开发者只需要实现扩展即可开发协程应用。  

目前只支持macOS和linux

## 项目调试方法一，编译安装:


1.系统中先要安装libevent库，具体安装方法请自行查找资料,另外可能还需要安装php需要的一些扩展

2.生成automake配置文件
```
 sh buildconf --force
```
3.安装php
```
 ./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --enable-maintainer-zts && make && make install
```
注意：linux 中需要增加--with-openssl ，不然安装会出错，mac不需要。  

4.安装完成后修改php-fpm配置文件，设置PHP-FPM进程数。线上环境根据CPU数量和需要的协程数量调整，测试的时候可以设置成1（php-fpm.d/www.conf）
主要是这两个参数
``` 
 pm = static   
 pm.max_children = 1
```
5.启动php-fpm
```
 sudo /usr/local/php7/sbin/php-fpm
```
6.配置nginx，请自行查阅相关资料,请将nginx的访问目录配置成源码中的tutorial目录，主要是里面的test.php,用于测试

7.以上不走完成了就可以开始测试了，根据配置好的NGINX，直接访问浏览器（注：mac里的chrome是不支持同一个url并发访问的。建议使用curl命令测试）  
如果强烈要求使用chrome,请认真读下面的文字  
例如：http://localhost/test.php?a=xx  
这里一定要注意，在chrome中可以开两个窗口访问，但是后面的参数要不一样。  


## 项目调试方法二，Docker安装[推荐]:

1.进入tutorial/，执行:
```
 docker build -t php-fpm-coroutine ./
```
2.运行docker:
```
 docker run -p 8083:80 php-fpm-coroutine
```
3.浏览器输入网址
```
 http://localhost:8083/test3.php
```
可以看到输出结果为：
```
"\n\n \n \n <\/head>\n \n <\/body>\n<\/html>\n"
```
test3.php代码：
```
<?php
echo json_encode(coro_http_get("https://eclick.baidu.com/fp.htm?br=2&fp=AFC7630BB
EFD3392102B5F3F1EEF0C4CF&fp2=94C2E84A9CD47B1BC80A6366448EEB23&ci=033B5A846E552C44
95EF85060AF903D17%3AFG%3D1&bi=3997FDA588664E4479DBBB8813432125%3AFG%3D1&im=0&wf==
0&ct=2194&bp=&m=&t=0&ft=&_=1533274737087"));
?>
```
----------
**********

# Project description[For English]

This project is a branch of php-fpm and has implemented PHP Native Coroutine.

This project is a version from PHP's official GitHub fork, based on php7.1.17 version.

Coopera is a design pattern that supports high concurrent servers.

Now the mainstream server-side languages and frameworks support the call of the association Coroutine, including golang, openresty, Java, swoole and so on.

Coroutine process can reduce server congestion, and the use of Coroutine process can significantly improve server performance for services such as RPC, mysql, redis, and other services that need to use remote calls.

The nature of association Coroutine is one of the implementations of asynchronous asynchronous non blocking IO model.

The traditional php-fpm design mode is mainly through concurrent processing requests through multiple processes. The use of server resources is inadequate.

This project is through the transformation of the php-fpm source code, the implementation of the Coroutine model of the php-fpm, and finally, like nginx, several processes can deal with a large number of concurrent requests, full use of CPU resources, multiplied the performance of the PHP server.

Note: this item has not been completely tested. Please be careful not to use it in the production environment.



## Coroutine theory

![avatar](/tutorial/coroutine.jpg)

The essence of the Coroutine is one of the asynchronous non blocking IO implementations. If php-fpm wants to implement the syndication, it needs to be divided into two parts, one is the scheduler, the other is to implement the syndication logic in the PHP extension.

PHP-FPM is the role of the scheduler.

When a request comes in, the scheduler triggers one execution. During the execution of this request, if a remote call is encountered in the PHP code, PHP automatically gives control to the scheduler after the request is sent. At this point, the scheduler can decide whether to handle other new incoming requests or to process requests that can be executed after the previous remote call.

Each request is bound from a Coroutine process context from the Coroutine context pool, and after the request is finished, the coroutine process context is put back so that other requests can be reused.

The context of the association mainly stores the global and private variables used in the PHP running environment. This implementation is mainly based on PHP's multithread secure storage (note not multithreading) and TSRM related library.

The TSRM library will isolate all PHP global variables from macros and PHP memory pools.

So when developing the extension of the coroutine, we should pay attention to the use of macro variables such as EG (), SG (), emalloc, EFREE and so on.

The --enable-maintainer-zts parameter tells the compiler that it is required to support ZTS multithreaded secure storage.

In multithread secure storage mode, the way of storing internal memory used by PHP can be generally understood as a matrix. The longitudinal axis of a matrix is each thread (coroutine). The horizontal axis of the matrix stores all the memory needed in all the running environments. That is to say, each PHP association has its own memory.

The PHP version of the coda version uses multithread secure storage (TSRM). So the expansion of this version of php-fpm and the PHP code of application layer do not support multithreading.

## Pressure test report

The system has not been optimized. It will be optimized in the future

The machine is 2016 MAC PRO, and the configuration is 2 cores, 8G memory.

The test tool is wrk, and the command is as follows

```
./wrk -c 800 -t 800 -d 120s http://localhost/test.php
```

Open 800 threads, 800 connection presses, equivalent to 800 concurrent requests.

In theory, each request is returned in 2 seconds, and the limit test result is 400qps.

The request process is:

```
wrk >> nginx >> php-fpm >> nodejs
```
The wrk client accesses the NGINX FASTCGI reverse proxy, nginx visits php-fpm, and php-fpm visits the NODEJS program through the coroutine function.

The nodejs program is on the 8080 port, simulating the interface that returns the result in 2 seconds.

Test.js program location: tutorial/test.js

The nodejs boot command:

```
Node tutorial/test.js
```
Test.php location: tutorial/test.php

Test.php calls test.js through coro_http_get (HTTP client to implement the Association).

```
Var_dump (coro_http_get ("http://localhost:8080/"));
```
In this test, php-fpm started 8 processes, and the maximum memory usage per process in PHP-FPM is 128M.

NGINX started 8 processes

The results of the test are as follows:

![avatar](/tutorial/pic/rt.png)

System occupancy capture 1:

![avatar](/tutorial/pic/top1.png)

System occupancy capture 2:

![avatar](/tutorial/pic/top2.png)

In the process of pressure measurement, access through curl can be accessed normally.

![avatar](/tutorial/pic/curl.png)

Summary:

The overall test results, although the number of processes is only 8, and the remote interface 2 seconds to return. Qps is 185. Because it is 800 concurrent, each request for 2 seconds, the limit Qps should be 400.

The result is much lower than I thought. It is estimated that it needs to be optimized. But from this measurement result, we can see that the difference between the coroutine and traditional php-fpm is still very different. Traditional php-fpm usually needs to open many more processes, and servers usually open 128 processes, even ignoring the impact of php-fpm multi-processes on efficiency. Then the maximum QPS theory that traditional php-fpm can provide is no more than 64 (because each request returns in 2 seconds, and the FPM process blocks for 2 seconds).

The result of pressure measurement is more time-out. This is related to the CPU occupied by the system task

Analyze the resource occupation of the TOP command, for example, the first resource map. Kernel_task takes up 43% of the resources, which is quite high. I don't know if the performance below LINUX will be better

The kernel_task is higher, it should be wrk nginx php-fpm nodejs that the network transmission scheduling of these four groups of programs is done by it in the whole process of pressure measurement. It is busy.

The CPU usage of php-fpm is relatively low, which means that the scheduling is handed over to the kernel for execution. This conforms to the characteristics of the co process.

In the figure, NODEJS takes about 18% and WRK takes up 10%

Nginx occupies CPU slightly below php-fpm. In another way, the performance of php-fpm is nearly nginx.

Finally, because the machine presses the machine, there are more services. If separated, the effect would be better.




## Matters of attention

1. Keep this in mind. In php-fpm syndication, the coroutine pool size of each php-fpm process is fixed to 128. If you want the machine to have better concurrency, please configure the process number reasonably. The total number of concurrent services that can be sustained is the sum of all process pool size.

2. when starting, pay attention to adjusting the number of PHP-FPM processes, php-fpm.d/www.conf

```
PM = static  
Pm.max_children = 4
```

3. note that adjust the memory size used by each process. In php.ini. Please calculate reasonably according to the number of processes and the available memory of the system.

```
Memory_limit = 128M
```

4. when testing, please note that some browsers are blocking the same link, but changing the parameters will not block, such as chrome (this comparison pit, at first I think it's a long time to think it's nginx's problem). It is recommended that the curl command test be used to better observe the synergy effect.

## Extension explanation

The ext/coro_http directory provides a coro_http_get method for testing the PHP extension of the co development.

The core document is coro_http.c, which contains detailed annotations.

This extension can be thought of as a demo for the implementation of a collaboration, which is very simple, and developers can develop more extensions to support the collaboration with the help of demo.

Because the part of scheduler in php-fpm has been implemented, developers only need to extend it to develop cooperating applications.

Only macOS and Linux are currently supported

## Debug method 1, compile and install:

1. Install libevent libraries in the system first. Find out how to install libevent libraries by yourself, and you may also need to install some extensions required by PHP

2. generate automake configuration file

```
sh buildconf --force
```
3. install PHP

```
./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --enable-maintainer-zts & make & make install
```

Note: --with-openssl is needed in Linux, otherwise installation will be wrong and Mac will not be needed.

4. after installation, modify the php-fpm configuration file and set the number of PHP-FPM processes. The online environment can be adjusted to 1 (php-fpm.d/www.conf) according to the number of CPU and the number of required cooperations.

This is mainly the two parameters

```
PM = static  
Pm.max_children = 1
```

5. start php-fpm

```
Sudo /usr/local/php7/sbin/php-fpm
```

6. Configuration of nginx, please consult the relevant information, please configure the nginx access directory into the source code tutorial directory, mainly the test. php, for testing

7. You can start the test without completing it. According to the configured NGINX, access the browser directly (Note: the chrome in MAC does not support the same URL concurrent access. It is recommended to use the curl command test)

If you strongly request the use of chrome, please read the following text carefully.

For example, http://localhost/test.php?a=xx

It is important to note that in chrome, two windows can be accessed, but the following parameters are different.


## Debug method two, Docker installation[recommended]:



1. enter tutorial/, execute:

```
Docker build -t php-fpm-coroutine. /
```

2. run docker:

```
Docker run -p 8083:80 php-fpm-coroutine
```

3. browsers enter the URL

```
Http://localhost:8083/test3.php
```

It can be seen that the output results are as follows:

```
"\n\n \n \n <\/head>\n \n <\/body>\n<\/html>\n"
```

Test3.php Code:

```
<?php
echo json_encode(coro_http_get("https://eclick.baidu.com/fp.htm?br=2&fp=AFC7630BB
EFD3392102B5F3F1EEF0C4CF&fp2=94C2E84A9CD47B1BC80A6366448EEB23&ci=033B5A846E552C44
95EF85060AF903D17%3AFG%3D1&bi=3997FDA588664E4479DBBB8813432125%3AFG%3D1&im=0&wf==
0&ct=2194&bp=&m=&t=0&ft=&_=1533274737087"));
?>
```

