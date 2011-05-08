#!/bin/bash

llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ autobind_send.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ address_already_in_use.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ rebind_socket_error.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ reconnect_socket.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ recvfrom.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ ops_after_close.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ send_from_not_connected.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ send_recv.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ send_recv_the_same_socket.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ sendto.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ sendmsg_recvmsg.c
llvm-gcc --emit-llvm -c -g -I../../../../include/ -I../../../../runtime/POSIX/ select.c

rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false autobind_send.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false address_already_in_use.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false rebind_socket_error.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false reconnect_socket.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false recvfrom.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false send_from_not_connected.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false ops_after_close.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false send_recv.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false send_recv_the_same_socket.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false sendto.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false sendmsg_recvmsg.o
rm -rf klee
../../../../Release/bin/klee --posix-runtime --libc=uclibc -output-dir klee --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls --only-output-states-covering-new --max-instruction-time=3600. --max-memory-inhibit=false select.o
rm -rf klee



