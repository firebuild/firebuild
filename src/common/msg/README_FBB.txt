FBB – FireBuildBuffers
======================

FBB supports simple messages, containing these field types:
 - scalar (booleans and integers of various sizes);
 - C-style ('\0'-terminated) string;
 - C-style (NULL-terminated) array of strings.

The message is always exactly one of the predefined types (i.e. kind of
a union on the outmost level). Other than this, no nesting, embedding,
unions, repetition etc. are supported.

FBB is super simple, flexible, and extremely fast for these simple
cases.

FBB is designed to be async-signal-safe.


The transfer protocol
---------------------

    ┌─────────────────────┐            ┐
    │ length   (uint32_t) │            │
    ├─────────────────────┤            ├ header
    │ ack_id   (uint32_t) │            │
    ├─────────────────────┤    ┐       ┘       ┐
    │ tag           (int) │    │               │
    │ ints                │    │               │
    │ bools               │    │               │
    │ has_* for optionals │    ├ FBB_foobar    │
    │ string sizes        │    │               │
    │ stringarray sizes   │    │               ├ payload
    │ ...                 │    │               │
    ├─────────────────────┤    ┘               │
    │ string1 '\0'        │                    │
    │ string2 '\0'        │                    │
    │ ...                 │                    │
    └─────────────────────┘                    ┘

For the header, namely "length" and "ack_id", refer to
README_MSG_FRAME.txt.

This is followed by the corresponding FBB_foobar (e.g. FBB_open,
FBB_fchmod etc.) structure, which contains:
 - the tag;
 - the scalar values themselves;
 - for optional scalars: a boolean has_ counterpart to denote if it's
   set;
 - for strings: their size;
 - for string arrays: their overall size.

"tag" is an auto-generated tag number (platform dependent integer) to
identify different FBB messages, such as FBB_TAG_open, FBB_TAG_fchmod
etc. This is at the very beginning of FBB, so after receiving a message,
the buffer can be cast to the proper kind of structure based on this
value.

The string's size includes the trailing '\0', i.e. one larger than the
length. For missing (NULL) strings the size is 0 (there's no separate
"has_" counterpart).

The string array's overall size is the sum of the size (including '\0')
for each contained string.

String arrays must be declared as "optional", reflecting that you don't
have to set them via the API. An unset (a.k.a. NULL) string array is the
same as an empty one (non-NULL pointing to a NULL pointer).

FBB_foobar is followed by the '\0'-terminated strings themselves (if
any). Missing optional strings are skipped. For string arrays, each
string is laid out, one after the other. That is, each string, including
the missing ones too, and each string array, occupies exactly as many
bytes as mentioned in its corresponding "size" field.


Constructing and sending a message
----------------------------------

Get a FBB_Builder_foobar structure somewhere (practically on the stack
if you wish to remain async-signal-safe). Initialize using
FBB_foobar_init() which sets the "tag" to the desired value and zeroes
out the rest.

Use FBB_foobar_set_fieldname() to set a particular field. Once set,
optional scalars cannot be unset. Strings and string arrays can be unset
by setting them to NULL.

Note that the strings are not copied; however, certain operation (e.g.
length computation) might happen when the string is set. The caller owns
the strings and string arrays, and is responsible for keeping them
unaltered in the memory until the message is sent.

Use FBB_send() to send the entire message over a file descriptor.

Do not use the getters while constructing a message, they are not
designed to work, and the string ones would surely crash. They are
designed to work on received messages only. You should know what you set
these values to. This is because the entire packet, as it traverses the
wire, is never constructed in a contiguous memory area, instead it's
glued together from multiple locations using writev(). For the same
reason, there is no method to serialize the message into memory.

FBB_Builder_foobar actually embeds a FBB_foobar, plus additional
necessary bookkeeping, including the string and string array pointers.
It also keeps track of which required scalars have been set, so that
FBB_foobar_send() can perform integrity checking before sending the
data. This integrity checking is only performed if compile-time
debugging (FBB_DEBUG) is enabled. Not setting a required field is
considered programming error, thus results in an assertion failure,
rather than some soft error to handle.


Receiving and parsing message
-----------------------------

Read the header. Then read the payload into a contiguous area in the
memory.

Based on the "tag", which is a platform-dependent "int" at the very
beginning of the payload, cast the pointer to the corresponding
FBB_foobar.

Use FBB_foobar_has_fieldname() to check if an optional field was set.

Use FBB_foobar_get_fieldname() to get the value of a field. For unset
strings it returns NULL. For unset scalars it bails out with an
assertion failure.

For optional scalars you can also use the convenience method
FBB_foobar_get_fieldname_with_fallback() which returns the given value
if the field is unset.

The getter, if used on a string, will return a pointer that points into
the payload, but beyond the FBB_foobar structure.

String array currently only has a C++ getter, as this field type is only
used in the interceptor->supervisor direction, and it was easier to
implement this way. To be fixed on demand.

Do not copy the FBB_foobar structure and perform operations on the copy,
because in that case the following strings aren't copied and the string
pointer computation goes wrong.

Use FBB_debug() to print a human-readable representation to stderr.
(Note that debugging currently only works on the received message, not
on the one you're building and sending. That one is to be implemented,
if needed.)
