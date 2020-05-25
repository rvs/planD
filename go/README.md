# Go code for interacting with Plan 9

Here's what is implemented. All these are mostly useful when running on Plan9:

* `acme` is a random collection of programs doing useful things in Acme windows
* `draw` is a port of Plan 9's libdraw to Go see [graphics(2)](https://9p.io/magic/man2html/2/graphics)
* `games` are two games that require `draw` for rendering
* `plan9` is a simple 9P client implementation (see `plan9/client/cat` for an example of how to use)
* `plumb` is a library for interacting with a [plumber(4)](https://9p.io/magic/man2html/4/plumber)
