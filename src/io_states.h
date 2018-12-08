#ifndef __RELAY_STATES_H__
#define __RELAY_STATES_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct relay_states_st relay_states_st;

void relay_states_init(relay_states_st * const relay_states);
relay_states_st * relay_states_create(void);
void relay_states_free(relay_states_st * const relay_states);

void 
relay_states_set_state(
    relay_states_st * const relay_states, 
    unsigned int relay_index, 
    bool const state);

uint32_t relay_states_get_gpio_to_write_mask(relay_states_st const * const relay_states);

uint32_t relay_states_get_gpio_states_mask(relay_states_st const * const relay_states);

#endif /* __RELAY_STATES_H__ */
