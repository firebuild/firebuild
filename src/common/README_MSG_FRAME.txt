Firebuild Message Frame Format
==============================

Protocol
--------

The interceptor<->supervisor communication protocol is a mixture of 2
different types of messages:

 - Empty
 - FBBCOMM (FirebuildBuffers)

Empty messages aren't literally empty, their header contains an ack id.
They're only used in the supervisor->interceptor direction, for acking.

The transfer protocol is:

    ┌─────────────────────┐            ┐
    │ msg_size (uint32_t) │            │
    │ ack_id   (uint16_t) │            ├ msg_header
    │ fd_count (uint16_t) │            │
    ├─────────────────────┼╌╌╌╌╌┐    ┐ ┘
    ┆ FBBCOMM             ┆ anc ┆    ├ payload ("msg_size" bytes)
    └╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┴╌╌╌╌╌┘    ┘

A nonzero "ack_id" indicates that the interceptor wishes to receive a
response from the supervisor with the same ack_id. A value of 0 means
that no response is expected, or ack_id is otherwise irrelevant.

"msg_size" is the FBBCOMM payload's length, i.e. excluding the header
and the ancillary data.

An empty message ends here ("msg_size" is 0).

A FirebuildBuffers message continues with the serialized FBBCOMM
message. Refer to fbb/README_FBB.txt for further details.

A message might also have file descriptors attached as ancillary data;
see SCM_RIGHTS in cmsg(3) and unix(7). In that case the header and the
payload are sent as separate steps and the ancillary data is attached to
the payload (that is, to its first byte). The number of such file
descriptors is placed in the header as "fd_count".
