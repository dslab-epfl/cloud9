*****************
Setting Up Cloud9
*****************

Right now, setting up Cloud9 is an elaborate process.  In the future, we plan to automate most of these tasks.

Prerequisites
=============

Cloud9 works on 64-bit Linux distributions (no 32-bit, sorry).  It has been tested on Ubuntu 10.10 and 11.04 and it should work with other distros as well.

1. Install the required Ubuntu packages:

::

  $ sudo apt-get install dejagnu flex bison protobuf-compiler libprotobuf-dev \
    libboost-thread-dev libboost-system-dev build-essential

2. Prepare the Cloud9 installation directory:

::

  $ mkdir $HOME/cloud9

NOTE: The rest of this guide assumes that all the necessary components are installed under the same directory (``$HOME/cloud9``, in our instructions).

3. Install a custom version of ``binutils``, capable of accepting a LLVM plugin:

::

  $ wget ftp://sourceware.org/pub/binutils/snapshots/binutils.tar.bz2
  $ mkdir binutils
  $ cd binutils && tar -xjvf ../binutils.tar.bz2 --strip-components=1
  $ ./configure --prefix=/usr/local/gold --enable-gold --enable-plugins \
     --enable-threads --enable-bfd=yes
  $ make all
  $ sudo make install

Make sure the generated linker has Gold plugin support:

::

  $ cd /usr/local/gold/bin
  $ sudo rm ./ld
  $ sudo ln ./ld.gold ./ld

4. Install Google's logging library:

::

  $ wget http://google-glog.googlecode.com/files/glog-0.3.1-1.tar.gz
  $ tar -xzvf glog-0.3.1-1.tar.gz && cd glog-0.3.1/
  $ ./configure
  $ make
  $ sudo make install
  $ sudo ldconfig

The last command is necessary to force the linker cache to pick up the new library addition.


Building LLVM and Clang
=======================

1. Cloud9 works with LLVM 3.0 and Clang. Download LLVM and Clang:

::

  $ wget http://llvm.org/releases/3.0/llvm-3.0.tar.gz
  $ wget http://llvm.org/releases/3.0/clang-3.0.tar.gz
  $ tar xzvf llvm-3.0.tar.gz
  $ tar xzvf clang-3.0.tar.gz
  $ mv clang-3.0.src llvm-3.0.src/tools/clang

2. Build the LLVM and Clang binaries:

::

  $ mkdir llvm-3.0.build
  $ cd llvm-3.0.build
  $ ../llvm-3.0.src/configure --enable-optimized --enable-assertions \
    --with-binutils-include=$(pwd)/../binutils/include
  $ make -j8

You may now go grab a coffee while everything is compiled :) At the end of the compilation, the following should be produced in the ``llvm-3.0.build/Release+Asserts`` directory:

* The Clang compiler binaries: ``bin/clang`` and ``bin/clang++``
* The LLVM Gold plug-in: ``lib/LLVMgold.so``

Building Cloud9 and The Klee C Library
======================================

1. Check out Cloud9 and configure the Cloud9 build environment:

::

  $ git clone sbucur.zrh:/var/cloud9/git/src/cloud9.git cloud9
  $ source cloud9/compile/config_cloud9

The ``config_cloud9`` script adds the LLVM and Cloud9 binary directories to your path.

2. Reconfigure LLVM to use the Clang compiler as its default compiler. Clang is now detected on ``$PATH``:

::

  $ cd llvm-3.0.build
  $ ../llvm-3.0.src/configure --enable-optimized --enable-assertions \
    --with-binutils-include=$(pwd)/../../binutils/include
  $ make -j8

*NOTE: Perform the next step in a separete shell sesson.*

3. Checkout and build the Klee C library:

::

  $ git clone sbucur.zrh:/var/cloud9/git/src/klee-uclibc.git klee-uclibc
  $ cd klee-uclibc
  $ source ../cloud9/compile/config_build
  $ ./configure --with-llvm=../llvm-3.0.build
  $ make -j8

At the end of the compilation, you should find the ``lib/libc.a`` static library in your ``klee-uclibc`` directory.

*NOTE: Switch back to the original session.*

4. Configure and build STP:

::

  $ git clone sbucur.zrh:/var/cloud9/git/src/stp.git stp
  $ cd stp
  $ ./scripts/configure --with-prefix=$(pwd)
  $ make -j8

5. Configure and build Cloud9:

::

  $ cd cloud9
  $ ./configure --with-llvmsrc=../llvm-3.0.src \
    --with-llvmobj=../llvm-3.0.build \
    --with-uclibc=../klee-uclibc/ \
    --enable-posix-runtime \
    --with-stp=../stp/
  $ make -j8


