#!/bin/bash

find ./ -name '*.cpp' -o -name '*.c' -o -name '*.h' | \
		xargs grep --color=auto -n -i $@
