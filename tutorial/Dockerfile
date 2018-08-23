FROM centos:latest
COPY ./ /data/soft/php-coroutine-engine
RUN cd /data/soft/php-coroutine-engine && sh ./tutorial/run.sh
CMD ["/usr/bin/supervisord"]
