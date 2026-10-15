#pragma once
/*
 * Lock-free single-producer / single-consumer ring of fixed-size slots.
 *
 * Zero-copy: the producer acquires a slot, fills it in place, then commits.
 * The consumer peeks at the oldest slot and pops it when done.
 * When full, acquire() fails and bumps the drop counter (the producer never blocks).
 *
 * Exactly one producer context and one consumer context may use a ring.
 */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *storage;
    uint32_t slot_size;
    uint32_t slots;           /* power of two */
    uint32_t mask;
    _Atomic uint32_t head;    /* next slot to write (producer-owned) */
    _Atomic uint32_t tail;    /* next slot to read  (consumer-owned) */
    _Atomic uint32_t dropped; /* producer failed to acquire */
    uint32_t high_water;      /* consumer-owned: max occupancy observed */
} spsc_ring_t;

/* slots must be a power of two; storage must hold slots * slot_size bytes. */
int spsc_ring_init(spsc_ring_t *r, void *storage, size_t slot_size, uint32_t slots);

/* Producer side */
void *spsc_ring_acquire(spsc_ring_t *r);  /* NULL if full (counts a drop) */
void spsc_ring_commit(spsc_ring_t *r);

/* Consumer side */
const void *spsc_ring_peek(spsc_ring_t *r); /* NULL if empty */
void spsc_ring_pop(spsc_ring_t *r);

/* Either side (approximate if called from the other thread) */
uint32_t spsc_ring_count(const spsc_ring_t *r);
