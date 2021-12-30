FROM ubuntu:20.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt update -y && \
    apt upgrade -y

RUN apt install -y \
    build-essential \
    cmake \
    git \
    python3-pip \
    pkg-config

RUN apt install -y --no-install-recommends \
    libboost-all-dev

RUN pip3 install conan

RUN apt install -y --no-install-recommends \
    psmisc \
    curl \
    rabbitmq-server \
    redis-server


