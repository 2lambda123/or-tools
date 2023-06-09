# Create a virtual environment with all tools installed
# ref: https://hub.docker.com/_/alpine
FROM alpine:edge AS env
LABEL maintainer="corentinl@google.com"
# Install system build dependencies
ENV PATH=/usr/local/bin:$PATH
RUN apk add --no-cache git build-base linux-headers zlib-dev
RUN apk add --no-cache -X http://dl-cdn.alpinelinux.org/alpine/edge/testing bazel4

# Install OpenJDK17
# note: default-jvm will now point to java-17-openjdk
RUN apk add --no-cache openjdk17 --repository=http://dl-cdn.alpinelinux.org/alpine/edge/testing/
# Remove infinite loop since jre point to the current directory
# otherwise bazel issue an error and stop...
RUN rm /usr/lib/jvm/default-jvm/jre

ENV JAVA_HOME=/usr/lib/jvm/default-jvm
ENV PATH=$JAVA_HOME/bin:$PATH

# Install Python
RUN apk add --no-cache python3-dev py3-pip
RUN ln -s $(command -v python3) /usr/bin/python

FROM env AS devel
WORKDIR /home/project
COPY . .

FROM devel AS build
RUN bazel version
RUN bazel build --host_javabase=@local_jdk//:jdk --cxxopt=-std=c++17 ...

FROM build AS test
RUN bazel test --host_javabase=@local_jdk//:jdk --cxxopt=-std=c++17 ...
