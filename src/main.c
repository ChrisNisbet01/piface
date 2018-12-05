#include "relay_states.h"
#include "daemonize.h"
#include "debug.h"
#include "ubus.h"
#include "state_handler.h"

#include <pifacedigital.h>
#include <libubusgpio/libubusgpio.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
 
#define BIT(x) (1UL << (x))

static int hw_addr = 0;

size_t piface_num_inputs(void)
{
    return 8;
}

size_t piface_num_outputs(void)
{
    return 8;
}

static void 
write_gpio_outputs( 
    relay_states_st const * const relay_states)
{
    uint32_t const gpio_to_write_mask = relay_states_get_gpio_to_write_mask(relay_states);
    uint32_t const gpio_values = relay_states_get_gpio_states_mask(relay_states);

    uint8_t states = pifacedigital_read_reg(OUTPUT, hw_addr);
    /* Leave the pins we don't want to write as they are but clear 
     * the ones we do want to write. 
     */
    states &= ~gpio_to_write_mask;
    /* Then turn on any pins we want to turn on. */
    states |= gpio_values;

    pifacedigital_write_reg(states, OUTPUT, hw_addr);
}

static uint32_t
read_gpio_inputs(uint32_t const gpio_input_pins_to_read_bitmask)
{
    /* The state will read true if the input is open, and I want 
     * the input to read as active/ON when the input is low (i.e. 
     * switch ON/closed). 
     * Therefore, the state should be reversed. 
     */
    uint32_t const all_states = ~pifacedigital_read_reg(INPUT, hw_addr);
    uint32_t const interesting_states = all_states & gpio_input_pins_to_read_bitmask;

    return interesting_states;
}

typedef struct
{
    uint32_t all_states;
} get_callback_ctx_st;

static void * get_start_callback(void * const callback_ctx)
{
    get_callback_ctx_st * const ctx = calloc(1, sizeof *ctx);

    if (ctx == NULL)
    {
        goto done;
    }

    ctx->all_states = read_gpio_inputs(0xffffffff);

done:
    return ctx;
}

static void get_end_callback(void * const callback_ctx)
{
    get_callback_ctx_st * const ctx = callback_ctx;

    free(ctx);
}

static bool get_callback(
    void * const callback_ctx,
    char const * const io_type,
    size_t const instance,
    bool * const state)
{
    get_callback_ctx_st * const ctx = callback_ctx;
    bool read_io;

    if (strcmp(io_type, "binary-input") != 0)
    {
        read_io = false;
        goto done;
    }

    if (instance >= piface_num_inputs())
    {
        read_io = false;
        goto done;
    }

    uint32_t const bitmask = BIT(instance);
    *state = (ctx->all_states & bitmask) != 0;

    read_io = true;

done:
    return read_io;
}

static void * set_start_callback(void * const callback_ctx)
{
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

    write_gpio_outputs(relay_states);
    relay_states_free(relay_states);

done:
    return;
}

static bool set_callback(
    void * const callback_ctx,
    char const * const io_type,
    size_t const instance,
    bool const state)
{
    relay_states_st * const relay_states = callback_ctx;
    bool wrote_io;

    if (strcmp(io_type, "binary-output") != 0)
    {
        wrote_io = false;
        goto done;
    }

    if (instance >= piface_num_outputs())
    {
        wrote_io = false;
        goto done;
    }

    if (relay_states == NULL)
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
    void * const callback,
    append_count_callback_fn const append_callback,
    void * const append_ctx)
{
    append_callback(append_ctx, "binary-input", piface_num_inputs());
    append_callback(append_ctx, "binary-output", piface_num_outputs());
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

static void usage(char const * const program_name)
{
    fprintf(stdout, "Usage: %s [options]\n", program_name);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -d %-21s %s\n", "", "Run as a daemon");
    fprintf(stdout, "  -s %-21s %s\n", "ubus socket", "Ubus socket path");
    fprintf(stdout, "  -h %-21s %s\n", "", "PiFace SPI address");
}

int main(int argc, char * * argv)
{
    bool daemonise = false;
    int daemonise_result;
    int exit_code;
    int option;
    char const * listening_socket_name = NULL;

    while ((option = getopt(argc, argv, "h:s:?d")) != -1)
    {
        switch (option)
        {
            case 'd':
                daemonise = true;
                break;
            case 's':
                listening_socket_name = argv[optind];
                break;
            case 'h':
                hw_addr = atoi(argv[optind]);
                break;
            case '?':
                usage(basename(argv[0]));
                exit_code = EXIT_SUCCESS;
                goto done;
        }
    }

    if (daemonise)
    {
        daemonise_result = daemonize(NULL, NULL, NULL);
        if (daemonise_result < 0)
        {
            fprintf(stderr, "Failed to daemonise. Exiting\n");
            exit_code = EXIT_FAILURE;
            goto done;
        }
        if (daemonise_result == 0)
        {
            /* This is the parent process, which can exit now. */
            exit_code = EXIT_SUCCESS;
            goto done;
        }
    }

    struct ubus_context * const ubus_ctx = ubus_initialise(listening_socket_name);

    if (ubus_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    ubus_gpio_server_ctx_st * ubus_server_ctx =
        ubus_gpio_server_initialise(
        ubus_ctx,
        "piface.gpio",
        &ubus_gpio_server_handlers,
        NULL); 

    if (ubus_server_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS server\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    if (pifacedigital_open(hw_addr) < 0)
    {
        goto uloop_done;
    }

    uloop_run();

    pifacedigital_close(hw_addr);

    ubus_gpio_server_done(ubus_ctx, ubus_server_ctx); 

uloop_done:
    uloop_done();

    ubus_done();

    exit_code = EXIT_SUCCESS;

done:
    exit(exit_code);
}
