FROM centos:latest

RUN mkdir -p /data/soft/

RUN yum -y install git
RUN yum -y install autoconf
RUN yum -y install automake
RUN yum -y install libtool
RUN yum -y install make
RUN yum -y install bison
RUN yum -y install python-devel
RUN yum -y install openssl
RUN yum -y install openssl-devel 
RUN yum -y install wget
RUN cd /data/soft/ && wget https://mirrors.tuna.tsinghua.edu.cn/epel/epel-release-latest-7.noarch.rpm
RUN cd /data/soft/ && rpm -ivh epel-release-latest-7.noarch.rpm
RUN yum -y install supervisor

RUN cd /data/soft/ && git clone http://github.com/sioomy/php-fpm-coroutine.git
RUN cd /data/soft/ && git clone http://github.com/libevent/libevent.git
RUN cd /data/soft/ && git clone git://git.gnome.org/libxml2
RUN cd /data/soft/ && wget http://nginx.org/download/nginx-1.14.0.tar.gz
RUN cd /data/soft/ && tar zxf nginx-1.14.0.tar.gz

RUN cd /data/soft/libevent && sh autogen.sh && ./configure && make && make install
RUN cd /data/soft/libxml2 && sh autogen.sh && ./configure && make && make install
RUN cd /data/soft/php-fpm-coroutine && sh buildconf --force && ./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --with-openssl --enable-maintainer-zts && make && make install
RUN cd /data/soft/nginx-1.14.0 && ./configure && make && make install

ADD ./conf/nginx.conf /usr/local/nginx/conf/
ADD ./conf/www.conf /usr/local/php7/etc/php-fpm.d/
ADD ./conf/php-fpm.conf /usr/local/php7/etc/
ADD ./conf/php.ini /usr/local/php7/lib/
ADD ./conf/supervisord.conf /etc/supervisord.conf

CMD ["/usr/bin/supervisord"]