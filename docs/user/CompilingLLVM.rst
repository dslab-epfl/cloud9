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

Apache ``httpd`` Server
-----------------------

We consider the original Apache ``httpd`` 2.2.16 distribution. Unpack the archive and configure Apache as follows::

  ./configure --disable-shared --with-mpm=worker --enable-proxy-balancer --enable-proxy --enable-static-support --enable-static-htpasswd CC="llvm-gcc -flto -use-gold-plugin -Wl,-plugin-opt=also-emit-llvm" CFLAGS="-g" RANLIB="ar --plugin <LLVM GCC front-end>/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -s" AR_FLAGS="--plugin <LLVM GCC front-end>/libexec/gcc/x86_64-unknown-linux-gnu/4.2.1/LLVMgold.so -cru"

Then run ``make`` and at the end of the compilation, ``httpd.bc`` should be in the base directory of httpd.
