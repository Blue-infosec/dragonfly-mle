FROM debian:stretch-slim
LABEL  maintainer="mle@counterflowai.com" domain="counterflow.ai"
#
# Install dependencies
#
RUN apt update --fix-missing
RUN apt install -y zlib1g-dev libluajit-5.1 liblua5.1-dev lua-socket libcurl4-openssl-dev libatlas-base-dev libhiredis-dev git make libmicrohttpd-dev
#
# Build dragonfly-mle
#
COPY ./ /tmp/dragonfly-mle
WORKDIR /tmp/dragonfly-mle/src
RUN make && make install
#
# Build redis
#
RUN git clone https://github.com/antirez/redis /tmp/redis
WORKDIR /tmp/redis/src
RUN make && make install
#
# Build redis ML
#
RUN git clone https://github.com/RedisLabsModules/redis-ml /tmp/redis-ml
WORKDIR /tmp/redis-ml/src
RUN make
RUN cp redis-ml.so /usr/local/lib
#
# Cleanup/Maintainance 
#
RUN rm -rf /tmp/*
RUN mkdir -p /opt/suricata/var
RUN apt purge -y build-essential git make && apt -y autoremove
#
# Entry
#
WORKDIR /usr/local/dragonfly-mle
ENTRYPOINT redis-server --loadmodule /usr/local/lib/redis-ml.so --daemonize yes && /usr/local/dragonfly-mle/bin/dragonfly-mle

