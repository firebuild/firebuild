## Installation

For Ubuntu earlier than 21.04 (xxhash earlier than 0.8.0 or Valgrind earlier than 3.17.0):

    sudo apt-add-repository ppa:firebuild/build-deps
    
Install the build dependencies:

    sudo apt update
    sudo apt install cmake bats graphviz libconfig++-dev node-d3 libxxhash-dev libjemalloc-dev libtsl-hopscotch-map-dev moreutils python3-jinja2 fakeroot
