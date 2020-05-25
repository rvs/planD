FROM i386/debian

WORKDIR /vx32
RUN apt update
RUN apt install -y git gcc make libc-dev perl libx11-dev
# alternatively https://bitbucket.org/rminnich/vx32/src/default/
# or https://bitbucket.org/yiyus/vx32/src/default/
RUN git clone --depth 1 https://github.com/0intro/vx32 .
RUN cd src ; make -j $(nproc)
