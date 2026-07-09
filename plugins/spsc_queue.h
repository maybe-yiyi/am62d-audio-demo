#ifndef SPSC_H
#define SPSC_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Single-producer / single-consumer queue, implemented as a ring buffer.
 *
 * One slot is always kept empty to distinguish full from empty without
 * a separate counter.  Effective capacity is therefore (CAP - 1) items.
 *
 * Usage:
 *   SPSC_DEFINE(name, type, capacity)
 *
 * Expands to:
 *   name_queue_t — the queue struct
 *   name_try_push() — producer side, non-blocking, returns false when full
 *   name_try_pop() — consumer side, non-blocking, returns false when empty
 */

#define SPSC_DEFINE(NAME, T, CAP) \
typedef struct { \
	_Atomic uint32_t head; \
	_Atomic uint32_t tail; \
	T slots[CAP]; \
} NAME##_queue_t; \
\
static inline bool NAME##_try_push(NAME##_queue_t *q, const T *item) \
{ \
	uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed); \
	uint32_t next = (tail + 1u) % (CAP); \
	if (next == atomic_load_explicit(&q->head, memory_order_acquire)) \
		return false; \
	q->slots[tail] = *item; \
	atomic_store_explicit(&q->tail, next, memory_order_release); \
	return true; \
} \
\
static inline bool NAME##_try_pop(NAME##_queue_t *q, T *item) \
{ \
	uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed); \
	if (head == atomic_load_explicit(&q->tail, memory_order_acquire)) \
		return false; \
	*item = q->slots[head]; \
	atomic_store_explicit(&q->head, (head + 1u) % (CAP), memory_order_release); \
	return true; \
}

#endif /* SPSC_H */
