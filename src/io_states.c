#include "io_states.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#define BIT(x) (1UL << (x))

struct io_states_st
{
    uint32_t states_modified; /* Bitmask indicating which bits in desired_states have meaning. */
    uint32_t states; /* Bitmask of the desired states. */
}; 

io_states_st * io_states_create(void)
{
    io_states_st * const io_state_ctx = calloc(1, sizeof *io_state_ctx);

    return io_state_ctx;
}

void io_states_free(io_states_st * const io_state_ctx)
{
    free(io_state_ctx);
}

void 
io_states_set_state(
    io_states_st * const io_state_ctx,
    unsigned int index, 
    bool const state)
{
    io_state_ctx->states_modified |= BIT(index);
    if (state)
    {
        io_state_ctx->states |= BIT(index);
    }
    else
    {
        io_state_ctx->states &= ~BIT(index);
    }
}

uint32_t io_states_get_interesting_states_mask(
    io_states_st const * const io_state_ctx)
{
    return io_state_ctx->states_modified;
}

uint32_t io_states_get_states_mask(
    io_states_st const * const io_state_ctx)
{
    return io_state_ctx->states;
}

