FireBuild Message Frame Format
==============================

Protocol
--------

The interceptor<->supervisor communication protocol is a mixture of 2
different types of messages:

 - Empty
 - FBB (FireBuildBuffers)

Empty messages aren't literally empty, their header contains an ack id.
They're only used in the supervisor->interceptor direction, for acking.

The transfer protocol is:

    ┌─────────────────────┐      ┐
    │ length   (uint32_t) │      │
    ├─────────────────────┤      ├ header
    │ ack_id   (uint32_t) │      │
    ├─────────────────────┤    ┐ ┘
    ┆ FBB                 ┆    ├ payload ("length" bytes)
    └╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘    ┘

"length" is the payload length, i.e. excluding "length" and "ack_id".

A nonzero "ack_id" indicates that the interceptor wishes to receive a
response from the supervisor with the same ack_id. A value of 0 means
that no response is expected, or ack_id is otherwise irrelevant.

An empty message ends here ("length" is 0).

A FireBuildBuffers message continues with the serialized FBB message.
Refer to README_FBB.txt for further details.


Implementation
--------------

FBB is implemented in this directory, in the generate_fbb script and the
tpl.* templates. Study them and the generated files for details.


Rationale
---------

We needed something that is extremely fast, and is async-signal-safe
(signal handlers might do stuff that the supervisor needs to know about,
so constructing and sending a message needs to be async-signal-safe,
e.g. it cannot use malloc).

The only non-mallocing solution we found was nanopb, but it was too slow
for our purposes.
