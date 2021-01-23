/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Shared memory based message queue
 *
 * shmq is a one-way message queue between exactly one writer and exactly one reader. The writer and
 * reader might be in separate processes. The writer "object" needs to be constructed first.
 *
 * There's no upper limit for the queue's size, it grows automatically on demand.
 *
 * A message is an arbitrary (possibly empty) blob, along with its length available.
 *
 * Primitives for the writer:
 * - allocate room for the next message of a given size (which then can be constructed in place),
 * - grow the area for the message under construction [to be implemented if needed],
 * - add this message to the queue's tail.
 * (Starting a new message and adding it to the queue have to be made in alternating order).
 *
 * Primitives for the reader:
 * - check if there's a message in the queue, and access the one at the queue's head if any,
 * - pop (discard) the message from the queue's head.
 *
 * Notifying the reader that there's new message available has to happen via means outside of shmq,
 * e.g. using a semaphore.
 */

/*
 * Main design of the buffer layout:
 *
 *   Initial state:     Example state later on:
 *    ┌──┐               ┌────────────────────────────────────────────┐
 *    │  ↓               │                                            ↓
 *   ┌──┬────┬╌╌╌┐      ┌──┬╌╌╌┬───────────┬────┬───────────┬────┬╌╌╌┬────┬╌╌╌┬───────────┬────┬╌╌╌┐
 *   │GH│p[0]│...│      │GH│...│h[3]╎msg[3]╎p[3]│h[4]╎msg[4]╎p[4]│...│p[2]│...│h[5]╎msg[5]╎p[5]│...│
 *   └──┴────┴╌╌╌┘      └──┴╌╌╌┴───────────┴────┴───────────┴────┴╌╌╌┴────┴╌╌╌┴───────────┴────┴╌╌╌┘
 *       │                      ↑           │    ↑           │        │        ↑           │
 *       ↓                      │           └────┘           └────────│────────┘           ↓
 *      -1                      └─────────────────────────────────────┘                   -1
 *
 * "GH"     = global header
 * "h[N]"   = Nth message header
 * "msg[N]" = Nth message blob
 * "p[N]"   = pointer (actually offset within the buffer) to the (N+1)st message header, or -1
 * "..."    = optional unused area
 *
 * For a given N, the fields h[N], msg[N] and p[N] are always in a single contiguous memory region.
 *
 * The second picture represents one possible buffer layout after the writer placed messages
 * msg[1..5] (inclusive) in the queue, and the reader has consumed msg[1..2], thus the queue
 * contains msg[3..5].
 *
 * The buffer begins with a global header.
 *
 * This global header contains a pointer to the location where the pointer to the reader's next
 * message header (i.e. the tail of the queue) is already available (if the queue is non-empty), or
 * will be placed (if the queue is empty). This pointer in the global header is the only piece of
 * data that is updated by the queue's reader, to signal to the writer which memory segments can be
 * reused.
 *
 * When the writer adds message N, it first finds a large enough continuous area, growing the shared
 * memory if necessary (the exact allocation details are documented below at the writer), then
 * builds the header h[N] and the message msg[N] here in place, sets p[N] to -1, and finally updates
 * p[N-1] to point to this new message.
 *
 * The reader can tell if the buffer is nonempty based on whether at the offset pointed to from the
 * global header contains -1 or not, in the latter case that points to the next message to proceed.
 * Based on the offsets, it might need to mremap() a larger area. After handling message N, it
 * updates the pointer in the global header to point to p[N], effectively freeing up p[N-1], h[N]
 * and msg[N].
 *
 * Note that there's always one more pointer in the queue buffer (excluding the global header) than
 * message, the pointers at both ends of the queue are valid (e.g. p[0] in the initial state where
 * there's no message, p[2..5] in the second example where there are 3 messages).
 *
 * Also note that the "pointers" are actually always "offsets" to the buffer.
 *
 * Everything is aligned to a multiple of 8 bytes, the paddings are omitted from the pictures.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  /* for mremap() */
#endif

#include "common/shmq.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>



