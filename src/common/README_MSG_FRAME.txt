FireBuild Message Frame Format
==============================

Protocol
--------

The interceptor<->supervisor communication protocol is a mixture of 2
different types of messages:

 - Empty
 - FBBCOMM (FireBuildBuffers)

Empty messages aren't literally empty, their header contains an ack id.
They're only used in the supervisor->interceptor direction, for acking.

The transfer protocol is:

    ┌─────────────────────┐      ┐
    │ msg_size (uint32_t) │      │
    ├─────────────────────┤      ├ msg_header
    │ ack_id   (uint32_t) │      │
    ├─────────────────────┤    ┐ ┘
    ┆ FBBCOMM             ┆    ├ payload ("msg_size" bytes)
    └╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘    ┘

A nonzero "ack_id" indicates that the interceptor wishes to receive a
response from the supervisor with the same ack_id. A value of 0 means
that no response is expected, or ack_id is otherwise irrelevant.

"msg_size" is the payload length, i.e. excluding "ack_id" and "msg_size".

An empty message ends here ("msg_size" is 0).

A FireBuildBuffers message continues with the serialized FBBCOMM
message. Refer to fbb/README_FBB.txt for further details.
