#include "relay_states.h"
#include "daemonize.h"
#include "debug.h"
#include "ubus.h"
#include "ubus_server.h"
#include "state_handler.h"

#include <pifacedigital.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
 

static int hw_addr = 0;

static void set_state_handler(void * const user_info, relay_states_st * const desired_relay_states);
static uint32_t get_state_handler(void * const user_info, uint32_t const gpio_input_pins_to_read_bitmask); 


static state_handler_st const state_handlers =
{
    .set_state_handler = set_state_handler,
    .get_state_handler = get_state_handler
};

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

static void set_state_handler(void * const user_info, relay_states_st * const desired_relay_states)
{
    write_gpio_outputs(desired_relay_states);
}

static uint32_t
read_gpio_inputs(uint32_t const gpio_input_pins_to_read_bitmask)
{
    uint32_t const all_states = pifacedigital_read_reg(INPUT, hw_addr);
    uint32_t const interesting_states = all_states & gpio_input_pins_to_read_bitmask;

    return interesting_states;
}

static uint32_t get_state_handler(void * const user_info, uint32_t const gpio_input_pins_to_read_bitmask)
{
    return read_gpio_inputs(gpio_input_pins_to_read_bitmask);
}

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

    bool const ubus_server_initialised = 
        ubus_server_initialise(
            ubus_ctx, 
            &state_handlers,
            NULL);

    if (!ubus_server_initialised)
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

uloop_done:
    uloop_done();

    ubus_done();
    ubus_server_done(); 

    exit_code = EXIT_SUCCESS;

done:
    exit(exit_code);
}
