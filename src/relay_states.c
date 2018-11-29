#include "relay_states.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#define BIT(x) (1UL << (x))

struct relay_states_st
{
    uint32_t states_modified; /* Bitmask indicating which bits in desired_states have meaning. */
    uint32_t desired_states; /* Bitmask of the desired states. */
}; 

void relay_states_init(relay_states_st * const relay_states)
{
    if (relay_states == NULL)
    {
        goto done;
    }

    relay_states->states_modified = 0;
    relay_states->desired_states = 0;

done:
    return;
}

relay_states_st * relay_states_create(void)
{
    relay_states_st * const relay_states = calloc(1, sizeof *relay_states);

    if (relay_states == NULL)
    {
        goto done;
    }

    relay_states_init(relay_states);

done:
    return relay_states;
}

void relay_states_free(relay_states_st * const relay_states)
{
    free(relay_states);
}

void relay_states_set_state(relay_states_st * const relay_states, unsigned int relay_index, bool const state)
{
    relay_states->states_modified |= BIT(relay_index);
    if (state)
    {
        relay_states->desired_states |= BIT(relay_index);
    }
    else
    {
        relay_states->desired_states &= ~BIT(relay_index);
    }
}

uint32_t relay_states_get_gpio_to_write_mask(relay_states_st const * const relay_states)
{
    return relay_states->states_modified;
}

uint32_t relay_states_get_gpio_states_mask(relay_states_st const * const relay_states)
{
    return relay_states->desired_states;
}

