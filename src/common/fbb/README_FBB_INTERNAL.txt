FireBuildBuffers (FBB) – Internals
==================================

This document assumes that you're already familiar with README_FBB.txt.


Builder structures
------------------

The builder structure of any particular tag consists of two major parts:

 - The "wire" field contains bits that will be part of the serialized
   format as-is. This includes the values of scalars, the lengths of
   required or optional strings, and the item count of arrays.

 - Additional fields that won't be part of the serialized message. This
   includes the raw pointers (for strings, FBBs, and arrays of
   anything), or the item getter callback function (for arrays of
   strings or FBBs). This also includes the debug boolean to check that
   a required scalar has indeed been set.


Serialized structure
--------------------

The serialized structure of a particular tag is the same as the "wire"
field of the builder, providing direct access to certain values.

This is followed by other data in the memory, as discussed below.


Relptr
------

The concept of "relptr" (relative pointer) is similar to a C pointer,
but in a way that doesn't care about the actual memory location:

 - a positive integer denotes the byte offset where the said data begins
   in memory, relative to the beginning of the FBB object,

 - or 0 represents the NULL pointer.

In case of nested FBBs, relptr is relative to the address of the
innermost (directly encapsulating) FBB object. This way neither the
serialization methods nor the getters have to care whether an FBB is the
toplevel or a nested one.

The following table shows the number of pointer indirections necessary
for each of the available types.

                               | required / optional │   array   │
  ─────────────────────────────┼─────────────────────┼───────────┤
   scalars (bool, int etc.)    │          0          │     1     │
  ─────────────────────────────┼─────────────────────┼───────────┤
   complex types (string, FBB) │          1          │     2     │
  ─────────────────────────────┴─────────────────────┴───────────┘

In case of the Builder it's the number of times you have to follow a raw
C pointer, in case of the Serialized format it's the number of times you
have to follow a relptr to get to the actual data.


Serialized format
-----------------

The serialized format goes as follows:

First there is the structure representing the concrete message tag. In
all cases this begins with the tag itself, and then contains the
scalars, the string lengths, and the array item counts - these are the
common parts between the Builder and the Serialized structure, namely
the "wire" field of the Builder.

    ┌───────────────────────────────┐    ┐
    │ tag                     (int) │    │
    │ scalar                        │    │
    │ string length                 │    ├ FBBNS_Serialized_foo
    │ array item count              │    │
    │ has_* for optional scalar     │    │
    └───────────────────────────────┘    ┘

Then it is followed by another structure that contains the first-hop
relptrs. That is, relptrs that point directly to the data (where the
table above has the number 1), and the first hop of indirect pointers
(where the table has the number 2).

    ┌───────────────────────────────┐                    ┐
    │ scalar array relptr           │ ──┐                │
    │ string relptr                 │ ──│──┐             │
    │ FBBNS relptr                  │ ──│──│──┐          ├ FBBNS_Relptrs_foo
    │ string array first relptr     │ ──│──│──│──┐       │
    │ FBBNS array first relptr      │ ──│──│──│──│──┐    │
    └───────────────────────────────┘   ┆  ┆  ┆  ┆  ┆    ┘

This structure is omitted if it would be empty, because C and C++
disagree on the size of empty structs.

Then it is followed by the variable-length data. This includes the
scalar arrays, the '\0'-terminated strings, the serialized nested FBBs,
and arrays of the latter two.

For arrays of complex types there's an additional hop needed. For arrays
of strings the first relptr points to an alternating array of second
relptrs and string lengths. For arrays of FBBs the first relptr points
to the array of the second relptrs. The example shows one of each kind,
with the arrays containing 2 items each (arrows continued from the
previous figure):

    ┌───────────────────────────────┐   ┆  ┆  ┆  ┆  ┆
    │ scalar array                  │ <─┘  │  │  │  │
    │ string '\0'                   │ <────┘  │  │  │
    │ FBBNS serialized              │ <───────┘  │  │
    │ string_array[0] second relptr │ <─┬────────┘  │
    │ string_array[0] length        │   │           │
    │ string_array[1] second relptr │ ──│──┐        │
    │ string_array[1] length        │   │  │        │
    │ string_array[0] '\0'          │ <─┘  │        │
    │ string_array[1] '\0'          │ <────┘        │
    │ FBBNS_array[0] second relptr  │ <─┬───────────┘
    │ FBBNS_array[1] second relptr  │ ──│──┐
    │ FBBNS_array[0] serialized     │ <─┘  │
    │ FBBNS_array[1] serialized     │ <────┘
    └───────────────────────────────┘

Padding might be added at some places, they are not shown in the
pictures.
