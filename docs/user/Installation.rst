=================
Installing Cloud9
=================

In order to install Cloud9, you need to perform the steps presented in the following sections (we assume an Ubuntu 10.10 x64 setup).

Building LLVM
-------------

Cloud9 requires LLVM 2.6. You should first download the GCC front-end binaries_, and make sure your $PATH variable points to the bin/ directory of the binaries distribution.  For convenience, you may make this setup permanent by adding it to your `~/.profile` configuration file.

Then download the LLVM 2.6 source_, run `./configure --enable-optimized`, then `make -jN`, where N is the number of available cores on the machine (for speeding up the build process).  Add then the resulting Release/bin directory to your $PATH.

Building The Klee C Library
---------------------------

Cloud9 relies on a modified Klee C Library (uClibc) to provide the API implementation for the POSIX interface that doesn't need a model.  You should download our custom uClibc_ library, then run `./configure --with-llvm=<Your LLVM Path>`.  Finally, run `make` and check that the lib/libc.a file is correctly generated (run `llvm-nm lib/libc.a` to see the LLVM symbols listing).

Building Cloud9
---------------

Now it's time to build the actual Cloud9 codebase. Clone the Cloud9 Git repository or download the latest release archive, and then run the configuration script: `./configure --with-llvm=<Your LLVM Path> --with-uclibc=<The uClibc Path> --enable-posix-runtime --enable-optimized`, then run `make`. You should see towards the end of the compilation process that the klee, c9-worker and c9-lb binaries are generated as a result of the compilation.


.. _binaries: http://llvm.org/releases/2.6/llvm-gcc-4.2-2.6-x86_64-linux.tar.gz
.. _source: http://llvm.org/releases/2.6/llvm-2.6.tar.gz
.. _uClibc: https://cloud9.epfl.ch/attachments/download/108/klee-c9-uclibc.tar.gz
