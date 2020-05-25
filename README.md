# planD: Plan9 from Containerspace

Looking back at it from 2020 it clearly seems that the Original
[Plan9](https://en.wikipedia.org/wiki/Plan_9_from_Bell_Labs) was very
much ahead of its time. Some of its insigts are much better appreciated 30
years later when the general "zeitgeist" of the software developer community
has enthusiastically moved towards fully distributed, eventually consistent
systems. These types of software systems may feel new, but a lot of the
[ideas](http://doc.cat-v.org/plan_9/1st_edition/designing_plan_9) behind them
were first articulated and implemented in Plan9.

One of these fundamental ideas was the notion of a system composed of microservices
communicating via a well established system of network API requests encoded as 9P
messages. Of course, these days, we would express a system like this through use
of containarized applications communicating via REST or gRPC. Some even suggest that
[these new patterns](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/45406.pdf)
will be as influential in software design as object oriented patterns have been. While
the jury is still very much out on that bombastic claim (and arguably the Gang of Four
book has done more damanage than good to teh software industry) one fact is undeniable:
OCI contatiners have very much become the building blocks of moderm distributed systems.

Container technology like Docker, however, is great at helping you package your application
inside of the container, but it doesn't really help in figuring out what is that you are
packaging. There are multiple attempts of retrofiting old school Unix/Linux development tools
to this new cotnainerized world. Arguably, the rise of Alpine Linux as a basis for how UNIX
applications need to be containerized is a direct result of trying to answer this question:
[what are the best building blocks to go inside containers](https://blogs.vmware.com/opensource/2020/02/27/distribution-tools-container-creation/)

This project attempts at giving an answer to the same question that is rooted in Plan9,
rather than UNIX software development culture. After all, what was Plan9 if not an attempt
to fix all the cruft that accumulated in UNIX over the years!

One final note before we deep dive into containerizing Plan9. A lot what you will read here
is condensed presentation of the following 5 resources. Each of them is absolutely amazing
in its own right and contains treasure troves of collective Plan9 wisdom:

* [Plan 9 from Bell Labs](https://9p.io/plan9/index.html)
* [cat-v.org](http://cat-v.org/)
* [Plan 9 Wiki](https://9p.io/wiki/plan9/plan_9_wiki/) 
* [9front.org](http://9front.org/)
* [plan 9 on openbsd](https://www.ueber.net/who/mjl/plan9/plan9-obsd.html)

If you want to help with this project, please take a look at the #TODO list at the bottom.


## Plan9 on "cloud native" infrastructure

Plan9 was concieved in an infrastructure landscape that was massively heterogenous
hardware and (worse yet!) [networking](http://doc.cat-v.org/plan_9/4th_edition/papers/net/)
perspectives. Today's "cloud native" infrastructure is mostly amd64 and arm64 compute
devices connected by TCP/IP networks. On the compute side, you can consume that infrastrucure
by leveraging one of the two runtime APIs:

1. a generalized Virtual Machine API
2. a POSIX-like process group API (typically backed by a Linux kernel)

### 1. VM API

VM APIs are the closes to the original view that Plan9 had of hardware infrastructure. In fact,
the job of a Plan9 kernel can be greatly simplified today since there's absolutely no need to
support the vast collection of various I/O devices: a very basic set of disk, network and serial
devices emulated by the VMM would suffice (it will be nice, however, to support virtio in Plan9:
see our #TODO list). In fact, the boostrapping phases of the Plan9 kernel and how they interplay
is extremely congenial with the "cloud native" infrastructure views of today:

0. Plan9 has an ability of producing a self-contained [multiboot](https://9p.io/magic/man2html/8/9boot)
kernel binary from its build. On some systems this kernel binary can be used directly, on other ones
it needs to be wrapped into a trivial disk image.
1. As a matter of fact, the self-contained nature of the Plan9 kernel image extends beyound just
the binary bits that get loaded into kernel memory. Plan9 as an ability of also packaging filesystem
content of the initial root filesystem right into the kernel binary itself (thus obviating the need
for things like initrd in Linux). The content of this filesystem is managed by the
[root device](http://man.cat-v.org/plan_9/3/root) and what files get included in the binary kernel
image gets managed by [bootdir section](https://9p.io/sources/plan9/sys/src/9/pc/pc) of the architecture
specific (the example above is for x86 architecture) Plan9 kernel build script (exploring various
of those scripts in the same folder will give you a taste of how to build different flavors of Plan9
kernel).
2. One of the files that gets included in that root device filesystem is `/boot/boot`. This executable
is the first one to run and [its job is to connect to the actual filesystem](https://9p.io/magic/man2html/8/boot).
As its last step, `/boot/boot` executes `/$cputype/init [options]` and at that point the Plan9 system
is considered to be [fully bootstrapped](https://9p.io/sources/plan9/sys/src/cmd/init.c). It must be kept
in mind, that init is sensitive to what "flavor" of the Plan9 this is. You should just read the source above,
but the key takeaway is that depending on the flavor you can expect `/rc/bin/cpurc`, `/rc/bin/termrc` and
`lib/profile` hooks to be called before init transitions into effectively a shell `/bin/rc` console.

Most of the time Plan9 is pretty stateless so you can simply kill a VM without any kind of shutdown sequence.
However, if your instance is managing disk (and thus state) you may need to call `fshalt -r` which, provided
that you booted with `acpi=1` in `plan9.ini` will also call [scram(8)](http://man.9front.org/8/fshalt).

Plan9 kernel booting can be further customized by using an intermediate boot kernel (yes! Plan9 effectively
supported kexec way before Linux kids dreamt it up) passing a series of command line arguments to the final
Plan9 kernel via [plan9.ini](http://man.cat-v.org/plan_9/8/plan9.ini). This kind of boot sequence is described
in [9boot](http://man.cat-v.org/plan_9/8/9boot) and is mostly helpful if you decide to do PXE booting.

## References

* [Plan9 legacy](http://9legacy.org/)
* [Plan9 9k](https://bitbucket.org/forsyth/plan9-9k/src/master/)

## XXXX

how differnt things (networks, disk, bitmap) get represented in plan9 via devices (/dev/sdC0) and userspace (partfs)

## TODO

* Provide a containerized (possbily based on linuxkit) build of a Plan9 kernel
* Explore linuxkit's ability to load various images into public clouds as VMs
* Explore running on Plan9 kernel on [firecracker VMM](https://github.com/firecracker-microvm/firecracker/blob/master/docs/getting-started.md)
* Implement [key](https://github.com/lf-edge/eve/blob/master/docs/HYPERVISORS.md#device-models) virtio devices in Plan9 kernel
* Explore [cloudboot](https://cloudboot.org/) for PXE booting Plan9
