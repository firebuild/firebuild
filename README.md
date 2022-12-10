## Installation

For Ubuntu earlier than 21.04 (xxhash earlier than 0.8.0 or Valgrind earlier than 3.17.0):

    sudo apt-add-repository ppa:firebuild/build-deps
    
Install the build dependencies:

    sudo apt update
    sudo apt install cmake bats graphviz libconfig++-dev node-d3 libxxhash-dev libjemalloc-dev libtsl-hopscotch-map-dev moreutils python3-jinja2 fakeroot

## Notes

 Firebuild breaks running make < 4.2 in parallel mode, thus it is recommended
 to run builds accelerated/analysed by firebuild with make >= 4.2.
