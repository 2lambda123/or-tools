# Create a virtual environment with all tools installed
# ref: https://hub.docker.com/_/ubuntu
FROM ubuntu:rolling AS base
LABEL maintainer="corentinl@google.com"
# Install system build dependencies
ENV PATH=/usr/local/bin:$PATH
RUN apt-get update -qq \
&& DEBIAN_FRONTEND=noninteractive apt-get install -yq \
 git wget libssl-dev pkg-config build-essential \
 autoconf libtool zlib1g-dev lsb-release \
&& apt-get clean \
&& rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Install CMake 3.21.1
RUN wget -q "https://cmake.org/files/v3.21/cmake-3.21.1-linux-x86_64.sh" \
&& chmod a+x cmake-3.21.1-linux-x86_64.sh \
&& ./cmake-3.21.1-linux-x86_64.sh --prefix=/usr/local/ --skip-license \
&& rm cmake-3.21.1-linux-x86_64.sh
CMD [ "/usr/bin/bash" ]

FROM base AS swig
RUN apt-get update -qq \
&& apt-get install -yq swig \
&& apt-get clean \
&& rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
