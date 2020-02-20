## Installation

For Ubuntu earlier than 19.10:

    sudo apt-add-repository ppa:rbalint/xxhash
    
This PPA hosts valgrind packages with fixes needed to make valgrind-check pass:

    sudo apt-add-repository ppa:rbalint/valgrind

Install the dependencies:

    sudo apt update
    sudo apt install bats graphviz libconfig++-dev libjs-d3 libprotobuf-dev protobuf-compiler libxxhash-dev moreutils python3-jinja2

Get [d3 version 5](https://github.com/d3/d3/releases),
extract `d3.min.js` from the zip, rename and place it as
`data/d3.v5.min.js`.
