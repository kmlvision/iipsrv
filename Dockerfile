FROM ubuntu:16.04

LABEL MAINTAINER="KML VISION, devops@kmlvision.com"

ENV LANG C.UTF-8
ENV DEBIAN_FRONTEND noninteractive

# install build deps
RUN sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list && \
    apt-get update -qq && \
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
      libjansson-dev \
      libjpeg8-dev \
      libmemcached-dev \
      libopenjpeg-dev \
      libssl-dev \
      libtiff5-dev \
      pkg-config \
      psmisc \
      software-properties-common \
      nginx && \
    apt-get -y build-dep iipimage

WORKDIR /usr/src/iipsrv
# copy the source
COPY ./ /usr/src/iipsrv

# build iipsrv
RUN sh autogen.sh && ./configure && make -j2
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