FROM centos:latest

RUN mkdir -p /data/soft/
#ADD ../../php-fpm-coroutine /data/soft/php-fpm-coroutine
RUN yum -y install git
RUN yum -y install autoconf
RUN yum -y install automake
RUN yum -y install libtool
RUN yum -y install make
RUN yum -y install bison
RUN yum -y install python-devel

RUN cd /data/soft/ && git clone https://github.com/sioomy/php-fpm-coroutine.git
RUN cd /data/soft/ && git clone https://github.com/libevent/libevent.git
RUN cd /data/soft/ && git clone git://git.gnome.org/libxml2

RUN cd /data/soft/libevent && sh autogen.sh && ./configure && make && make install
RUN cd /data/soft/libxml2 && sh autogen.sh && ./configure && make && make install
RUN cd /data/soft/php-fpm-coroutine && sh buildconf --force && ./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --enable-maintainer-zts && make && make install