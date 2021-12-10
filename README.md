## Installation

For Ubuntu earlier than 19.10:

    sudo apt-add-repository ppa:rbalint/xxhash
    
For Ubuntu earlier than 21.04 (Valgrind earlier than 3.17.0):

    sudo apt-add-repository ppa:rbalint/valgrind

Install the build dependencies:

    sudo apt update
    sudo apt install cmake bats graphviz libconfig++-dev node-d3 libxxhash-dev libjemalloc-dev libfmt-dev libtsl-hopscotch-map-dev moreutils python3-jinja2 fakeroot

## Notes

 Firebuild breaks running make < 4.2 in parallel mode, thus it is recommended
 to run builds accelerated/analysed by firebuild with make >= 4.2.
