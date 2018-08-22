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
RUN yum -y install freetype-devel
RUN yum -y install libjpeg-devel
RUN yum -y install unixODBC-devel
RUN yum -y install libpng-devel
RUN yum -y install aspell-devel
RUN yum -y install libwebp-devel
RUN yum -y install systemtap-sdt-devel
RUN yum -y install bzip2 bzip2-devel
RUN yum -y install curl-devel
RUN yum -y install gdbm-devel
RUN yum -y install libicu-devel
RUN yum -y install openldap-devel

RUN yum -y install libxslt libxslt-devel
RUN yum -y install libedit-devel
RUN yum -y install postgresql-devel
RUN yum -y install gcc gcc-c++
RUN yum -y install libxml2-devel

#安装yum扩展包
RUN yum -y install epel-release
RUN yum -y update

RUN yum -y install libmcrypt-devel
RUN yum -y install gettext-devel
RUN yum -y install supervisor


RUN cd /data/soft/ && wget http://ibiblio.org/pub/Linux/ALPHA/freetds/stable/freetds-stable.tgz
RUN cd /data/soft/ && tar zxvf freetds-stable.tgz
RUN cd /data/soft/freetds-* && ./configure --prefix=/usr --sysconfdir=/etc --with-tdsver=0.91 --enable-msdblib && make && make install

RUN cd /data/soft/ && wget http://nih.at/libzip/libzip-1.2.0.tar.gz
RUN cd /data/soft/ && tar zxvf libzip-1.2.0.tar.gz && cd libzip-1.2.0 && ./configure --prefix=/usr/local && make && make install



RUN cp /usr/local/lib/libzip/include/zipconf.h /usr/local/include/zipconf.h
RUN ln -s /usr/lib64/libldap.so /usr/lib/libldap.so
RUN ln -s /usr/lib64/liblber* /usr/lib/
RUN ln -s /usr/lib64/libz* /usr/lib/


#RUN cd /data/soft/ && git clone https://github.com/phpcoro/php-fpm-coro.git
ADD ./ /data/soft/php-fpm-coro


RUN cd /data/soft/ && wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
RUN cd /data/soft/ && tar zxvf libevent-2.1.8-stable.tar.gz
RUN cd /data/soft/libevent-2.1.8-stable && ./configure && make && make install


RUN cd /data/soft/ && wget http://nginx.org/download/nginx-1.14.0.tar.gz
RUN cd /data/soft/ && tar zxf nginx-1.14.0.tar.gz

RUN echo '/usr/local/lib64\
/usr/local/lib\
/usr/lib\
/usr/lib64'>>/etc/ld.so.conf&&ldconfig -v

RUN cd /data/soft/php-fpm-coro && sh buildconf --force && ./configure --prefix=/usr/local/php7  --enable-fpm --enable-coro_http --enable-mysqlnd --enable-zip --with-pdo-mysql --with-openssl --enable-maintainer-zts --enable-opcache-file --with-curl --enable-bcmath --enable-calendar  --enable-dtrace --enable-exif --enable-ftp  --enable-mbregex --enable-mbstring --enable-pcntl --enable-phpdbg --enable-phpdbg-webhelper --enable-shmop --enable-soap --enable-sockets --enable-sysvmsg --enable-sysvsem --enable-sysvshm --enable-wddx  --with-bz2 --with-iconv --with-pic --with-xmlrpc  --with-mhash --with-mysql-sockunixunix=/tmp/mysql.sock --with-mysqli=mysqlnd --with-pdo-mysql=mysqlnd --with-kerberos --with-layout=GNU   --with-mcrypt=/usr/include --with-gd --with-jpeg-dir=/usr/include --with-png-dir=/usr/include  --with-webp-dir=/usr/include --with-freetype-dir=/usr/include --with-icu-dir=/usr --with-pspell --with-xsl --with-libzip=/usr/local --enable-zip --with-zlib --with-pdo-dblib --with-pdo-odbc=unixODBC,/usr --with-unixODBC=/usr --with-libedit --enable-dba --with-ldap --with-ldap-sasl --with-pdo-pgsql --with-pgsql --with-gdbm --enable-intl --with-gettext && make && make install

RUN cd /data/soft/nginx-1.14.0 && ./configure && make && make install

ADD ./conf/nginx.conf /usr/local/nginx/conf/
ADD ./conf/www.conf /usr/local/php7/etc/php-fpm.d/
ADD ./conf/php-fpm.conf /usr/local/php7/etc/
ADD ./conf/php.ini /usr/local/php7/lib/
ADD ./conf/supervisord.conf /etc/supervisord.conf

CMD ["/usr/bin/supervisord"]
