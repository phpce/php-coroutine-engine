FROM kong:0.12.3

ADD ./kong-plugins/kong /usr/local/openresty/lualib/kong
#ADD ./etc/kong.conf /etc/kong/kong.conf
ADD ./etc/custom_nginx.conf /usr/local/kong/custom_nginx.conf
ADD ./docker-entrypoint.sh /docker-entrypoint.sh
RUN chmod +x /docker-entrypoint.sh
