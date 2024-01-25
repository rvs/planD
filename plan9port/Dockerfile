FROM alpine:latest

RUN apk add git make gcc libc-dev linux-headers perl patch

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
