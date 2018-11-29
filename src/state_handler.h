#ifndef __STATE_HANDLER_H__
#define __STATE_HANDLER_H__

#include "relay_states.h"

#include <stdint.h>

typedef void (* set_state_handler_fn)(void * const user_info, relay_states_st * const desired_relay_states);
typedef uint32_t (* get_state_handler_fn)(void * const user_info, uint32_t const gpio_input_pins_to_read_bitmask); 


typedef struct state_handler_st
{
    set_state_handler_fn set_state_handler;
    get_state_handler_fn get_state_handler;
} state_handler_st;

#endif /* __STATE_HANDLER_H__ */
