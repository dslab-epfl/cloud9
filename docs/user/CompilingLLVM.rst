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

Compiling ``memcached``
~~~~~~~~~~~~~~~~~~~~~~~

1. ``memcached`` requires ``libevent``, so we'll have to generate a static ``libevent`` library that we will link in the final executable. Download `libevent 1.4.14b <http://monkey.org/~provos/libevent-1.4.14b-stable.tar.gz>`_ and unpack it. From now on, we assume the source directory of libevent is in the ``$LIBEVENT_ROOT`` shell variable.

2. Since Cloud9's POSIX model supports file descriptor polling through the use of ``select()`` calls, we'll first have to disable any other calls in ``libevent``. In order to do that, locate the ``eventops`` static array in ``event.c``, and comment out all polling mechanisms except for ``select``.

3. Configure ``libevent`` as follows:

::

  ./configure --disable-shared CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" CFLAGS="-g" \
     RANLIB="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" \
     AR="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so"

4. Run ``make``. At the end of the compilation, you should have the static ``.libs/libevent.a`` archive.

5. Download `memcached 1.4.5 <http://memcached.googlecode.com/files/memcached-1.4.5.tar.gz>`_, and unpack the archive.

6. Configure ``memcached`` as follows:

::

  ./configure --disable-sasl --enable-64bit --disable-docs --disable-dtrace CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" \
     CFLAGS="-g" RANLIB="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" \
     AR="ar --plugin $LLVM_GCC_ROOT/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so"

7. Due to a glitch in the Memcached linking process, the LLVM bitcode isn't statically linked with the previously compiled libevent library.  To fix this, in the generated ``Makefile``, (1) replace the ``LIBS`` variable contents from ``-levent`` to ``$LIBEVENT_ROOT/.libs/libevent.a -lrt``, and (2) set the ``LDFLAGS`` variable to ``-static``.  Alternatively, run the scripts below to do this automatically for you:

::

  sed -i -e "s/^LIBS[ ]*=[ ]*-levent\$/LIBS = ${LIBEVENT_ROOT//\//\\/}\/.libs\/libevent.a -lrt/" Makefile
  sed -i -e 's/^LDFLAGS[ ]*=[ ]*$/LDFLAGS = -lrt/' Makefile

8. Run ``make``. At the end of the compilation, the ``memcached`` and ``memcached-debug`` executables should be produced in the base directory, together with their corresponding LLVM ``.bc`` binaries.  From now on, we will use ``memcached-debug.bc`` as our testing target.
 
:Note: You can easily check whether the resulting executable has been correctly compiled by checking whether the ``libevent`` symbols are defined:

::

  llvm-nm memcached-debug.bc | grep event_

In the symbol list, no entry should be marked as undefined ("u").

Basic ``memcached`` Tests
~~~~~~~~~~~~~~~~~~~~~~~~~

1. Assuming that the Cloud9 binaries (``klee``, ``c9-worker``, and ``c9-lb``) are in your ``$PATH``, you can now test some basic concrete executions on ``memcached``:

::

  klee --libc=uclibc --posix-runtime memcached-debug.bc -h

After standard Klee/Cloud9 initialization logs, you should see ``memcached`` showing its version and the list of command line parameters, followed by some execution statistics:

::

  KLEE: done: total instructions = 1119265
  KLEE: done: completed paths = 1
  KLEE: done: generated tests = 1
  [00004.002] Cloud9:	Info:	Instrumentation interrupted. Stopping.

2. Now let's try some more advanced functionality. Let's ask ``memcached`` to start serving on a TCP port:

::

  klee --libc=uclibc --posix-runtime memcached-debug.bc -v -p 11211 -U 0 -u root

At this point, you should see that the memcached execution ends with an error message sounding like ``multiprocess.h:214:  ******** hang (possible deadlock?)``.  

So why is this happening?  It's very unlikely that ``memcached`` would hang or deadlock at initialization when we run it for real.  However, in the context of symbolic execution, ``memcached`` runs in a "closed universe".  The symbolic state contains only its process, and as soon as ``memcached`` initializes and starts listening for connections, no other thread or process exists to be scheduled.  Cloud9 detects this as a "hang", since the system can no longer progress at that point.

Therefore, we need to add a client for memcached in our symbolic execution context.  We deal with this in the next section.

:Note: If, instead of the above error message, you get warnings about external calls being made into ``epoll_*`` functions, then you should make sure that you properly patched ``libevent``, as explained in the previous section.

Client-Server Testing
~~~~~~~~~~~~~~~~~~~~~

``memcached`` comes with a test suite that includes a set of test cases written in C.  We will use those as our starting point for writing the client-server symbolic execution scenario.  You might also notice that some steps in our solution can be considered bad programming practice; however, an elegant implementation is not the focus of the tutorial, and it is left as an engineering exercise.

1. Open the ``memcached.c`` source file and rename the ``main`` function to ``server_main``.  This function will be invoked later by our new main function.
2. 
