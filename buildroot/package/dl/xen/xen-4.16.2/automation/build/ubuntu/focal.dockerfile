FROM ubuntu:20.04
LABEL maintainer.name="The Xen Project " \
      maintainer.email="xen-devel@lists.xenproject.org"

ENV DEBIAN_FRONTEND=noninteractive
ENV USER root

RUN mkdir /build
WORKDIR /build

# build depends
RUN apt-get update && \
    apt-get --quiet --yes install \
        build-essential \
        zlib1g-dev \
        libncurses5-dev \
        libssl-dev \
        python3-dev \
        xorg-dev \
        uuid-dev \
        libyajl-dev \
        libaio-dev \
        libglib2.0-dev \
        clang \
        libpixman-1-dev \
        pkg-config \
        flex \
        bison \
        # gettext for Xen < 4.13
        gettext \
        acpica-tools \
        bin86 \
        bcc \
        liblzma-dev \
        # libc6-dev-i386 for Xen < 4.15
        libc6-dev-i386 \
        libnl-3-dev \
        ocaml-nox \
        libfindlib-ocaml-dev \
        libsystemd-dev \
        transfig \
        pandoc \
        checkpolicy \
        wget \
        git \
        nasm \
        # QEMU
        ninja-build \
        && \
        apt-get autoremove -y && \
        apt-get clean && \
        rm -rf /var/lib/apt/lists* /tmp/* /var/tmp/*
