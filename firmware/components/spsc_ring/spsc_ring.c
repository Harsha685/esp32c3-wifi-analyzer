#include "spsc_ring.h"

int spsc_ring_init(spsc_ring_t *r, void *storage, size_t slot_size, uint32_t slots)
{
    if (!r || !storage || slot_size == 0 || slots < 2 || (slots & (slots - 1)) != 0) {
        return -1;
    }
    r->storage = storage;
    r->slot_size = (uint32_t)slot_size;
    r->slots = slots;
    r->mask = slots - 1;
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    atomic_init(&r->dropped, 0);
    r->high_water = 0;
    return 0;
}

void *spsc_ring_acquire(spsc_ring_t *r)
{
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head - tail >= r->slots) {
        atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
        return NULL;
    }
    return r->storage + (size_t)(head & r->mask) * r->slot_size;
}

void spsc_ring_commit(spsc_ring_t *r)
{
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
}

const void *spsc_ring_peek(spsc_ring_t *r)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (head == tail) {
        return NULL;
    }
    uint32_t used = head - tail;
    if (used > r->high_water) {
        r->high_water = used;
    }
    return r->storage + (size_t)(tail & r->mask) * r->slot_size;
}

void spsc_ring_pop(spsc_ring_t *r)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
}

uint32_t spsc_ring_count(const spsc_ring_t *r)
{
    return atomic_load_explicit(&r->head, memory_order_acquire) -
           atomic_load_explicit(&r->tail, memory_order_acquire);
}
