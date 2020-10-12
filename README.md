## Installation

For Ubuntu earlier than 19.10:

    sudo apt-add-repository ppa:rbalint/xxhash
    
This PPA hosts valgrind packages with fixes needed to make valgrind-check pass:

    sudo apt-add-repository ppa:rbalint/valgrind

Install the dependencies:

    sudo apt update
    sudo apt install cmake bats graphviz libconfig++-dev node-d3 libevent-dev libprotobuf-dev protobuf-compiler libxxhash-dev moreutils python3-jinja2

## Notes

 Firebuild breaks running make < 4.2 in parallel mode, thus it is recommended
 to run builds accelerated/analysed by firebuild with make >= 4.2.
