=================
Installing Cloud9
=================

In order to install Cloud9, you need to perform the steps presented in the following sections.

Building LLVM
-------------

Cloud9 requires LLVM 2.9. You should first download the GCC front-end binaries_, and make sure your ``$PATH`` variable points to the ``bin/`` directory of the binaries distribution.  For convenience, you may make this setup permanent by adding it to your ``~/.profile`` configuration file.

Then download the LLVM 2.9 source_, and run ``./configure --enable-optimized --enable-assertions`` (add the ``--with-binutils-include=/usr/src/binutils/binutils-2.20.51.20100908/include`` option if you plan to compile LLVM targets).

Run ``make`` (you can also run ``make -jN``, where ``N`` is the number of cores on the machine, to speed up the build process).  Add then the resulting ``Release+Asserts/bin`` directory to your ``$PATH``.

For LLVM targets, you also need to copy ``LLVMgold.so`` and ``libLTO.so`` from LLVM's ``Release+Asserts/lib`` directory to ``libexec/gcc/x86_64-unknown-linux-gnu/4.2.1`` in the LLVM GCC front-end binary installation.

Building The Klee C Library
---------------------------

Cloud9 relies on a modified Klee C Library (uClibc) to provide the API implementation for the POSIX interface that doesn't need a model.  You should download our custom uClibc_ library, then run ``./configure --with-llvm=<Your LLVM Path>``.  Finally, run ``make`` and check that the ``lib/libc.a`` file is correctly generated (run ``llvm-nm lib/libc.a`` to see the LLVM symbols listing).

Building Cloud9
---------------

Now it's time to build the actual Cloud9 codebase. Clone the Cloud9 Git repository or download the latest release archive, and then run the configuration script: ``./configure --with-llvm=<Your LLVM Path> --with-uclibc=<The uClibc Path> --enable-posix-runtime --enable-optimized``, then run ``make``. You should see towards the end of the compilation process that the klee, c9-worker and c9-lb binaries are generated as a result of the compilation.


.. _binaries: http://llvm.org/releases/2.9/llvm-gcc4.2-2.9-x86_64-linux.tar.bz2
.. _source: http://llvm.org/releases/2.9/llvm-2.9.tgz
.. _uClibc: https://dslabredmine.epfl.ch/attachments/download/136/klee-c9-uclibc.tar.gz
