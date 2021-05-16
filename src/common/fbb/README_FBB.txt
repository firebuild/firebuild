FireBuildBuffers (FBB) – User documentation
===========================================

FBB is a similar concept to many popular data serialization solutions,
such as Protocol Buffers, FlatBuffers etc.

It is tailored to Firebuild's needs, including these features:
 - high performance,
 - async-signal-safety (e.g. doesn't malloc) (*).

(*) The basic plain C API, including setters, getters, serializing etc.
does not perform malloc. However, the debugging methods, and the
convenience C++ API might malloc.


Namespaces
----------

Namespaces, or prefixes correspond to different worlds, e.g. we have an
"fbbcomm" namespace for messages used for interceptor-supervisor
communication, "fbbfp" for fingerprinting a process, "fbbstore" for data
stored in the cache, and "fbbtest" for unittesting.

There's no link between separate namespaces, no explicit support is
provided to encapsulate a message of one namespace inside a message of
another (although you can transfer it in a char array as a blob). They
are meant to be distinct worlds, in a way that an application might use
multiple of them at the same time without having to worry about name
conflicts.

In the rest of this document we look at a single example namespace
called "fbbns", often automatically capitalized to "FBBNS" by the code
generator software.


Basics
------

There are multiple object types, distinguished from each other using
the "tag" field.

For example, an FBBNS message with the tag "open" might represent the
opening of a file, consisting of a string (filename), an int32 (flags)
and another int32 (the returned fd). The "rename" tag might represent
the renaming of a file, consisting of two strings (two filenames). And
so on. Every FBB object, no matter if it's a builder or in serialized
format (see later) knows its own tag.

A generic FBB message means one that is of any of these tags, i.e. the
tag is only known at runtime. You can consider this as the union of all
the particular FBBNS_foo types. However, unlike with C unions, the
object knows its own tag at runtime, and the object is just as small as
required for this particular tag.

The top level object is of this generic (a.k.a. union) type, and so are
the embedded (nested) FBBNS members.

Members of a particular FBBNS object can be of these types:
  1. Scalar (fixed size):
      - bool
      - (unsigned) char, short, int, long, long long
      - (u)int{8,16,32,64}_t
      - uid_t, gid_t, mode_t, pid_t etc.
      - float, double
      - small custom structures, copied by value (you need to write a
        debugger method, see below)
      - etc.
  2. More complex object (dynamic size):
      - string (C-style '\0'-terminated)
      - FBBNS (embedded message of the same namespace, with no
        pre-determined tag)

Each of these fields can be:
  A. required (exactly one)
       Must be set before sending the message, otherwise it's an
       assertion failure in debug build, undefined behavior (might even
       crash) in prod build.
  B. optional (at most one)
       May or may not be set.
  C. array (zero, one or more items, decided at runtime)
       Note that there's no distinction between an unset and a
       zero-sized array. Defaults to zero-sized, no need to set if
       that's the desired value.

1A (required scalar):
  This is simply the scalar itself.

1B (optional scalar):
  This is the scalar, plus an additional accompanying boolean telling
  whether the value has been set.

1C (array of scalars):
  This is a pointer to such scalars, plus the length stored separately.

2A (required complex):
  This is equivalent to a non-NULL C pointer to the object.

2B (optional complex):
  This is equivalent to a C pointer, with the value of NULL denoting if
  the field is missing.

2C (array of complexes):
  This is equivalent to a pointer to a NULL-terminated array of pointers
  in C, e.g. "char **" or "void **".


In order to send a blob whose size isn't known at compile time, use a
char array.

[Rationale: I wanted to maintain consistent design in that sense that
for every data type we support, we support an array of them too. Unlike
with array of strings ("char**") or array of FBBs ("void**"), there's no
standard C practice to specify an array of blobs (pointers and lengths).
The currently available recursive FBB method is not any more complicated
in order to construct an array of char arrays (array of blobs) than any
custom API for this one-off case would have been. And for just a single
blob, a char array is already available. This is why there's no "native"
blob support.]

In order to send a fixed (and reasonably small) sized blob, or some
compound data of fixed size, a more convenient and faster way is to
declare (and typedef) a C struct that contains the necessary items, and
refer to this struct as a scalar. This way setting the blob copies the
value, rather than remembering the pointer only. Memory management
becomes simpler when you construct the message.


Message definition
------------------

Choose a namespace, in the examples we'll go with "fbbns".

Create a <namespace>.def file, i.e. "fbbns.def" with contents like this
(it's a Python dictionary):

    {
      "tags": [
        ("foo", [
          (OPTIONAL, "uint16_t", "myuint"),
          (OPTIONAL, STRING,     "mystring"),
          (ARRAY,    "uint16_t", "myuintarray"),
          (ARRAY,    STRING,     "mystringarray"),
          (ARRAY,    FBB,        "myfbbarray"),
        ]),
        ("bar", [
          (REQUIRED, "int",      "something"),
        ]),
      ]
    }

This defines message tags "foo" and "bar", and a couple of fields for
them.

For scalar data types the exact C data type should be spelled out
(inside quotes). It might consist of multiple words, e.g. "unsigned long
long". For strings and FBBs, use the uppercase STRING and FBB constants
(without quotes).

From the directory where fbbns.def resides, use the command

    path/to/generate_fbb fbbns outputdir

to generate the C (and bit of C++) source code into the given output
directory.


Two formats: builder vs. serialized
-----------------------------------

There are two ways an FBB can be represented in memory.

One is the read-write "builder". This is used while constructing a
message. It might contain raw C pointers (to strings, FBBs, arrays), and
in case of FBB members the structure is recursive, i.e. contains
pointers to other FBB structures within the same namespace.

The other is the read-only "serialized" format. It's a blob (a sequence
of bytes) that does not contain raw C pointers, can be passed from one
process to another, or stored in a file and later read back.

The builder can be serialized to the serialized format. There's no
deserialization step, getter methods work directly on the serialized
data.


Building a message
------------------

The specific type "FBBNS_Builder_foo" refers to an entire builder object
of tag "foo". Methods that work on a pre-determined tag take a pointer
to such objects.

The generic type "FBBNS_Builder" is more of a symbolic thing for cleaner
code (to write the readable "FBBNS_Builder*" instead of the meaningless
"void*"). Methods that work on any tag take such pointers. If needed,
you need to cast the pointers manually.


Let's suppose you want to construct a message of tag "foo". Get a
corresponding builder structure somewhere (practically on the stack if
you need async-signal-safety). You must initialize it too, this sets the
"tag" field to the desired value and zeroes out the rest.

    FBBNS_Builder_foo mybuilder;
    fbbns_builder_foo_init(&mybuilder);

Then you can set some values:

    fbbns_builder_foo_set_myuint(&mybuilder, 42);

Refer to the section "Setters and getters" for details on these.


IMPORTANT: The strings, FBBs, and arrays are not copied; however,
certain operations (e.g. length computation) might happen when they are
set. The caller owns these values, and is responsible for keeping them
unaltered in the memory until the builder object is no longer used.


Serializing a message
---------------------

First you need to measure how big the serialized format will be.

    size_t len = fbbns_builder_measure((FBBNS_Builder *) &mybuilder);

Then you need to allocate a sufficiently large buffer to serialize into.
Go with the stack or shared memory if you need async-signal-safety,
otherwise you might allocate on the heap too. Example:

    char *buf = alloca(len);

Now serialize the message into this area:

    fbbns_builder_serialize((FBBNS_Builder *) &mybuilder, buf);

Note that this method also performs integrity checks on the builder,
e.g. whether all required fields have indeed been set. Not setting a
required field is considered programming error, thus results in an
assertion failure in debug builds and undefined behavior in production
builds, rather than some soft error to handle.

Once serialized, you can throw away the builder object or any data
referenced by that, the serialized version will remain intact.

The serialized format does not know its own length, and running the
getters on a truncated (or otherwise corrupted) serialized data might
result in crash or incorrect behavior. Therefore the serialized format
on its own is not suitable to be transferred over a continous stream;
the data has to be prefixed with its own length in order to reconstruct
the message boundaries. You can allocate an accordingly larger buffer,
serialize to the proper offset, and then fill out the header. This is
outside of the scope of FBB.


Receiving and decoding a message
--------------------------------

You should use the symbolic type FBBNS_Serialized when pointing to a
serialized FBBNS message of a generic tag, and FBBNS_Serialized_foo when
pointing to a serialized FBBNS message of the particular tag "foo". Cast
between the pointer types if necessary.

Note, however, that the serialized message is most likely longer than
the area covered by the FBBNS_Serialized or FBBNS_Serialized_foo
structure. Don't copy these structures because getters will not work on
the copies.


Read the message into a contiguous area in the memory. Cast the pointer
to the generic type (FBBNS_Serialized) in order to check the tag, verify
that it's the value you expect, or branch on it if you allow multiple
values in the given context. Then cast to the specific type (e.g.
FBBNS_Serialized_foo) for the getters.

    FBBNS_Serialized *msg_generic = (FBBNS_Serialized *) buf;
    int tag = FBBNS_Serialized_get_tag(msg_generic);

    assert(tag == FBBNS_TAG_foo);

    FBBNS_Serialized_foo *msg = (FBBNS_Serialized_foo *) msg_generic;

Now you can use the getters to read the message.

    uint16_t myuint = fbbns_serialized_foo_get_myuint(&mybuilder);

Refer to the section "Setters and getters" for details on these.


IMPORTANT: The getter, if used on a string, an FBB, or an array of
anything, will return a pointer that points into the message data, but
beyond the FBBNS_Serialized_foo structure. Do not copy the
FBBNS_Serialized_foo structure and perform operations on the copy,
because in that case the following storage area isn't copied and the
pointer computations go wrong. Also, make sure the received message
resides in memory unaltered as long as any of these pointers (returned
by the getters) are in use.


Setters and getters
-------------------

Setters work on the builder only (obviously).

Getters are available both for the builder and the serialized format.
The ones for the serialized version are shown below, they always have an
identical counterpart for the builder version, with one exception as
noted below.

(Getters of the builder are useful e.g. when sorting an array of FBBs,
or for debugging.)

The examples use these variables:

    FBBNS_Builder_foo bldr;
    fbbns_builder_foo_init(&bldr);

    const FBBNS_Serialized_foo *msg;


### Required scalar

Nothing special. Setter:

    fbbns_builder_foo_set_myuint(&bldr, 42);

Getter:

    uint16_t val = fbbns_serialized_foo_get_myuint(msg);


### Optional scalar

Setter is the same as for required scalars. Note that once you set a
value, there's no way to unset:

    fbbns_builder_foo_set_myuint(&bldr, 42);

Check if a value has been set:

    bool myuint_was_set = fbbns_serialized_foo_has_myuint(msg);

Read the value. Must only be called after checking that the value has
been set. Running the getter on an unset field is considered programming
error and results in assertion failure.

    if (myuint_was_set) {
      uint16_t val = fbbns_serialized_foo_get_myuint(msg);
    }

However, you can use this convenience wrapper which returns the given
fallback value if the field was unset. Needless to say, it cannot tell
if the field was actually set to that fallback value.

    uint16_t val = fbbns_serialized_foo_get_myuint_with_fallback(msg, 100);


### Array of scalars

Setter: pass the array with the item count. Note that you cannot append
elements to an array one by one, you need to set the entire array in a
single step.

    const uint16_t myuintarray[] = { 25, 20, 23 };
    fbbns_builder_foo_set_myuintarray(&bldr, &myuintarray, 3);

Get the item count:

    size_t count = fbbns_serialized_foo_get_myuintarray_count(msg);

Get a particular item by value (the index must be a valid one):

    uint16_t val = fbbns_serialized_foo_get_myuintarray_at(msg, index);

Get a pointer to the entire array:

    uint16_t *arr = fbbns_serialized_foo_get_myuintarray(msg);

Get the entire array, C++ convenience API. Note that this allocates
memory (hence not async-signal-safe) and copies the data:

    std::vector<uint16_t> values = fbbns_serialized_foo_get_myuintarray_as_vector(msg);


### Required or optional string

Set (you can unset by setting to NULL, but why would you need it):

    fbbns_builder_foo_set_mystring(&bldr, "Hello world!");

Set with length. Note that the string must still be '\0'-terminated, the
passed value has to be its strlen(). This method is for speeding up
things if you already know the length, not for introducing blob support.

    fbbns_builder_foo_set_mystring_with_length(&bldr, "Hello world!", 12);

Set, convenience C++ wrapper (use this only if the string has no
embedded '\0'):

    std::string str("Hello world!");
    fbbns_builder_foo_set_mystring(str);

Getter. It's okay to call this on unset optional strings, it'll return
NULL:

    const char *str = fbbns_serialized_foo_get_mystring(msg);

Check if set (only for optional strings). Same as checking if the getters returns
NULL:

    if (fbbns_serialized_foo_has_mystring(msg)) { ... }

Get the string, C++ convenience API. Note that this allocates memory
(hence not async-signal-safe) and copies the data. Also, it's an
assertion failure to call it on an unset optional string.

    std::string str = fbbns_serialized_foo_get_mystring_as_string(msg);


### Required or optional FBBs

Essentially the same as required and optional strings. No setter variant
that would take the length, and no setter/getter C++ variants working
with std::string.

    FBBNS_Builder_bar inner_bldr;
    /* initialize and fill in inner_bldr's fields here */
    fbbns_builder_foo_set_myfbb(&bldr, (FBBNS_Builder *) &inner_bldr);

    const FBBNS_Serialized *inner_msg = fbbns_serialized_foo_get_myfbb(msg);


### Array of strings

Note that - as with arrays of scalars - you cannot append elements to an
array one by one, you need to set the entire array in a single step.

Setter - NULL-terminated array of pointers, in the usual C "char**" way:

    const char *loremipsum[] = { "lorem", "ipsum", NULL };
    fbbns_builder_foo_set_mystringarray(&bldr, loremipsum);

Setter - from an array of pointers, and their length. No need for a
trailing NULL, but all intermediate items must be non-NULL:

    const char *dolorsitamet[] = { "dolor", "sit", "amet" };
    fbbns_builder_foo_set_mystringarray(&bldr, dolorsitamet, 3);

Setter - convenience C++ wrapper. Note that it's just a thin wrapper
around the C setters. Note that there's no version taking the more
typical std::vector<std::string> because then the memory layout of
things is not what FBB expects; use the generic setter with the
"item_fn" callback function to hook up a vector of C++ strings.

    std::vector<const char *> c_string_array = ...;
    fbbns_builder_foo_set_mystringarray(&bldr, c_string_array);

Setter - generic version. Takes the length, and a getter "item_fn"
callback function that has to return the value for any valid index. The
callback won't be called with out-of-bounds index. This method can work
as an adaptor between FBB and any data structure the caller might have:

    const char *mystemfn(fbb_size_t index, void *user_data) {
      if (index == 0) return "this" else return "that";
    }

    fbbns_builder_foo_set_mystringarray_item_fn(&bldr, 2 /* item count */, myitemfn, myuserdata);

Getter - get the count:

    size_t count = fbbns_serialized_foo_get_mystringarray_count(msg);

Getter - get a particular item:

    const char *str = fbbns_serialized_foo_get_mystringarray_at(msg, index);

Getter - get a particular item's length. This method is only available
on the serialized format and not on the builder because the information
is readily available from the serialized format, whereas the builder
would need to run strlen().

    size_t len = fbbns_serialized_foo_get_mystringarray_len_at(msg, index);

Get the entire array, C++ convenience API. Note that this allocates
memory (hence not async-signal-safe) but does not copy the string_view backing data:

    std::vector<std::string_view> values = fbbns_serialized_foo_get_mystringarray_as_vector(msg);


### Array of FBBs

The same as the array of strings, except that the type is "const
FBBNS_Builder *" on the builder and "const FBBNS_Serialized *" on the
serialized format, instead of "const char *" or "std::string".


Debugging
---------

Debug a message from source code:

    FILE *f = ...;
    fbbns_builder_debug(f, (FBBNS_Builder *) &bldr);
    fbbns_serialized_debug(f, (FBBNS_Serialized *) msg);

Note that the debugger uses stdio, and hence is not async-signal-safe.
(Directly writing to the fd without buffering would be
async-signal-safe, but significantly slower.)

Debug from command line:

The fbbns_decode application pretty-prints the serialized FBB stored in
the given file, for easy debugging.

The debug format is valid JSON, and almost valid Python (the only thing
you need to do is set null=None before eval'ing it).


Custom scalars
--------------

Scalars can have types that are declared in some specific system-wide or
3rd-party header files (such as mode_t, pid_t, XXH128hash_t etc.). You
might need to include some header files to declare them.

Also you must write a debugger method for them, unless the type can be
automatically converted to a "long long int" and you're okay with
printing the value as such.

Next to the outmost "tags" key in the Python definition file, add the
keys "extra_h" for source code to place in the .h file and "extra_c" for
a snippet placed in the .c file, and "types_with_custom_debugger" being
a list of all the scalar types that need a custom debugger. These
debuggers should be implemented in "extra_c" or elsewhere.


Serialization format
--------------------

See README_FBB_INTERNAL.txt.


Compatibility
-------------

FBB is guaranteed to be consistent within one particular architecture
and one particular FBB version only.

If required, the caller needs to ensure that serialized formats created
by one build are not used from an incompatible one.

FBB should add some support to make this easier.

See #544.
