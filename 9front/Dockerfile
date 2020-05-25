ARG VERSION
FROM plan9d/9front-arm64:${VERSION} as arm64
ARG VERSION
FROM plan9d/9front-amd64:${VERSION} as amd64

FROM plan9d/u9fs:latest
ENV ROOT /plan9
COPY --from=amd64 /plan9 /plan9
COPY --from=arm64 /plan9/arm64 /plan9/
COPY --from=arm64 /plan9/acme/bin/arm64 /plan9/acme/bin/arm64
COPY --from=arm64 /plan9/adm/users /plan9/adm/users
COPY --from=arm64 /plan9/usr/glenda/bin/arm64 /plan9/usr/glenda/bin/arm64
COPY --from=arm64 /plan9/sys/src/boot/bcm /plan9/sys/src/boot/bcm
