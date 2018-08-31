mkdir -p /data/soft/
mkdir -p /data/www/

apk add git
apk add autoconf
apk add automake
apk add libtool
apk add make
apk add bison
apk add python-dev
apk add wget
apk add freetype-dev
apk add libjpeg-turbo-dev
apk add unixodbc-dev
apk add libpng-dev
apk add aspell-dev
apk add libwebp-dev
        # apk add systemtap-sdt-dev
apk add bzip2 bzip2-dev
apk add curl-dev
apk add gdbm-dev
apk add icu-dev
apk add openldap-dev

apk add libxslt libxslt-dev
apk add libedit-dev
apk add postgresql-dev
apk add gcc g++
apk add libxml2-dev

#安装yum扩展包
# apk add epel-release
#yum -y update

apk add libmcrypt-dev
apk add gettext-dev
apk add supervisor

cd /data/soft/ && wget https://github.com/phpce/systemtap/archive/master.zip
cd /data/soft/ && unzip master.zip

cd /data/soft/ && wget https://github.com/phpce/elfutils/archive/master.zip
cd /data/soft/ && unzip master.zip.1
# apk add elfutils-dev
# apk add elfutils-libelf
# apk add libdw-dev
# cd /data/soft/systemtap-master/ && 


# cd /data/soft/ && wget http://ibiblio.org/pub/Linux/ALPHA/freetds/stable/freetds-stable.tgz
# cd /data/soft/ && tar zxvf freetds-stable.tgz
# cd /data/soft/freetds-* && ./configure --prefix=/usr --sysconfdir=/etc --with-tdsver=0.91 --enable-msdblib && make && make install

apk add freetds-dev

apk add libzip

# cd /data/soft/ && wget http://nih.at/libzip/libzip-1.2.0.tar.gz
# cd /data/soft/ && tar zxvf libzip-1.2.0.tar.gz && cd libzip-1.2.0 && ./configure --prefix=/usr/local && make && make install



# cp /usr/local/lib/libzip/include/zipconf.h /usr/local/include/zipconf.h
# ln -s /usr/lib64/libldap.so /usr/lib/libldap.so
# ln -s /usr/lib64/liblber* /usr/lib/
# ln -s /usr/lib64/libz* /usr/lib/

apk add libevent
# cd /data/soft/ && wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
# cd /data/soft/ && tar zxvf libevent-2.1.8-stable.tar.gz
# cd /data/soft/libevent-2.1.8-stable && ./configure && make && make install

apk add nginx
# cd /data/soft/ && wget http://nginx.org/download/nginx-1.14.0.tar.gz
# cd /data/soft/ && tar zxf nginx-1.14.0.tar.gz

# echo '/usr/local/lib64\
# /usr/local/lib\
# /usr/lib\
# /usr/lib64'>>/etc/ld.so.conf&&ldconfig -v

cd /data/soft/php-coroutine-engine && sh buildconf --force && ./configure --prefix=/usr/local/php7  --enable-fpm --enable-coro_http --enable-mysqlnd --enable-zip --with-pdo-mysql --with-openssl --enable-maintainer-zts --enable-opcache --with-curl --enable-bcmath --enable-calendar  --enable-dtrace --enable-exif --enable-ftp  --enable-mbregex --enable-mbstring --enable-pcntl --enable-phpdbg --enable-phpdbg-webhelper --enable-shmop --enable-soap --enable-sockets --enable-sysvmsg --enable-sysvsem --enable-sysvshm --enable-wddx  --with-bz2 --with-iconv --with-pic --with-xmlrpc  --with-mhash --with-mysql-sockunixunix=/tmp/mysql.sock --with-mysqli=mysqlnd --with-pdo-mysql=mysqlnd --with-kerberos --with-layout=GNU   --with-mcrypt=/usr/include --with-gd --with-jpeg-dir=/usr/include --with-png-dir=/usr/include  --with-webp-dir=/usr/include --with-freetype-dir=/usr/include --with-icu-dir=/usr --with-pspell --with-xsl --with-libzip=/usr/local --enable-zip --with-zlib --with-pdo-dblib --with-pdo-odbc=unixODBC,/usr --with-unixODBC=/usr --with-libedit --enable-dba --with-ldap --with-ldap-sasl --with-pdo-pgsql --with-pgsql --with-gdbm --enable-intl --with-gettext && make && make install

#/usr/local/php7/bin/pecl install redis-3.1.2
#if you need redis 4.1.1 then use this
/usr/local/php7/bin/pecl install redis

# cd /data/soft/nginx-1.14.0 && ./configure && make && make install
mpdir -p /usr/local/nginx/sbin
ln -s /usr/sbin/nginx /usr/local/nginx/sbin/nginx

cd /data/soft/php-coroutine-engine
cp ./tutorial/conf/nginx.conf /etc/nginx/conf/
cp ./tutorial/conf/www.conf /usr/local/php7/etc/php-fpm.d/
cp ./tutorial/conf/php-fpm.conf /usr/local/php7/etc/
cp ./tutorial/conf/php.ini /usr/local/php7/etc/
cp ./tutorial/conf/supervisord.conf /etc/supervisord.conf
cd /
rm -rf /data/soft
