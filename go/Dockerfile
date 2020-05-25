FROM golang:1.12.4-alpine

WORKDIR /9fans-go
RUN apk add git
RUN git clone --depth 1 https://github.com/9fans/go.git .
RUN go mod vendor
