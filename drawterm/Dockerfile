FROM alpine:latest

#FROM ubuntu:latest
#RUN apt update
#RUN apt install -y gcc curl libc-dev xorg-dev make tightvncserver
RUN apk add --no-cache gcc curl libc-dev libx11-dev libxt-dev make linux-headers git
RUN apk add --no-cache xvfb x11vnc ratpoison xterm
RUN apk add --no-cache sudo supervisor shadow

# from https://github.com/mschrupp/alpine-vnc
# or https://github.com/danielguerra69/alpine-vnc/
RUN useradd -m -s /bin/bash glenda
RUN echo "glenda:qwerty#123" | chpasswd
RUN echo "glenda ALL=(ALL) ALL" >> /etc/sudoers

RUN mkdir -p /etc/supervisor/conf.d
RUN x11vnc -storepasswd "qwerty#123" /etc/vncpw && chmod 444 /etc/vncpw

ADD supervisord.conf /etc/supervisor/conf.d/

ENV CONF unix
RUN git clone git://git.9front.org/plan9front/drawterm /drawterm
RUN make -C /drawterm

EXPOSE 5900
CMD ["/usr/bin/supervisord","-c","/etc/supervisor/conf.d/supervisord.conf"]
