#ifndef __IO_STATES_H__
#define __IO_STATES_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct io_states_st io_states_st;

io_states_st * io_states_create(void);

void io_states_free(
    io_states_st * const io_state_ctx);

void 
io_states_set_state(
    io_states_st * const io_state_ctx,
    unsigned int index, 
    bool const state);

uint32_t io_states_get_interesting_states_mask(
    io_states_st const * const io_state_ctx);

uint32_t io_states_get_states_mask(
    io_states_st const * const io_state_ctx);

#endif /* __IO_STATES_H__ */
