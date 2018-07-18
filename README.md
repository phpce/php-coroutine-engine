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

这里PHP-FPM就是充当着调度器的功能。当有请求进来之后，由调度器来触发执行。在这次请求的执行过程中，如果遇到远程调用，则需要在请求发送之后PHP主动将控制权交给调度器。调度器这个时候再决定处理其他的请求或者是处理之前执行远程时返回来可以据需执行的请求。
每个请求之间有独立的存储，互相不干扰。

进度说明
===================
本项目已经基本通过PHP-FPM实现了协程。为了便于测试，只开通了5个协程。
项目还处于未完成阶段，待处理任务：
1.load_module
的处理，现在只有第一个协程可以正常加载默认的系统扩展。所以一些依赖扩展的功能在本项目中还用不了。比如phpinfo函数

2.内存泄漏问题
之前为了快速调试项目，在PHP代码中有很多未释放的变量需要处理，这个需要事件整理一下

3.协程使用限制
默认程序中会声明一个协程池，为方便调试写了5个。超出会报错。所以暂时不支持压测。这个需要优化


项目调试说明
===================
/ext/coro_http 目录是为了测试协程开发的PHP扩展。
目前只支持macOS和linux

项目调试方法：

1.系统中先要安装libevent库，具体安装方法请自行查找资料

2.项目根目录中执行 
sh buildconf --force

3.项目根目录中执行，安装php
./configure --prefix=/usr/local/php7 --enable-fpm --enable-maintainer-zts && make && make install

4.安装协程扩展coro_http,进入ext/coro_http
执行刚刚安装好的php对应的phpize
/usr/local/php7/bin/phpize
./configure --with-php-config=/usr/local/php7/bin/php-config --enable-coro_http && make &&sudo  make install

5.修改php-fpm配置文件，将进程数设置成1（php-fpm.d/www.conf）
主要是这两个参数
pm = static
pm.max_children = 1

6.修改PHP配置文件，增加coro_http扩展(具体位置具体修改)
extension=/Users/sioomy/work/php-src/ext/coro_http/modules/coro_http.so

7.启动php-fpm
sudo /usr/local/php7/sbin/php-fpm

8.配置nginx，请自行查阅相关资料,请将nginx的访问目录配置成源码中的tutorial目录，主要是里面的test.php,用于测试

The PHP Interpreter
===================
This is the github mirror of the official PHP repository located at
http://git.php.net.

[![Build Status](https://secure.travis-ci.org/php/php-src.svg?branch=master)](http://travis-ci.org/php/php-src)
[![Build status](https://ci.appveyor.com/api/projects/status/meyur6fviaxgdwdy?svg=true)](https://ci.appveyor.com/project/php/php-src)

Pull Requests
=============
PHP accepts pull requests via github. Discussions are done on github, but
depending on the topic can also be relayed to the official PHP developer
mailing list internals@lists.php.net.

New features require an RFC and must be accepted by the developers.
See https://wiki.php.net/rfc and https://wiki.php.net/rfc/voting for more
information on the process.

Bug fixes **do not** require an RFC, but require a bugtracker ticket. Always
open a ticket at https://bugs.php.net and reference the bug id using #NNNNNN.

    Fix #55371: get_magic_quotes_gpc() throws deprecation warning

    After removing magic quotes, the get_magic_quotes_gpc function caused
    a deprecate warning. get_magic_quotes_gpc can be used to detected
    the magic_quotes behavior and therefore should not raise a warning at any
    time. The patch removes this warning

We do not merge pull requests directly on github. All PRs will be
pulled and pushed through http://git.php.net.


Guidelines for contributors
===========================
- [CODING_STANDARDS](/CODING_STANDARDS)
- [README.GIT-RULES](/README.GIT-RULES)
- [README.MAILINGLIST_RULES](/README.MAILINGLIST_RULES)
- [README.RELEASE_PROCESS](/README.RELEASE_PROCESS)

