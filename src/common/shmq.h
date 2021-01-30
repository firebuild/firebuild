/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef COMMON_FIREBUILD_SHMQ_H_
#define COMMON_FIREBUILD_SHMQ_H_

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHMQ_INITIAL_SIZE 4096

/* Round up a nonnegative number to the nearest multiple of 8. */
#ifndef roundup8
#define roundup8(x) ((x + 7) & ~0x07)
#endif

typedef struct {
  /* The offset of the oldest pointer, e.g. the address of p[2] in shmq.c's example. */
  /* Updated by the reader */
  volatile int32_t tail_location;
  /* Padding, so that if we continue with fields that are updated by the writer then an
   * 8-byte boundary separates it from the field updated by the reader. */
  int32_t padding;
} shmq_global_header_t;

typedef struct {
  int32_t len;
  int32_t ack_id;
} shmq_message_header_t;

typedef struct {
  volatile int32_t next_message_location;
} shmq_next_message_pointer_t;


static inline int shmq_global_header_size() {return roundup8(sizeof(shmq_global_header_t));}
static inline int shmq_message_header_size() {return roundup8(sizeof(shmq_message_header_t));}
static inline int shmq_next_message_pointer_size() {return roundup8(sizeof(shmq_next_message_pointer_t));}

/* The overall size occupied by a message's head, body, and the next message pointer. */
static inline int shmq_message_overall_size(int32_t len) {
  return shmq_message_header_size() + roundup8(len) + shmq_next_message_pointer_size();
}


typedef struct {
  size_t size;
  char *buf;
  bool tail_message_peeked;
} shmq_reader_t;


void shmq_reader_init(shmq_reader_t *reader, const char *name);
void shmq_reader_fini(shmq_reader_t *reader);
int32_t shmq_reader_peek_tail(shmq_reader_t *reader, const char **message_body_ptr);
void shmq_reader_discard_tail(shmq_reader_t *reader);


typedef struct {
  size_t size;
  char *buf;
  int fd;
  /* The layout's state, possible values are 1..4. */
  int state, next_state;
  /* The interval(s) actually used by the data stored in the queue. Exactly nr_chunks() are used,
   * starting at chunk[0] representing the stream's tail. */
  struct {
    int32_t tail;
    int32_t head;
  } chunk[3];
  int32_t next_message_location;
  int32_t next_message_len;
} shmq_writer_t;


void shmq_writer_init(shmq_writer_t *writer, const char *name);
void shmq_writer_fini(shmq_writer_t *writer);
char *shmq_writer_new_message(shmq_writer_t *writer, int32_t ack_id, int32_t len);
void shmq_writer_add_message(shmq_writer_t *writer);

static inline int shmq_writer_nr_chunks(const shmq_writer_t *writer) {
  static const int state_to_nr_chunks[5] = {0, 1, 2, 3, 2};
  return state_to_nr_chunks[writer->state];
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  // COMMON_FIREBUILD_SHMQ_H_
