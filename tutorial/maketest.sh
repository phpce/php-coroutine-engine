#########################################################################
# File Name: make.sh
# Author: ma6174
# mail: ma6174@163.com
# Created Time: 四  6/21 16:15:12 2018
#########################################################################
#!/bin/bash
#./configure --enable-debug --prefix=/usr/local/php7 --enable-fpm && make && make install && sudo killall php-fpm&& sudo /usr/local/php7/sbin/php-fpm
./configure --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --enable-mysqlnd --enable-zip --with-pdo-mysql --with-openssl=/usr/local/Cellar/openssl/1.0.2o_2/ --enable-maintainer-zts --enable-opcache-file && make && make install && sudo /usr/local/php7/sbin/php-fpm
#./configure --enable-debug --prefix=/usr/local/php7 --enable-fpm && make && make install && sudo gdb /usr/local/php7/sbin/php-fpm
#./configure CFLAGS="-ggdb3" CXXFLAGS="-ggdb3" --prefix=/usr/local/php7 --enable-fpm --enable-coro_http --with-openssl --enable-maintainer-zts && make && make install && sudo gdb /usr/local/php7/sbin/php-fpm
