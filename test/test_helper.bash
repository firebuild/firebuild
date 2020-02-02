#!/bin/bash
export LD_LIBRARY_PATH=$(realpath ../src/interceptor)
export PATH=$(realpath ../src/firebuild):$PATH
export FIREBUILD_CACHE_DIR=$(realpath ./test_cache_dir)
