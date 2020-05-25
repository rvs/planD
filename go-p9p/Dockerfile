FROM golang:1.12.4-alpine

WORKDIR /go-p9p
RUN apk add git
RUN git clone --depth 1 https://github.com/rvs/go-p9p .
RUN go mod init github.com/docker/go-p9p && go mod download
RUN cd cmd/9pr ; go build .
RUN cd cmd/9ps ; go build .
