#!/bin/bash
gcc -o test_runner test/test_main.c -I/path/to/unity
./test_runner > test_output.txt
cat test_output.txt