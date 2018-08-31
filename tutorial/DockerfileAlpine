FROM alpine:3.8
COPY ./ /data/soft/php-coroutine-engine
RUN cd /data/soft/php-coroutine-engine && sh ./tutorial/runAlpine.sh
CMD ["/usr/bin/supervisord"]
