项目说明
===================
这个项目是从PHP官方的github中fork出来的版本，基于php7.1.17版本。本项目主要用来研究PHP-FPM支持协程。

协程是一种可以支持高并发服务器的设计模式。

现在主流的服务器端语言和框架均支持协程调用，包括golang、openresty、java、swoole等。

协程可以降低服务器阻塞，对于需要使用远程调用的服务如使用rpc、mysql、redis等场景，使用协程可以显著提升其服务器性能。

是一种服务器端异步非阻塞IO模型的一种实现方式。

PHP-FPM的设计模式主要是通过多进程来进行并发处理请求。对于服务器资源的使用不充分。

本项目就是通过对PHP-FPM进行优化，通过实现协程模式的PHP-FPM，最终可以像NGINX一样，几个进程即可处理大量的并发请求，充分的利用CPU资源，成倍提升PHP服务器性能。

协程原理
===================
![avatar](/tutorial/coroutine.jpg)

协程本身是一种异步非阻塞的实现方式之一，实现协程需要分成两部分，一个是调度器，另外一个就是协程插件。

这里PHP-FPM就是充当着调度器的功能。当有请求进来之后，由调度器来触发执行。在这次请求的执行过程中，如果遇到远程调用，则需要在请求发送之后PHP主动将控制权交给调度器。调度器这个时候再决定处理其他的请求或者是处理之前执行远程时返回来可以继续执行的请求。
每个请求之间有独立的存储，互相不干扰。

压测报告
===================
只做了简单压测，系统并没有做调优。以后会优化

机器是2016款的MAC PRO，配置是2核，8G内存

压测工具使用的是wrk,命令如下

./wrk -c 800 -t 800 -d 120s http://localhost/test.php

开启800个线程 800个链接压测

请求流程是，经过NGINX FASTCGI反向代理，访问PHP-FPM.同时机器中启动一个NODEJS的程序，在8080端口，模拟2秒钟返回结果。

test.js程序位置 tutorial/test.js

test.php位置   tutorial/test.php

test.php 通过coro_http_get(实现协程的http客户端)调用test.js,既http://127.0.0.1:8080/

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

总体压测结果，虽然进程数只有8个，并且远端接口2秒才返回的情况。Qps 为185。

这个结果比我想象的要低很多。估计需要优化。但是初步可以提现出协程和传统PHP-FPM的区别。

压测结果中超时比较多。这个和系统TASK占用的CPU有关

分析一下TOP命令的资源占用情况，例如第一个资源图。kernel_task占用的资源达到43%，相当高。不知道LINUX下面表现会不会更好

kernel_task较高，应该是整个压测过程中  WRK NGINX PHP-FPM NODEJS  这4组程序的网络传输调度都是由它来搞定，比较繁忙

PHP-FPM的cpu占用相对较低，说明调度都交给内核来执行了。这个符合协程的特性。

图中，NODEJS占用18%左右，WRK占用10%

NGINX 占用CPU略低于PHP-FPM，换个角度来讲，PHP-FPM协程的性能已经接近于NGINX

最后，因为本机压测本机，服务也比较多。如果分开，效果会更好一些。


注意事项
===================

1.请牢记这一点，PHP-FPM协程中，固定每个PHP-FPM进程的协程池大小为128.超出的并发会等待。要想机器拥有更好的并发，请合理配置进程数量。服务能承受的总并发数为所有进程协程池大小的总和。

2.启动时，请注意调整好PHP-FPM进程数量,php-fpm.d/www.conf中

 pm = static
 pm.max_children = 4

3.注意,调整每个进程使用的内存大小.在php.ini中。请根据进程数量和系统可用内存合理计算

 memory_limit = 128M

4.测试时，请注意有些浏览器同一个链接是阻塞的，例如chrome。建议使用curl命令测试。这样可以看到协程的效果

项目调试说明
===================
/ext/coro_http 目录是为了测试协程开发的PHP扩展。
里面已包含详尽的注释。
这个扩展可以当成一个协程实现的demo,里面内容非常简单，开发者可以参照demo开发更多支持协程的扩展，不需要关心PHP-FPM中实现的携程控制器部分

目前只支持macOS和linux

项目调试方法一，编译安装:
=====

1.系统中先要安装libevent库，具体安装方法请自行查找资料

2.项目根目录中执行

 sh buildconf --force

3.项目根目录中执行，安装php

 ./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --enable-maintainer-zts && make && make install

4.修改php-fpm配置文件，设置PHP-FPM进程数，线上环境根据CPU数量和需要的协程数量调整，测试的情况可以设置成1（php-fpm.d/www.conf）
主要是这两个参数

 pm = static
 pm.max_children = 1

5.启动php-fpm

 sudo /usr/local/php7/sbin/php-fpm

6.配置nginx，请自行查阅相关资料,请将nginx的访问目录配置成源码中的tutorial目录，主要是里面的test.php,用于测试


7.这里就可以开始测试了，根据配置好的NGINX，直接访问浏览器(在mac里特别说明)
http://localhost/test.php?a=xx
注意：这里一定要注意，可以开两个窗口访问，但是后面的参数要不一样。因为NGINX对同一个请求，如果相同的参数，NGINX会排队，这里NGINX可能也需要处理一下。不过可以忽略
到这里，就可以看到协程的效果了，结果是，两个窗口同时访问，会先后回来。coro_http会请求nodejs的服务，5秒返回。这里大致可以看到同一个进程的情况下，访问时无阻塞的。

test.php中coro_http_get()方法是实现好的支持协程的扩展，功能是可以请求一个远程地址，返回值是远程地址的输出结果

项目调试方法二，Docker安装:
=====

1.进入tutorial/，执行:

 docker build -t php-fpm-coroutine ./

2.运行docker:

 docker run --privileged php-fpm-coroutine

注意:这块还在调试兼容性，需要进入docker自行启动php，这块启动的时候有一个错误，但是可以正常运行