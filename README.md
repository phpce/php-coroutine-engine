项目说明
===================
这个项目是从PHP官方的github中fork出来的版本，基于php7.1.17版本。本项目主要用来研究PHP-FPM支持协程。

协程是一种可以支持高并发服务器的设计模式。
现在主流的服务器端语言和框架均支持协程调用，包括golang、openresty、java、swoole等。
协程可以降低服务器阻塞，对于需要使用远程调用的服务如使用rpc、mysql、redis等场景，使用协程可以显著提升其服务器性能。
是一种服务器端异步非阻塞IO模型的一种实现方式。

PHP-FPM的设计模式主要是通过多进程来进行并发处理请求。对于服务器资源的使用不充分。
本项目就是通过对PHP-FPM进行优化，通过实现协程模式的PHP-FPM，最终可以像NGINX一样，几个进程即可处理大量的并发请求，充分的利用CPU资源，成倍提PHP服务器性能。

协程原理
===================





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

