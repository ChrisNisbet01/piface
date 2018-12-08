#include "relay_states.h"
#include "debug.h"
#include "ubus.h"

#include <pifacedigital.h>
#include <libubusgpio/ubus_gpio_server.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#define BIT(x) (1UL << (x))

static int hw_addr = 0;
static char const binary_input_str[] = "binary-input";
static char const binary_output_str[] = "binary-output"; 
static char const piface_ubus_name[] = "piface.gpio";

static size_t piface_num_inputs(void)
{
    return 8;
}

static size_t piface_num_outputs(void)
{
    return 8;
}

static uint32_t read_piface_register(
    uint8_t const reg)
{
    return pifacedigital_read_reg(reg, hw_addr);
}

static void write_piface_register(
    uint8_t const value, 
    uint8_t const reg)
{
    pifacedigital_write_reg(value, reg, hw_addr); 
}

static void
write_gpio_outputs( 
    uint32_t const gpio_to_write_bitmask,
    uint32_t const gpio_values)
{

    uint8_t states = read_piface_register(OUTPUT);
    /* Leave the pins we don't want to write as they are but clear 
     * the ones we do want to write. 
     */
    states &= ~gpio_to_write_bitmask;
    /* Set the bit for any pins we want to turn on. */
    states |= gpio_values;

    write_piface_register(states, OUTPUT);
}

static uint32_t
read_gpio_inputs(uint32_t const interesting_pins_bitmask)
{
    /* The state will read true if the input is open, and I want 
     * the input to read as active/ON when the input is low (i.e. 
     * switch ON/closed). 
     * Therefore, the state should be reversed. 
     */
    uint32_t const all_states = 
        ~read_piface_register(INPUT);
    uint32_t const interesting_states = 
        all_states & interesting_pins_bitmask;

    return interesting_states;
}

static uint32_t
read_gpio_outputs(uint32_t const interesting_pins_bitmask)
{
    uint32_t const all_states = 
        read_piface_register(OUTPUT);
    uint32_t const interesting_states = 
        all_states & interesting_pins_bitmask;

    return interesting_states;
}

typedef struct
{
    uint32_t input_states;
    uint32_t output_states;
} get_callback_ctx_st;

static void * get_start_callback(void * const callback_ctx)
{
    get_callback_ctx_st * const ctx = calloc(1, sizeof *ctx);

    if (ctx == NULL)
    {
        goto done;
    }

    ctx->input_states = read_gpio_inputs(0xffffffff);
    ctx->output_states = read_gpio_outputs(0xffffffff); 

done:
    return ctx;
}

static void get_end_callback(void * const callback_ctx)
{
    get_callback_ctx_st * const ctx = callback_ctx;

    free(ctx);
}

static bool read_state_from_ctx(
    get_callback_ctx_st const * const ctx,
    char const * const io_type,
    size_t const instance,
    bool * const state)
{
    bool read_state;

    if (strcmp(io_type, binary_input_str) == 0 && instance < piface_num_inputs())
    {
        uint32_t const bitmask = BIT(instance);

        *state = (ctx->input_states & bitmask) != 0;
        read_state = true;
    }
    else if (strcmp(io_type, binary_output_str) == 0 && instance < piface_num_outputs())
    {
        uint32_t const bitmask = BIT(instance);

        *state = (ctx->output_states & bitmask) != 0;
        read_state = true;
    }
    else
    {
        read_state = false;
    }

    return read_state;
}

static bool get_callback(
    void * const callback_ctx,
    char const * const io_type,
    size_t const instance,
    ubus_gpio_data_type_st * const value)
{
    get_callback_ctx_st const * const ctx = callback_ctx;
    bool read_io;
    bool state;

    if (!read_state_from_ctx(ctx, io_type, instance, &state))
    {
        read_io = false;
        goto done;
    }

    ubus_gpio_data_value_set_bool(value, state);

    read_io = true;

done:
    return read_io;
}

static void * set_start_callback(void * const callback_ctx)
{
    (void)callback_ctx;
    relay_states_st * const relay_states = relay_states_create();

    return relay_states;
}

static void set_end_callback(void * const callback_ctx)
{
    relay_states_st * const relay_states = callback_ctx;

    if (relay_states == NULL)
    {
        goto done;
    }

    uint32_t const gpio_to_write_mask = relay_states_get_gpio_to_write_mask(relay_states);
    uint32_t const gpio_values = relay_states_get_gpio_states_mask(relay_states); 

    write_gpio_outputs(gpio_to_write_mask, gpio_values);

done:
    relay_states_free(relay_states);
    return;
}

static bool set_callback(
    void * const callback_ctx,
    char const * const io_type,
    size_t const instance,
    ubus_gpio_data_type_st const * const value)
{
    relay_states_st * const relay_states = callback_ctx;
    bool wrote_io;
    bool const io_instance_is_writeable =
        strcmp(io_type, binary_output_str) == 0
        && instance < piface_num_outputs();

    if (!io_instance_is_writeable)
    {
        wrote_io = false;
        goto done;
    }

    if (relay_states == NULL)
    {
        wrote_io = false;
        goto done;
    }

    bool state;

    if (!ubus_gpio_data_value_get_bool(value, &state))
    {
        wrote_io = false;
        goto done;
    }

    relay_states_set_state(relay_states, instance, state);

    wrote_io = true;

done:
    return wrote_io;
}

static void count_callback(
    void * const callback_ctx,
    append_count_callback_fn const append_callback,
    void * const append_ctx)
{
    (void)callback_ctx;

    append_callback(append_ctx, binary_input_str, piface_num_inputs());
    append_callback(append_ctx, binary_output_str, piface_num_outputs());
}

static ubus_gpio_server_handlers_st const ubus_gpio_server_handlers =
{
    .count_callback = count_callback,
    .get =
    {
        .start_callback = get_start_callback,
        .get_callback = get_callback,
        .end_callback = get_end_callback
    },
    .set =
    {
        .start_callback = set_start_callback,
        .set_callback = set_callback,
        .end_callback = set_end_callback
    }
};

int run_ubus_server(int const piface_hw_address,
                    char const * const ubus_socket_name)
{
    int result;
    struct ubus_context * const ubus_ctx = ubus_initialise(ubus_socket_name);

    if (ubus_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS\n");
        result = -1;
        goto done;
    }

    hw_addr = piface_hw_address;

    ubus_gpio_server_ctx_st * const ubus_server_ctx =
        ubus_gpio_server_initialise(
        ubus_ctx,
        piface_ubus_name,
        &ubus_gpio_server_handlers,
        NULL); 

    if (ubus_server_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS server\n");
        result = -1;
        goto done;
    }

    uloop_run();
    uloop_done(); 

    ubus_gpio_server_done(ubus_ctx, ubus_server_ctx); 

    ubus_done();

    result = 0;

done:
    return result;
}
