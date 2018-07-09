#########################################################################
# File Name: maketest.sh
# Author: ma6174
# mail: ma6174@163.com
# Created Time: 二  7/ 3 16:51:03 2018
#########################################################################
#!/bin/bash
/usr/local/php7/bin/phpize && ./configure --with-php-config=/usr/local/php7/bin/php-config --enable-coro_http && make &&sudo  make install
