9FRONT_201910080_amd64=http://bell-labs.co/9front/iso/9front-7408.1d345066125a.386.iso.gz.torrent
9FRONT_201910080_arm64=http://bell-labs.co/9front/iso/9front-7408.1d345066125a.pi3.img.gz.torrent
9FRONT_201910080_arm32=http://bell-labs.co/9front/iso/9front-7408.1d345066125a.pi.img.gz.torrent

9FRONT_VERSION=201910080

ORG=plan9d
VERSION=latest
ALL=base plan9port u9fs authsrv9 drawterm 9front-amd64 9front-arm64 9front 9vx go go-p9p

ARCH=$(shell if [ `uname -m` = x86_64 ]; then echo amd64; else echo arm64; fi)

9front-amd64: BUILD_ARGS=--build-arg P9FRONT_URL=$(9FRONT_$(9FRONT_VERSION)_amd64)
9front-amd64: VERSION=$(9FRONT_VERSION)
9front-arm64: BUILD_ARGS=--build-arg P9FRONT_URL=$(9FRONT_$(9FRONT_VERSION)_arm64)
9front-arm64: VERSION=$(9FRONT_VERSION)
9front: BUILD_ARGS=--build-arg VERSION=$(9FRONT_VERSION)
9front: VERSION=$(9FRONT_VERSION)

all: $(ALL)
	@echo Done building

clean:
	rm -rf blobs

$(ALL):
	docker build $(BUILD_ARGS) -t $(ORG)/$@:$(VERSION) $@
	@if [ -n "$(DOCKER_PUSH)" ]; then docker push $(ORG)/$@:$(VERSION) ;fi

blobs/9pc: | blobs
	docker cp `docker create $(ORG)/9front-amd64:$(9FRONT_VERSION) a`:plan9/386/$(notdir $@) $@

blobs/9pi4 blobs/9pi3: | blobs
	docker cp `docker create $(ORG)/9front-arm64:$(9FRONT_VERSION) a`:plan9/arm64/$(notdir $@) $@

run-amd64: blobs/9pc
	ARCH=amd64 scripts/run-cpu-server.sh $<

run-arm64: blobs/9pi3
	ARCH=arm64 scripts/run-cpu-server.sh $<

blobs:
	mkdir $@

.PHONY: $(ALL) clean run-amd64 run-arm64
