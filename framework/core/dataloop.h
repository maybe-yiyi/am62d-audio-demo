#ifndef DATALOOP_H
#define DATALOOP_H

#include <stdint.h>
#include <spa/support/plugin.h>

struct spa_dataloop {
    void *dl_handle;

    void *system_data;
    struct spa_handle *system_handle;
    void *system_iface;

    void *loop_data;
    struct spa_handle *loop_handle;
    struct spa_loop *loop;
    struct spa_loop_control *control;

    struct spa_support support[6];
    uint32_t n_support;

    pthread_t thread;
    int quit;
};

struct spa_dataloop *spa_dataloop_create(void);

void spa_dataloop_destroy(struct spa_dataloop *dl);

const struct spa_support *spa_dataloop_support(struct spa_dataloop *dl, uint32_t *n_support);

#endif /* DATALOOP_H */
