=========================================
A General Guide to Compiling LLVM Targets
=========================================

Compiling testing targets to LLVM and obtaining the final ``.bc`` LLVM bytecode used to be a tedious process, requiring in most cases to hack the target's build scripts.  A new compilation procedure, based on the LLVM gold plugin, alleviates this problem.  This section describes the LLVM-gold procedure, and assumes that LLVM 2.9 was installed for LLVM target building, according to `the documentation <Installation.html>`_.

General Rules
=============

These guidelines apply generally to all testing targets.  Their application is specific to each target, so please consult the target's build documentation.

- Build every component statically.  This is generally configured via the ``./configure`` script of the target (if any).
- Explicitly disable the shared versions of the components, if possible.  Some ``./configure`` scripts have the ``--disable-shared`` option that achieves this.
- Make sure the LLVM GCC front-end binary is in ``$PATH``, and set the compiler to ``llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm``.
- Set the ``ar`` tool parameters to ``--plugin <LLVM GCC front-end>/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -cru``.
- Set the ``ranlib`` tool to be instead ``ar --plugin <LLVM GCC front-end>/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s``. This is required due to a bug in the original ``ranlib`` that prevents it from recognizing the Gold plugin.


Case Studies
============

For the following case studies, we assume that the LLVM GCC frontend path is stored in the ``$LLVM_GCC_ROOT`` shell variable.

Apache ``httpd`` Server
-----------------------

1. Download the original Apache `httpd 2.2.16 <http://archive.apache.org/dist/httpd/httpd-2.2.16.tar.bz2>`_ distribution, and unpack it. 
2. Configure Apache as follows:

::

  ./configure --disable-shared --with-mpm=worker --enable-proxy-balancer --enable-proxy --enable-static-support \
     --enable-static-htpasswd CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" CFLAGS="-g" \
     RANLIB="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" \
     AR_FLAGS="--plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -cru"

3. Run ``make`` and at the end of the compilation, ``httpd.bc`` should be in the base directory of httpd.

The ``memcached`` Memory Caching System
---------------------------------------

1. ``memcached`` requires ``libevent``, so we'll have to generate a static ``libevent`` library that we will link in the final executable. Download `libevent 1.4.14b <http://monkey.org/~provos/libevent-1.4.14b-stable.tar.gz>`_ and unpack it. From now on, we assume the source directory of libevent is in the ``$LIBEVENT_ROOT`` shell variable.

2. Configure it as follows:

::

  ./configure --disable-shared CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" CFLAGS="-g" \
     RANLIB="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" \
     AR="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so"

3. Run ``make``. At the end of the compilation, you should have the static ``.libs/libevent.a`` archive.

4. Download `memcached 1.4.5 <http://memcached.googlecode.com/files/memcached-1.4.5.tar.gz>`_, and unpack the archive.

5. Configure ``memcached`` as follows:

::

  ./configure --disable-sasl --enable-64bit --disable-docs --disable-dtrace CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" \
     CFLAGS="-g" RANLIB="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" \
     AR="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so"

6. Due to a glitch in the Memcached linking process, the LLVM bitcode isn't statically linked with the previously compiled libevent library.  To fix this, in the generated ``Makefile``, (1) replace the ``LIBS`` variable contents from ``-levent`` to ``$LIBEVENT_ROOT/.libs/libevent.a -lrt``, and (2) set the ``LDFLAGS`` variable to ``-static``.  Alternatively, run the scripts below to do this automatically for you:

::

  sed -i -e "s/^LIBS[ ]*=[ ]*-levent\$/LIBS = ${LIBEVENT_ROOT//\//\\/}\/.libs\/libevent.a -lrt/" Makefile
  sed -i -e 's/^LDFLAGS[ ]*=[ ]*$/LDFLAGS = -lrt/' Makefile

