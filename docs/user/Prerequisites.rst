=============
Prerequisites
=============

Cloud9 works on 64-bit Linux distributions.  It has been extensively tested on Ubuntu 10.10, and it should work with other distros as well.  Except where otherwise noted, all snippets and examples from this documentation refer to Ubuntu 10.10 64-bit.

In order to properly build and run Cloud9, you need to install the following packages: ``dejagnu``, ``flex``, ``bison``, ``protobuf-compiler``, ``libprotobuf-dev``, ``libboost-thread-dev``, ``libboost-system-dev``.

If you also plan to compile testing targets to LLVM, you also need to install ``binutils-gold`` and ``binutils-source``. After installing the ``binutils`` source code, go to ``/usr/src/binutils``, and run ``tar -xJvf binutils-2.20.51.tar.xz``. You should obtain the ``binutils-2.20.51.20100908`` directory.
