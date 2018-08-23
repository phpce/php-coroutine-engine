FROM centos:latest

ADD ./ /data/soft/php-coroutine-engine

RUN cd /data/soft/php-coroutine-engine && sh ./tutorial/run.sh

ADD ./tutorial/conf/nginx.conf /usr/local/nginx/conf/
ADD ./tutorial/conf/www.conf /usr/local/php7/etc/php-fpm.d/
ADD ./tutorial/conf/php-fpm.conf /usr/local/php7/etc/
ADD ./tutorial/conf/php.ini /usr/local/php7/etc/
ADD ./tutorial/conf/supervisord.conf /etc/supervisord.conf

CMD ["/usr/bin/supervisord"]
