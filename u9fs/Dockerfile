FROM alpine:latest as build

WORKDIR /
COPY *.patch /
RUN apk add --no-cache git gcc musl-dev make patch
RUN git clone --depth 1 https://github.com/unofficial-mirror/u9fs.git
RUN cat /*.patch | patch -p0
RUN make -C u9fs

FROM plan9d/base:latest
RUN apk add --no-cache socat
COPY --from=build /u9fs/u9fs /bin
COPY entry.sh /bin/

WORKDIR /
EXPOSE 564
ENTRYPOINT ["/bin/entry.sh"]
