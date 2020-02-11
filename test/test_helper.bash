#!/bin/bash
export LD_LIBRARY_PATH=$(realpath ../src/interceptor)
export PATH=$(realpath ../src/firebuild):$PATH
export FIREBUILD_CACHE_DIR=$(realpath ./test_cache_dir)

function strip_stderr () {
    awk '/^==[0-9]*== $/ {next} /FILE DESCRIPTORS: 0 open at exit/ {next} {print}' < $1
}
