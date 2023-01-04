FROM alpine:3.12
LABEL maintainer.name="The Xen Project" \
      maintainer.email="xen-devel@lists.xenproject.org"

ENV USER root

RUN mkdir /build
WORKDIR /build

# build depends
RUN \
  # apk
  apk update && \
  \
  # xen build deps
  apk add argp-standalone && \
  apk add autoconf && \
  apk add automake && \
  apk add bash && \
  apk add curl && \
  apk add curl-dev && \
  apk add dev86 && \
  apk add gcc  && \
  apk add g++ && \
  apk add clang  && \
  # gettext for Xen < 4.13
  apk add gettext && \
  apk add git && \
  apk add iasl && \
  apk add libaio-dev && \
  apk add linux-headers && \
  apk add make && \
  apk add musl-dev  && \
  apk add libc6-compat && \
  apk add ncurses-dev && \
  apk add patch  && \
  apk add python3-dev && \
  apk add texinfo && \
  apk add util-linux-dev && \
  apk add xz-dev && \
  apk add yajl-dev && \
  apk add zlib-dev && \
  \
  # qemu build deps
  apk add bison && \
  apk add flex && \
  apk add glib-dev && \
  apk add libattr && \
  apk add libcap-ng-dev && \
  apk add ninja && \
  apk add pixman-dev && \
  \
  # cleanup
  rm -rf /tmp/* && \
  rm -f /var/cache/apk/*
