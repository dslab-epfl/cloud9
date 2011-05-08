=======================================
Testing Complex Multi-Threaded Programs
=======================================

Cloud9 can symbolically execute programs that make complex use of their operating system environment, including spawning multiple threads, forking processes, or communicating over the network.  If you are already familiar with Klee, Cloud9 also provides an enhanced version of Klee, equipped with the POSIX model necessary to run such programs.

In the `examples/` directory, you can find programs that exercise various aspects of the POSIX environment.  In particular, `examples/prod-cons` contains an implementation of a distributed producer-consumer system, where multiple client producer processes connect over the network to a central consumer server.  The server spawns multiple threads to handle client connections, and also maintains a pool of threads that execute consumer routines.  Pthread-specific synchronization API is used inside the server.

To run these examples or any other program, you have multiple options:

1. Run the fully-fledged Cloud9 on a cluster of nodes (see the next section for info on how to use the provided cluster infrastructure scripts).

2. Run a single Cloud9 worker in stand-alone mode (the `--stand-alone` flag). For instance, in order to run the examples/prod-cons/cs1.c (compiled in cs1.bc), one should run::

    c9-worker --stand-alone --posix-runtime --libc=uclibc examples/prod-cons/cs1.bc

3. Run the enhanced Klee::

    klee --posix-runtime --libc=uclibc examples/prod-cons/cs1.bc

From a functional perspective, 2. and 3. are equivalent when running Cloud9 in single-node.  However, the Cloud9 worker implementation uses different data structures to store the local execution tree and uses the Cloud9 search strategy interface, which provides a different set of strategies.
