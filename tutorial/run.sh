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

cd /data/soft/php-coroutine-engine
cp ./tutorial/conf/nginx.conf /usr/local/nginx/conf/
cp ./tutorial/conf/www.conf /usr/local/php7/etc/php-fpm.d/
cp ./tutorial/conf/php-fpm.conf /usr/local/php7/etc/
cp ./tutorial/conf/php.ini /usr/local/php7/etc/
cp ./tutorial/conf/supervisord.conf /etc/supervisord.conf


#remove soft 备份
# mkdir -p /data/soft/soft
# cd /data/soft
# cp /usr/lib64/libcrypt.so.1 soft
# cp /usr/local/lib/libzip.so.5 soft
# cp /usr/lib64/libz.so.1 soft
# cp /usr/lib64/libexslt.so.0 soft
# cp /usr/lib64/libresolv.so.2 soft
# cp /usr/lib64/libedit.so.0 soft
# cp /usr/lib64/libncurses.so.5 soft
# cp /usr/lib64/libtinfo.so.5 soft
# cp /usr/lib64/libaspell.so.15 soft
# cp /usr/lib64/libpspell.so.15 soft
# cp /usr/lib64/libpq.so.5 soft
# cp /usr/lib64/librt.so.1 soft
# cp /lib/libsybdb.so.5 soft
# cp /usr/lib64/libmcrypt.so.4 soft
# cp /usr/lib64/libldap-2.4.so.2 soft
# cp /usr/lib64/liblber-2.4.so.2 soft
# cp /usr/lib64/libstdc++.so.6 soft
# cp /usr/lib64/libpng15.so.15 soft
# cp /usr/lib64/libjpeg.so.62 soft
# cp /usr/lib64/libwebp.so.4 soft
# cp /usr/lib64/libgdbm.so.4 soft
# cp /usr/lib64/libcurl.so.4 soft
# cp /usr/lib64/libbz2.so.1 soft
# cp /usr/lib64/libm.so.6 soft
# cp /usr/lib64/libdl.so.2 soft
# cp /usr/lib64/libnsl.so.1 soft
# cp /usr/lib64/libpthread.so.0 soft
# cp /usr/lib64/libxml2.so.2 soft
# cp /usr/lib64/libgssapi_krb5.so.2 soft
# cp /usr/lib64/libkrb5.so.3 soft
# cp /usr/lib64/libk5crypto.so.3 soft
# cp /usr/lib64/libcom_err.so.2 soft
# cp /usr/lib64/libssl.so.10 soft
# cp /usr/lib64/libcrypto.so.10 soft
# cp /usr/lib64/libfreetype.so.6 soft
# cp /usr/lib64/libicui18n.so.50 soft
# cp /usr/lib64/libicuuc.so.50 soft
# cp /usr/lib64/libicudata.so.50 soft
# cp /usr/lib64/libicuio.so.50 soft
# cp /usr/lib64/libodbc.so.2 soft
# cp /usr/lib64/libxslt.so.1 soft
# cp /usr/local/lib/libevent-2.1.so.6 soft
# cp /usr/lib64/libgcc_s.so.1 soft
# cp /usr/lib64/libc.so.6 soft
# cp /lib64/ld-linux-x86-64.so.2 soft
# cp /usr/lib64/libfreebl3.so soft
# cp /usr/lib64/libgcrypt.so.11 soft
# cp /usr/lib64/libgpg-error.so.0 soft
# cp /usr/lib64/libldap_r-2.4.so.2 soft
# cp /usr/lib64/libsasl2.so.3 soft
# cp /usr/lib64/libssl3.so soft
# cp /usr/lib64/libsmime3.so soft
# cp /usr/lib64/libnss3.so soft
# cp /usr/lib64/libnssutil3.so soft
# cp /usr/lib64/libplds4.so soft
# cp /usr/lib64/libplc4.so soft
# cp /usr/lib64/libnspr4.so soft
# cp /usr/lib64/libidn.so.11 soft
# cp /usr/lib64/libssh2.so.1 soft
# cp /usr/lib64/liblzma.so.5 soft
# cp /usr/lib64/libkrb5support.so.0 soft
# cp /usr/lib64/libkeyutils.so.1 soft
# cp /usr/lib64/libltdl.so.7 soft
# cp /usr/lib64/libselinux.so.1 soft
# cp /usr/lib64/libpcre.so.1 soft


# yum -y remove git
# yum -y remove autoconf
# yum -y remove automake
# yum -y remove libtool
# yum -y remove make
# yum -y remove bison
# yum -y remove python-devel
# yum -y remove openssl
# yum -y remove openssl-devel
# yum -y remove wget
# yum -y remove freetype-devel
# yum -y remove libjpeg-devel
# yum -y remove unixODBC-devel
# yum -y remove libpng-devel
# yum -y remove aspell-devel
# yum -y remove libwebp-devel
# yum -y remove systemtap-sdt-devel
# yum -y remove bzip2 bzip2-devel
# yum -y remove curl-devel
# yum -y remove gdbm-devel
# yum -y remove libicu-devel
# yum -y remove openldap-devel

# yum -y remove libxslt libxslt-devel
# yum -y remove libedit-devel
# yum -y remove postgresql-devel
# yum -y remove gcc gcc-c++
# yum -y remove libxml2-devel
# yum -y remove epel-release

# yum -y remove libmcrypt-devel
# yum -y remove gettext-devel

# cd /data/soft/freetds-* && make distclean
# cd /data/soft/libzip-1.2.0 && make distclean

# cd /data/soft/libevent-2.1.8-stable && make distclean

# cd /data/soft
# #恢复
# mv soft/libzip.so.5  /usr/local/lib/libzip.so.5
# mv soft/libsybdb.so.5 /lib/libsybdb.so.5
# mv soft/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
# mv soft/libevent-2.1.so.6 /usr/local/lib/libevent-2.1.so.6
# cp soft/* /usr/lib64

cd /
rm -rf /data/soft





