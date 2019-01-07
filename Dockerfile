FROM ubuntu:16.04

LABEL MAINTAINER="KML VISION, devops@kmlvision.com"

# install build deps
RUN apt-get update -qq && \
    apt-get -qq -y install \
      build-essential \
      autoconf \
      autoconf-archive \
      automake \
      openssl \
      locate \
      libssl-dev \
      net-tools \
      nano \
      cmake \
      git \
      libjpeg8-dev \
      libmemcached-dev \
      libopenjpeg-dev \
      libssl-dev \
      libtiff5-dev \
      pkg-config \
      psmisc \
      software-properties-common \
      nginx && \
    apt-get -y build-dep iipimage-server

WORKDIR /usr/src/iipsrv
# copy the source
COPY ./ /usr/src/iipsrv
RUN sh autogen.sh && ./configure --enable-openjpeg && make
RUN cp ./src/iipsrv.fcgi /usr/local/bin/iipsrv.fcgi
RUN ldconfig -v

# set nginx config
COPY docker/nginx.conf /etc/nginx/nginx.conf
# copy entrypoint
COPY docker/start_iipsrv.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

WORKDIR /
EXPOSE 80
ENTRYPOINT ["/entrypoint.sh"]