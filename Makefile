9FRONT_20231122_amd64=http://iso.only9fans.com/release/9front-10277.amd64.iso.gz.torrent
9FRONT_20231122_i386=http://iso.only9fans.com/release/9front-10277.386.iso.gz.torrent
9FRONT_20231122_arm64=http://iso.only9fans.com/release/9front-10277.pi3.img.gz.torrent
9FRONT_20231122_arm32=http://iso.only9fans.com/release/9front-10277.pi.img.gz.torrent

9FRONT_VERSION=20231122

ORG=plan9d
VERSION=latest
ALL=base plan9port u9fs authsrv9 drawterm 9front-amd64 9front-arm64 9front 9vx go go-p9p

ARCH=$(shell if [ `uname -m` = x86_64 ]; then echo amd64; else echo arm64; fi)

9front-amd64: BUILD_ARGS=--build-arg P9FRONT_URL=$(9FRONT_$(9FRONT_VERSION)_amd64)
# 9front-amd64: VERSION=$(9FRONT_VERSION)
9front-arm64: BUILD_ARGS=--build-arg P9FRONT_URL=$(9FRONT_$(9FRONT_VERSION)_arm64)
# 9front-arm64: VERSION=$(9FRONT_VERSION)
9front: BUILD_ARGS=--build-arg VERSION=$(9FRONT_VERSION)
# 9front: VERSION=$(9FRONT_VERSION)

# List dependencies between Docker images
ifneq ($(FORCE_REBUILD),)
u9fs: base
9front-amd64: base plan9port u9fs
9front-arm64: base plan9port u9fs
9front: 9front-amd64 9front-arm64 u9fs
endif

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

run-u9fs:
	docker run -it --rm -p 127.0.0.1:564:564 $(ORG)/u9fs

run-authsrv9:
	docker run -it --rm -p 127.0.0.1:567:567 $(ORG)/authsrv9

run-drawterm:
	docker run -it --rm -p 127.0.0.1:5900:5900 $(ORG)/drawterm

blobs:
	mkdir $@

.PHONY: $(ALL) clean run-amd64 run-arm64 help

.DEFAULT_GOAL := help
help:
	@echo "Builds the following containers (with mostly Alpine as base):"
	@echo "  . $(ORG)/base      - sets up users that map to plan9 ones"
	@echo "  . $(ORG)/plan9port - all the tools from plan9port running on Unix"
	@echo "  . $(ORG)/u9fs      - 9p backend for serving content of a container based on it"
	@echo "                       Usage: docker run -p 127.0.0.1:564:564 -v XXX:/rootfs $(ORG)/u9fs"
	@echo "  . $(ORG)/authsrv9  - authsrv9 subsystem for all the services to auth against"
	@echo "                       Usage: docker run -p 127.0.0.1:567:567 -v XXX:/rootfs $(ORG)/authsrv9"
	@echo "  . $(ORG)/drawterm  - drawterm with a VNC frontend"
	@echo "                       Usage: docker run -p 127.0.0.1:5900:5900 # connect your VNC client to localhost:0"
	@echo "  . $(ORG)/go-p9p    - A modern, performant 9P library for Go (with clients)"
	@echo "                       Usage: WIP"
	@echo "  . $(ORG)/9vx       - mostly exists for historical/archival reasons (not currently used)"
	@echo "  . $(ORG)/go        - Plan9 related Go libraries (not currently used)"
	@echo "  . $(ORG)/9front"
	@echo "  . $(ORG)/9front[-amd64|-arm64]"
	@echo
	@echo "You can ommit the organization name '$(ORG)' when using make targets. E.g. 'make u9fs'"
	@echo
	@echo "You can run the following services (with default settings) via 'make run-[u9fs|authsrv9|drawterm|amd64|arm64]'"
