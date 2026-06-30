#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "../../plugins/spsc_queue.h"

typedef struct { int val; } item_t;
SPSC_DEFINE(item, item_t, 2)

int main(void)
{
    item_queue_t q = {0};
    item_t x;

    /* Empty queue: pop fails */
    assert(!item_try_pop(&q, &x));

    /* Push one item: succeeds */
    item_t a = {42};
    assert(item_try_push(&q, &a));

    /* Queue full (cap=2 holds 1 item): second push fails */
    item_t b = {99};
    assert(!item_try_push(&q, &b));

    /* Pop: gets the item */
    assert(item_try_pop(&q, &x));
    assert(x.val == 42);

    /* Empty again: pop fails */
    assert(!item_try_pop(&q, &x));

    /* Round-trip two items sequentially */
    assert(item_try_push(&q, &a));
    assert(item_try_pop(&q, &x));
    assert(x.val == 42);

    assert(item_try_push(&q, &b));
    assert(item_try_pop(&q, &x));
    assert(x.val == 99);

    printf("PASS: spsc_queue\n");
    return 0;
}
