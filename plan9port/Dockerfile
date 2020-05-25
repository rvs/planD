FROM alpine:3.11.6

RUN apk add git make gcc libc-dev linux-headers perl

# workaround for https://github.com/9fans/plan9port/issues/36
WORKDIR /libucontext
RUN git clone --depth 1 https://github.com/kaniini/libucontext.git . &&\
    make ARCH=x86_64 DESTDIR=/usr install && rm -rf /libucontext

WORKDIR /plan9port
RUN git clone --depth 1 https://github.com/9fans/plan9port.git . ; rm -rf .git
COPY hjfs src/cmd/hjfs
COPY *patch ./
RUN cat *patch | patch -p1
RUN ./INSTALL

WORKDIR /srv
ENV PLAN9 /plan9port
ENV PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/plan9port/bin
ENV NAMESPACE /srv
