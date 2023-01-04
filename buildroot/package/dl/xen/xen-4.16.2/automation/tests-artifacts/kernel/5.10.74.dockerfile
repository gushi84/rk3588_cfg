FROM debian:unstable
LABEL maintainer.name="The Xen Project" \
      maintainer.email="xen-devel@lists.xenproject.org"

ENV DEBIAN_FRONTEND=noninteractive
ENV LINUX_VERSION=5.10.74
ENV USER root

RUN mkdir /build
WORKDIR /build

# build depends
RUN apt-get update && \
    apt-get --quiet --yes install \
        build-essential \
        libssl-dev \
        bc \
        curl \
        flex \
        bison \
        libelf-dev \
        && \
    apt-get autoremove -y && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists* /tmp/* /var/tmp/*

# Build the kernel
RUN curl -fsSLO https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-"$LINUX_VERSION".tar.xz && \
    tar xvJf linux-"$LINUX_VERSION".tar.xz && \
    cd linux-"$LINUX_VERSION" && \
    make defconfig && \
    make xen.config && \
    cp .config .config.orig && \
    cat .config.orig | grep XEN | grep =m |sed 's/=m/=y/g' >> .config && \
    make -j$(nproc) bzImage && \
    cp arch/x86/boot/bzImage / && \
    cd /build && \
    rm -rf linux-"$LINUX_VERSION"*
