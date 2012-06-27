************
Using Cloud9
************

*NOTE: This section is obsolete. It will be replaced soon.*


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

Running in a Cluster
====================

Cloud9 supports running on private clusters of commodity hardware, as well as on public cloud infrastructures.  We provide in the `infra/` directory a handy set of Python scripts that run and coordinate Cloud9 executions across a set of machines accessible via SSH through their IP addresses.  Therefore, these scripts can be used both on private clusters, as well as public cloud instances (the nature of the machines is orthogonal to the scripts, as long as they are available through their IP addresses).

In this tutorial, we show how the infrastructure scripts can be used to schedule and run Cloud9 across the entire range of Coreutils.


Setting Up Worker Nodes and Load Balancer
-----------------------------------------

The infrastructure scripts run the Cloud9 workers on the cluster machines, while the load balancer is run locally from where the scripts are being invoked.  Therefore, you need to follow the Cloud9 installation procedure on all the machines that Cloud9 will run on.  You may also want to consider setting up Cloud9 in a single place, and make the installation avaiable to the other machines via NFS.  Finally, you should also make sure the testing targets (the .bc binaries) are available locally to each worker node, and that a dedicated directory for storing the experimental results is available on each of the machines.


Configuring the Infrastructure Scripts
--------------------------------------

The infrastructure scripts rely on multiple configuration files that describe various aspects of the testing setup.  In this section, we show how to write such configuration files for testing the Coreutils UNIX utilities.  All the configuration files reside in directories under `infra/`.

The Machine Setup
~~~~~~~~~~~~~~~~~

The infrastructure scripts assume that worker nodes will run on single or multi-core machines, and that the load balancer is invoked locally.  Multi-core machines can co-locate multiple workers (which will still run independently).

You describe the cluster setup by creating a .hosts file in the `hosts/` directory of the infrastructure scripts. You need to specify there the names of the machines you're using (including the local host), the number of cores available (0 for the local host), the directory where Cloud9 is deployed, the SSH user name used to connect to the machines, the directory where the logs should be stored (the directory must exist), and optionally a root directory of the testing targets.  A sample configuration file is provided as a starting point.

Testing Targets
~~~~~~~~~~~~~~~

Setup the command line arguments of the testing targets by creating a configuration file in the `cmdlines/` directory.  On each line, the configuration file should have an entry of the form <id>:<command line>, where the <id> is an unique name for the command line described after ":".  You may use the provided `coreutils.cmdlines` for testing the Coreutils.

Klee-specific Parameters
~~~~~~~~~~~~~~~~~~~~~~~~

Since each Cloud9 worker node runs the sequential Klee symbolic execution engine, you can specify which Klee-specific parameters to pass to each worker by creating a corresponding configuration file in the `kleecmd/` directory. You may use the provided `coreutils.kcmd` configuration for running the Coreutils. 

Coverable Source Files
~~~~~~~~~~~~~~~~~~~~~~

Setup the file that describes the source files accounted for when computing code coverage in the `coverable/` directory. You may use the provided `coreutils.coverable` file for running the Coreutils.

Experiment Schedule
~~~~~~~~~~~~~~~~~~~

An experiment schedule describes which targets are executed on which machines, and in which order.  The experiment runs in multiple iterations.  During each iteration, multiple targets are being executed in parallel; at the end of an iteration, all jobs are terminated, then the next iteration follows.

You create an experiment schedule file in the `exp/` directory. Each line in the file is one iteration of the experiment -- all the items on the line are executed in parallel. The items are space-separated, and have the form "<testing target id> <# workers> [<host> <# cores>]*", which describes the number of workers allocated for a testing target, and their allocation per phisical hosts.  The provided `coreutils-1.exp` file shows how the entire Coreutils suite would be run with 1-worker Cloud9, on the sample infrastructure provided in the `hosts/` directory.  You can also use the provided `./gen-schedule.py` script that can generate such schedule files given a host and testing targets configuration files.

Running the Testing Experiment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Finally, you should run the `./run-experiment.py` script, which takes as input the name of the configuration files that you created at the previous steps. You should not use the full names of the files, but only their base name. For instance, when specifying the coverage configuration in `coverable/coreutils.coverable`, you should just mention `coreutils`. For instance, the full command line to run Coreutils on 12-worker Cloud9, on an EPFL cluster described by `epfl.hosts`, for 10-minute iterations is::

  ./run-experiment.py -t 600 --strategy random-path,cov-opt epfl coreutils coreutils-12 coreutils coreutils


You can run ./run-experiment.py --help to see the exact format.

Measuring Code Coverage
-----------------------

During or after the experiment, you can check the coverage progress by running the provided `./mine-coverage.py` script. You need to specify the configuration file for the cluster, and one or more test IDs that identify the experiment. The test IDs are printed by the `run-experiment.py` script before the experiment starts (look for a log line saying "Using testing ID: test-xx-xx-xx-xx-xx-xx"). You can also use the "-t" parameter to get a nice tabular ASCII output.

