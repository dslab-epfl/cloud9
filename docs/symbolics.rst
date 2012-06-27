**************************
Specifying Symbolic Inputs
**************************

In Cloud9, there are multiple ways to specify symbolic inputs.  In this section, we present the concepts behind each symbolic input type, together with how they are used to form symbolic files and command line arguments.  Specifying symbolic inputs is done by adding specially-recognized command line arguments to the program under test, which are intercepted and processed by Cloud9 before the program executes.

Pure Symbolic Inputs
====================

This is a form of symbolic input inherited from KLEE.  Pure symbolic inputs are memory buffers (or arrays, in solver terminology) whose contents are fully unconstrained.  Whenever pure symbolic inputs are used in program operations, a symbolic expression is constructed, containing the symbolic inputs as free variables.  At branching points, path constraints are created for control flow conditions whose outcome depend on the symbolic inputs.  The pure symbolic inputs are the basic structure that induces an execution tree instead of a linear program path.

Pure Symbolic Command Line Arguments
------------------------------------

One can specify pure symbolic command line arguments in two ways:
 * Using the ``--sym-arg <size>`` argument, which is replaced by Cloud9 with a single pure symbolic argument of size at most `<size>`.
 * Using the ``--sym-args <min-count> <max-count> <size>`` argument, which is replaced by Cloud9 with `n` arguments of size at most `<size>`, where `n` is at least `<min-count>` and at most `<max-count>`.  Cloud9 creates an execution state in the symbolic execution tree for each possible value of `<n>`.

Pure Symbolic Files
-------------------

Symbolic files (pure or not) belong to a "symbolic file system" overlayed on top of the real host file system, and are named using capital letters, in alphabetical order (``A``, ``B``, ``C``, ...).  Whenever the target program attempts to open a file named like this, the environment model intercepts the syscall and opens instead the corresponding symbolic file.

Similarly to command line arguments, pure symbolic files can be specified in two ways:
 * Using the ``--sym-files <count> <size>`` argument, which causes Cloud9 to create `<count>` pure symbolic files of size `<size>`.
 * Using the ``--sym-file <file-name>`` argument, which causes Cloud9 to obtain the size of the real host file `<file-name>`, and create a pure symbolic file of the same size for the target program.

As a special case, one may also use the ``--con-file <file-name>`` argument, which is similar to the ``--sym-file`` one, except that the symbolic file contents are replaced with the concrete contents of the real host file pointed to by `<file-name>`, instead.  Note that the generated concrete file is still part of the symbolic file system, so it is both maintained by the environment model and accessed using the symbolic file naming conventions.


Symbolic Fuzzing
================

Pure symbolic inputs may not scale well for large input sizes.  Large blocks of symbolic memory produce query expressions that bottleneck the constraint solver.  The size of the symbolic execution tree may also increase exponentially in the input size, if that input controls the execution of program loops.  This, in turn, may reduce the effectiveness of the exploration strategy.

Symbolic fuzzing aims at reducing the effective size of the symbolic memory, while allowing the program to take large inputs that may drive it through more behaviors.  Symbolic fuzzing conceptually works by creating a (potentially large) concrete input, and then flipping (or `fuzzing`) portions of it from concrete to pure symbolic.

This concept is inspired from blackbox fuzzing, where an original input is randomly altered, and then passed to the program.  While blackbox fuzzing allows checking for one alternate behavior at a time, and may have low precision (i.e., it's likely to hit an error path), symbolic fuzzing is partially "whitebox", in the sense that the flipped bytes allow the execution to adapt to the program structure and capture more execution paths.

Fuzzed Files
------------

Cloud9 implements symbolic fuzzing through `fuzzed files`, specified using the ``--fuzz-file <file-name>`` special argument.  A fuzzed file takes its concrete contents from the real host file indicated by `<file-name>`.  When data is read from the file, the execution forks for each fuzzed byte, such that in one path the original concrete byte is read, while in the other, the byte is marked as symbolic (i.e., a 1-byte pure symbolic variable).  For each execution path produced in this way, the total number of symbolic bytes in the file is called the `path entropy`.

To avoid the combinatorial explosion of fuzzed paths, Cloud9 limits the possible patterns of fuzzed bytes.  The fuzzed bytes are allowed only as contiguous regions in the file (called clusters), and one can limit both the total entropy, as well as the total number of clusters along each path.  For instance, for a file size `S`, a total entropy `E`, and one single contiguous region allowed, the total number of paths would be 1 + `S` + (`S` - 1) + .. + (`S` - `E` + 1) = O(`S` * `E`).  Note that each of these paths may in turn fork inside the program, due to its symbolic bytes.  The maximum entropy and number of fuzzing clusters are controlled using the ``-max-fuzzing-entropy`` and ``-max-fuzzing-clusters`` flags.

Internally, Cloud9 implements fuzzed files efficiently, in order to avoid expanding all fuzzed paths at once, and give flexibility to the search strategy.  For each fuzzed file, three separate buffers are maintained:  the original file contents buffer, a mask buffer, and the final file contents buffer.  The mask buffer indicates which bytes in the fuzzed file have been read.  When a read operation happens, the mask of each of the bytes read is checked.  If the mask is unset, then the mask becomes set, a state fork is performed, and the final contents buffer is filled in with the appropriate value (concrete or symbolic) in each path taken.  If the mask is found to be set, the final contents buffer is read directly instead.

Cloud9 offers a CUPA strategy (the ``-c9-job-entropy`` flag) that group states according to their entropy value, and thus give different entropy families the same chance of being selected.  

Current limitations:
 * There is no support for fuzzed command line arguments.
 * Fuzzed files are read-only.


Shadow-Driven Symbolic Input (Experimental)
===========================================
