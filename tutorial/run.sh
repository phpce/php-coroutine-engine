mkdir -p /data/soft/
mkdir -p /data/www/

yum -y install git
yum -y install autoconf
yum -y install automake
yum -y install libtool
yum -y install make
yum -y install bison
yum -y install python-devel
yum -y install openssl
yum -y install openssl-devel 
yum -y install wget
yum -y install freetype-devel
yum -y install libjpeg-devel
yum -y install unixODBC-devel
yum -y install libpng-devel
yum -y install aspell-devel
yum -y install libwebp-devel
yum -y install systemtap-sdt-devel
yum -y install bzip2 bzip2-devel
yum -y install curl-devel
yum -y install gdbm-devel
yum -y install libicu-devel
yum -y install openldap-devel

yum -y install libxslt libxslt-devel
yum -y install libedit-devel
yum -y install postgresql-devel
yum -y install gcc gcc-c++
yum -y install libxml2-devel

#安装yum扩展包
yum -y install epel-release
#yum -y update

yum -y install libmcrypt-devel
yum -y install gettext-devel
yum -y install supervisor


cd /data/soft/ && wget http://ibiblio.org/pub/Linux/ALPHA/freetds/stable/freetds-stable.tgz
cd /data/soft/ && tar zxvf freetds-stable.tgz
cd /data/soft/freetds-* && ./configure --prefix=/usr --sysconfdir=/etc --with-tdsver=0.91 --enable-msdblib && make && make install

cd /data/soft/ && wget http://nih.at/libzip/libzip-1.2.0.tar.gz
cd /data/soft/ && tar zxvf libzip-1.2.0.tar.gz && cd libzip-1.2.0 && ./configure --prefix=/usr/local && make && make install



cp /usr/local/lib/libzip/include/zipconf.h /usr/local/include/zipconf.h
ln -s /usr/lib64/libldap.so /usr/lib/libldap.so
ln -s /usr/lib64/liblber* /usr/lib/
ln -s /usr/lib64/libz* /usr/lib/


cd /data/soft/ && wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
cd /data/soft/ && tar zxvf libevent-2.1.8-stable.tar.gz
cd /data/soft/libevent-2.1.8-stable && ./configure && make && make install


cd /data/soft/ && wget http://nginx.org/download/nginx-1.14.0.tar.gz
cd /data/soft/ && tar zxf nginx-1.14.0.tar.gz

echo '/usr/local/lib64\
/usr/local/lib\
/usr/lib\
/usr/lib64'>>/etc/ld.so.conf&&ldconfig -v

cd /data/soft/php-coroutine-engine && sh buildconf --force && ./configure --prefix=/usr/local/php7  --enable-fpm --enable-coro_http --enable-mysqlnd --enable-zip --with-pdo-mysql --with-openssl --enable-maintainer-zts --enable-opcache --with-curl --enable-bcmath --enable-calendar  --enable-dtrace --enable-exif --enable-ftp  --enable-mbregex --enable-mbstring --enable-pcntl --enable-phpdbg --enable-phpdbg-webhelper --enable-shmop --enable-soap --enable-sockets --enable-sysvmsg --enable-sysvsem --enable-sysvshm --enable-wddx  --with-bz2 --with-iconv --with-pic --with-xmlrpc  --with-mhash --with-mysql-sockunixunix=/tmp/mysql.sock --with-mysqli=mysqlnd --with-pdo-mysql=mysqlnd --with-kerberos --with-layout=GNU   --with-mcrypt=/usr/include --with-gd --with-jpeg-dir=/usr/include --with-png-dir=/usr/include  --with-webp-dir=/usr/include --with-freetype-dir=/usr/include --with-icu-dir=/usr --with-pspell --with-xsl --with-libzip=/usr/local --enable-zip --with-zlib --with-pdo-dblib --with-pdo-odbc=unixODBC,/usr --with-unixODBC=/usr --with-libedit --enable-dba --with-ldap --with-ldap-sasl --with-pdo-pgsql --with-pgsql --with-gdbm --enable-intl --with-gettext && make && make install

#/usr/local/php7/bin/pecl install redis-3.1.2
#if you need redis 4.1.1 then use this
/usr/local/php7/bin/pecl install redis

cd /data/soft/nginx-1.14.0 && ./configure && make && make install

rm -rf /data/soft