void shmq_reader_init(shmq_reader_t *reader, const char *name) {
  assert(name[0] == '/');

  int fd = shm_open(name, O_RDWR | O_EXCL, 0);
  assert(fd != -1);

  reader->size = SHMQ_INITIAL_SIZE;
  reader->buf = mmap(NULL, reader->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  /* As opposed to shmq_writer, shmq_reader can now close the file descriptor, it won't need it. */
  close(fd);

  shm_unlink(name);

  reader->tail_message_peeked = false;
}

void shmq_reader_fini(shmq_reader_t *reader) {
  munmap(reader->buf, reader->size);
  reader->buf = NULL;
}


/**
 * Get the next message, i.e. the message at the tail of the queue. Leaves the message in the queue.
 *
 * Returns the message's length, and stores the pointer to the beginning of the message blob.
 * This memory region containing the blob is valid until discard_tail() is called.
 *
 * Returns -1 if the queue is empty.
 */
int32_t shmq_reader_peek_tail(shmq_reader_t *reader, const char **msg_ptr) {
  /* The location of p[2] according to the example. */
  int32_t tail_location = ((shmq_global_header_t *)(reader->buf))->tail_location;
  assert(tail_location % 8 == 0);

  /* Where p[2] points to, i.e. the location of h[3] according to the example. */
  int32_t header_location = *(int32_t *)(reader->buf + tail_location);
  if (header_location < 0) {
    return -1;  /* Empty queue. */
  }
  assert(header_location >= shmq_global_header_size());
  assert(header_location % 8 == 0);
  reader->tail_message_peeked = true;

  /* Maybe the area containing the message header (h[3] in the example) isn't mapped yet. */
  size_t required_size = header_location + shmq_message_header_size();
  if (reader->size < required_size) {
    size_t old_size = reader->size;
    do { reader->size *= 2; } while (reader->size < required_size);
    reader->buf = mremap(reader->buf, old_size, reader->size, MREMAP_MAYMOVE);
  }

  /* The message length. */
  int32_t len = ((shmq_message_header_t *)(reader->buf + header_location))->len;

  /* Maybe the message (msg[3] in the example) or the following pointer (p[3]) isn't mapped yet. */
  required_size = shmq_reader_message_end_location(reader, header_location) + shmq_next_message_pointer_size();
  if (reader->size < required_size) {
    size_t old_size = reader->size;
    do { reader->size *= 2; } while (reader->size < required_size);
    reader->buf = mremap(reader->buf, old_size, reader->size, MREMAP_MAYMOVE);
  }

  /* Store the raw pointer to the message blob, return the length. */
  *msg_ptr = reader->buf + header_location + shmq_message_header_size();
  return len;
}


/**
 * Discard the message at the queue's tail.
 *
 * Must have been preceded by a peek_tail() call for this message, otherwise the required memory
 * area might not be mapped yet, and we don't want to bother with mremap()'ing here. You wouldn't
 * want to discard a message you haven't seen, would you?
 */
void shmq_reader_discard_tail(shmq_reader_t *reader) {
  assert(reader->tail_message_peeked);
  reader->tail_message_peeked = false;

  /* The location of p[2] according to the example. */
  int32_t tail_location = ((shmq_global_header_t *)(reader->buf))->tail_location;
  assert(tail_location % 8 == 0);

  /* Where p[2] points to, i.e. the location of h[3] according to the example. */
  int32_t header_location = *(int32_t *)(reader->buf + tail_location);
  assert(header_location >= shmq_global_header_size());
  assert(header_location % 8 == 0);

  /* Update the tail in the global header to point to p[3] according to the example.
   * This value will be seen by the Writer so that it knows it can reuse this memory segment of
   * p[2], h[3] and msg[3]. */
  ((shmq_global_header_t *)(reader->buf))->tail_location = shmq_reader_message_end_location(reader, header_location);
}


/*
 * shmq_writer implements the following simple "memory management" to allocate room for the next
 * message in the queue:
 *
 * The data is stored in at most 3 contiguous memory chunks. These are the 4 possible states, and
 * their possible transitions:
 *
 *          ┌──┬╌╌╌┬──────────┬╌╌╌┐
 * state 1: │GH│...│ chunk[0] │...│
 *          └──┴╌╌╌┴──────────┴╌╌╌┘
 *          The data is in 1 contiguous chunk.
 *
 *          Adding a new message: If there's room for the new message in front of this area then
 *          place it right after GH, starting a new chunk[1] in front of the old one, entering state
 *          2. Otherwise append it to chunk[0] and remain in state 1.
 *
 *          Discarding old data keeps us in state 1.
 *
 *          ┌──┬─────────────────┬╌╌╌┬─────────────────┬╌╌╌┐
 * state 2: │GH│ chunk[1] (head) │...│ chunk[0] (tail) │...│
 *          └──┴─────────────────┴╌╌╌┴─────────────────┴╌╌╌┘
 *          The data is stored in 2 contiguous chunks, the newer chunk[1] preceding the older
 *          chunk[0] in the memory.
 *
 *          Adding a new message: If there's enough room between these two chunks then append it to
 *          the end of chunk[1], staying in state 2. Otherwise start a new chunk[2], immediately
 *          where chunk[0] ends in the memory, entering state 3.
 *
 *          Discarding old data from the tail might take us back to state 1 (chunk[0] is gone,
 *          chunk[1] is relabeled as chunk[0]).
 *
 *          ┌──┬───────────────────┬╌╌╌┬─────────────────┬─────────────────┬╌╌╌┐
 * state 3: │GH│ chunk[1] (middle) │...│ chunk[0] (tail) │ chunk[2] (head) │...│
 *          └──┴───────────────────┴╌╌╌┴─────────────────┴─────────────────┴╌╌╌┘
 *          The data is stored in 3 contiguous chunks, the oldest chunk[0] being in the middle in
 *          the memory, the middle chunk[1] residing at the beginning of the memory, and the newest
 *          chunk[2] being the last in the memory.
 *
 *          Adding a new message: Append it to chunk[2], staying in state 3.
 *
 *          Discarding old data might take us to state 4 (chunk[0] is gone, the remaininig ones are
 *          relabeled to the one lower index), and then in turn to state 1 (shifting the indices
 *          again).
 *
 *          ┌──┬╌╌╌┬─────────────────┬╌╌╌┬─────────────────┬╌╌╌┐
 * state 4: │GH│...│ chunk[0] (tail) │...│ chunk[1] (head) │...│
 *          └──┴╌╌╌┴─────────────────┴╌╌╌┴─────────────────┴╌╌╌┘
 *          The data is stored in 2 contiguous chunks, the first one preceding the second.
 *
 *          Adding a new message: Append it to chunk[1], staying in state 4.
 *
 *          Discarding old data might take us back to state 1 (chunk[0] is gone, chunk[1] is
 *          relabeled as chunk[0]).
 *
 * The code doing this "memory management" doesn't care about which bytes are allocated for message
 * headers vs. message bodies vs. pointers, nor about which bytes were allocated in a single step.
 * That is, it's almost as if it handled a continuous byte stream.
 *
 * However, a message (including its preceding header and following pointer) placed in one step is
 * guaranteed to receive a contiguous area.
 *
 * Moreover, since the code links the new message to the previous one's pointer, it relies on the
 * reader not discarding that pointer's area from the queue.
 *
 * Currently the message size has to known in advance, but if there's demand, allowing to grow it
 * wouldn't be hard to implement using an occasional memmove() if necessary.
 */

void shmq_writer_init(shmq_writer_t *writer, const char *name) {
  assert(name[0] == '/');
  writer->fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
  assert(writer->fd != -1);

  writer->size = SHMQ_INITIAL_SIZE;
  ftruncate(writer->fd, writer->size);  /* Grow the backing virtual file. */
  writer->buf = mmap(NULL, writer->size, PROT_READ | PROT_WRITE, MAP_SHARED, writer->fd, 0);

  ((shmq_next_message_pointer_t *)(writer->buf + shmq_global_header_size()))->next_message_location = -1;
  ((shmq_global_header_t *)(writer->buf))->tail_location = shmq_global_header_size();

  writer->state = 1;
  writer->chunk[0].tail = shmq_global_header_size();
  writer->chunk[0].head = shmq_global_header_size() + shmq_next_message_pointer_size();

  writer->previous_head_location = -1;
  writer->new_header_location = -1;
}

void shmq_writer_fini(shmq_writer_t *writer) {
  munmap(writer->buf, writer->size);
  writer->buf = NULL;

  /* As opposed to shmq_reader, shmq_writer needs to keep the fd opened to grow the underlying shm
   * file (ftruncate()) when needed. Close it when we're done. */
  close(writer->fd);

  // FIXME maybe shm_unlink() too, just in case the reader didn't appear?
}

/**
 * Free up the area already consumed by the reader.
 */
static void shmq_writer_advance_tail(shmq_writer_t *writer) {
  int32_t tail = ((shmq_global_header_t *)(writer->buf))->tail_location;

  while (tail < writer->chunk[0].tail || tail >= writer->chunk[0].head) {
    /* If the stream's tail is outside of the tail chunk chunk[0] then drop the entire chunk[0] and
     * shift down the remaining ones, at most twice in the loop, according to state transitions 2->1
     * and 3->4->1. */
    writer->chunk[0] = writer->chunk[1];
    writer->chunk[1] = writer->chunk[2];
    static const int old_to_new_state[5] = {0, 0, 1, 4, 1};
    writer->state = old_to_new_state[writer->state];
    assert(writer->state);
  }
  /* Maybe advance the tail within the tail chunk. */
  writer->chunk[0].tail = tail;
}

/**
 * Find a place for a message of len bytes, which then can be constructed here in place by the caller.
 */
char *shmq_writer_new_message(shmq_writer_t *writer, int32_t len) {
  /* This assertion and assignment guarantee that new_message and add_message() are called
   * alternatingly. */
  assert(writer->previous_head_location == -1);
  writer->previous_head_location = writer->chunk[shmq_writer_nr_chunks(writer) - 1].head;

  shmq_writer_advance_tail(writer);

  /* Now we've potentially "freed up" some area. Time to find a contiguous slot for the forthcoming
   * message and its accompanying technical data. */
  int32_t needed_bytes = shmq_message_header_size() + roundup8(len) + shmq_next_message_pointer_size();

  /* See if a state change is necessary, create a new empty chunk in that case. */
  if (writer->state == 1 && needed_bytes <= writer->chunk[0].tail - shmq_global_header_size()) {
    writer->chunk[1].head = writer->chunk[1].tail = shmq_global_header_size();
    writer->state = 2;
  } else if (writer->state == 2 && needed_bytes > writer->chunk[0].tail - writer->chunk[1].head) {
    writer->chunk[2].head = writer->chunk[2].tail = writer->chunk[0].head;
    writer->state = 3;
  }

  /* Append to the last (possibly just created) chunk. */
  writer->new_header_location = writer->chunk[shmq_writer_nr_chunks(writer) - 1].head;
  writer->chunk[shmq_writer_nr_chunks(writer) - 1].head += needed_bytes;

  /* We've figured out where the new message will go. Now make sure the shm is big enough. */
  size_t required_size = writer->new_header_location + needed_bytes;
  if (writer->size < required_size) {
    size_t old_size = writer->size;
    do {
      writer->size *= 2;
    } while (writer->size < required_size);
    ftruncate(writer->fd, writer->size);  /* Grow the backing virtual file. */
    writer->buf = mremap(writer->buf, old_size, writer->size, MREMAP_MAYMOVE);
  }

  /* Initiaize the new message's header and the pointer to the next (future) message. */
  ((shmq_message_header_t *)(writer->buf + writer->new_header_location))->len = len;
  ((shmq_next_message_pointer_t *)(writer->buf + shmq_writer_message_end_location(writer, writer->new_header_location)))->next_message_location = -1;

  /* Return the location of the message body, to be filled in by the caller. */
  return writer->buf + writer->new_header_location + shmq_message_header_size();
}

/**
 * Add the constructed message to the queue.
 */
void shmq_writer_add_message(shmq_writer_t *writer) {
  /* This assertion, and the assignment below guarantee that new_message and add_message() are
   * called alternatingly. */
  assert(writer->previous_head_location >= shmq_global_header_size() + shmq_next_message_pointer_size());

  /* Just flip one address to add the new message to the linked list, that's all we need to do. */
  ((shmq_next_message_pointer_t *)(writer->buf + writer->previous_head_location - shmq_next_message_pointer_size()))->next_message_location =
      writer->new_header_location;

  writer->previous_head_location = -1;
}
