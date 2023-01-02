# Firebuild

<img alt="Firebuild logo" src="data/firebuild-logo.svg" width=15%>

## Introduction

Firebuild is an automatic build accelerator. It works by caching the outputs of executed commands
and replaying the results when the same commands are executed with the same parameters within the
same environment.

The commands can be compilation or other build artifact generation steps, tests, or any command that
produces predictable output. The commands to cache and replay from the cache are determined
automatically based on `firebuild`'s [configuration](etc/firebuild.conf) and each command's and its
children's observed behavior.

<img align="center" alt="Firebuild accelerates cc and ld with LTO" src="doc/parallel-make-acceleration.svg">

## Usage

Prefix your build command with `firebuild`:

    firebuild <build command>

The first build is typically a 5-10% slower due to the overhead of analyzing the build and populating
the cache. Successive builds can be 5-20 times or even faster depending on the project and the changes
between the builds.

## Installation

Binaries for supported Ubuntu releases can be downloaded from the [official PPA](https://launchpad.net/~firebuild/+archive/ubuntu/stable):

    sudo add-apt-repository ppa:firebuild/stable
    sudo apt install firebuild

If you would like to use `firebuild` in your GitHub pipeline there is a [GitHub Action](https://github.com/marketplace/actions/firebuild-for-github-actions) to do just that.


## Building from source

For Ubuntu earlier than 21.04 (xxhash earlier than 0.8.0 or Valgrind earlier than 3.17.0):

    sudo apt-add-repository ppa:firebuild/build-deps
    
Install the build dependencies:

    sudo apt update
    sudo apt install cmake bats graphviz libconfig++-dev node-d3 libxxhash-dev libjemalloc-dev libtsl-hopscotch-map-dev moreutils python3-jinja2 fakeroot

Build:

    cmake . && make
