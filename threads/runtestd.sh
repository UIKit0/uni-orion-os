#!/bin/sh
pintos --gdb -v -k -T 480 --bochs  -- -q run $1
