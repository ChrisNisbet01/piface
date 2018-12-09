#include "io_states.h"
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define GPIO_INTERRUPT_PIN 25
#define BIT(x) (1UL << (x))

typedef struct ubus_server_ctx_st
{
    struct uloop_fd gpio_interrupt_fd;
    struct ubus_context * ubus_ctx;
    ubus_gpio_server_ctx_st * ubus_gpio_server_ctx;
    int hw_addr;
    int gpio_pin_fd;
    int epoll_fd;
} ubus_server_ctx_st;

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

static void
write_gpio_outputs( 
    uint32_t const gpio_to_write_bitmask,
    uint32_t const gpio_values,
    int const hw_addr)
{

    uint8_t states = pifacedigital_read_reg(OUTPUT, hw_addr);
    /* Leave the pins we don't want to write as they are but clear 
     * the ones we do want to write. 
     */
    states &= ~gpio_to_write_bitmask;
    /* Set the bit for any pins we want to turn on. */
    states |= gpio_values;

    pifacedigital_write_reg(states, OUTPUT, hw_addr);
}

static uint32_t
read_gpio_inputs(
    uint32_t const interesting_pins_bitmask,
    int const hw_addr)
{
    /* The state will read true if the input is open, and I want 
     * the input to read as active/ON when the input is low (i.e. 
     * switch ON/closed). 
     * Therefore, the state should be reversed. 
     */
    uint32_t const all_states = 
        ~pifacedigital_read_reg(INPUT, hw_addr);
    uint32_t const interesting_states = 
        all_states & interesting_pins_bitmask;

    return interesting_states;
}

static uint32_t
read_gpio_outputs(
    uint32_t const interesting_pins_bitmask,
    int const hw_addr)
{
    uint32_t const all_states = 
        pifacedigital_read_reg(OUTPUT, hw_addr);
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
    ubus_server_ctx_st * const server_ctx = callback_ctx;
    get_callback_ctx_st * const ctx = calloc(1, sizeof *ctx);

    if (ctx == NULL)
    {
        goto done;
    }

    ctx->input_states = read_gpio_inputs(0xffffffff, server_ctx->hw_addr);
    ctx->output_states = read_gpio_outputs(0xffffffff, server_ctx->hw_addr);

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

    if (strcmp(io_type, binary_input_str) == 0 
        && instance < piface_num_inputs())
    {
        uint32_t const bitmask = BIT(instance);

        *state = (ctx->input_states & bitmask) != 0;
        read_state = true;
    }
    else if (strcmp(io_type, binary_output_str) == 0 
             && instance < piface_num_outputs())
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

typedef struct set_context_st
{
    ubus_server_ctx_st * server_ctx;
    io_states_st * io_states;
} set_context_st;

static void * set_start_callback(void * const callback_ctx)
{
    set_context_st * const set_ctx = calloc(1, sizeof *set_ctx);

    set_ctx->server_ctx = callback_ctx;
    set_ctx->io_states = io_states_create();

    return set_ctx;
}

static void set_end_callback(void * const callback_ctx)
{
    set_context_st * const set_ctx = callback_ctx;
    ubus_server_ctx_st * const server_ctx = set_ctx->server_ctx;
    io_states_st * const io_states = set_ctx->io_states;

    if (io_states == NULL)
    {
        goto done;
    }

    uint32_t const gpio_to_write_mask = io_states_get_interesting_states_mask(io_states);
    uint32_t const gpio_values = io_states_get_states_mask(io_states);

    write_gpio_outputs(gpio_to_write_mask, gpio_values, server_ctx->hw_addr);

done:
    io_states_free(io_states);
    free(set_ctx);

    return;
}

static bool set_callback(
    void * const callback_ctx,
    char const * const io_type,
    size_t const instance,
    ubus_gpio_data_type_st const * const value)
{
    set_context_st * const set_ctx = callback_ctx;
    io_states_st * const io_states = set_ctx->io_states;
    bool wrote_io;
    bool const io_instance_is_writeable =
        strcmp(io_type, binary_output_str) == 0
        && instance < piface_num_outputs();

    if (!io_instance_is_writeable)
    {
        wrote_io = false;
        goto done;
    }

    if (io_states == NULL)
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

    io_states_set_state(io_states, instance, state);

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

void
notify_input_state_change(
    ubus_server_ctx_st * const server_ctx,
    uint8_t const states)
{
    ubus_gpio_notify_message_ctx_st * const ctx = 
        ubus_notify_message_create();

    for (size_t i = 0; i < 8;  i++)
    {
        ubus_gpio_data_type_st value;

        ubus_gpio_data_value_set_bool(&value, (states & BIT(i)) != 0);
        ubus_notify_message_append_value(ctx, binary_input_str, i, &value);
    }

    ubus_notify_message_send(ctx, server_ctx->ubus_gpio_server_ctx);
}

static void handle_input_state_change(struct uloop_fd * u, unsigned int events)
{
    ubus_server_ctx_st * const server_ctx =
        container_of(u, ubus_server_ctx_st, gpio_interrupt_fd);
    (void)events;

    /* This handler is called when the epoll_fd file handle is ready to read, 
     * which means that it won't block here when epoll_wait is called. 
     */

    /* XXX - I'm not quite sure if  the interrupt register should be read first, 
     * or if epoll_wait() should be called first. 
     */

    /* Read the input register, thus clearing the interrupt. */
    uint8_t const states = pifacedigital_read_reg(INPUT, server_ctx->hw_addr);

    notify_input_state_change(server_ctx, states);

    /* Now call epoll_wait, which will stop this handler getting called until 
     * the inputs change state again. 
     */
    struct epoll_event mcp23s17_epoll_events;
    epoll_wait(server_ctx->epoll_fd, &mcp23s17_epoll_events, 1, -1);
}

static bool init_epoll(ubus_server_ctx_st * const server_ctx)
{
    bool result;
    int epoll_fd;
    int gpio_pin_fd;
    char gpio_pin_filename[33]; 

    /* Calculate the GPIO pin's path. */
    snprintf(gpio_pin_filename,
             sizeof(gpio_pin_filename),
             "/sys/class/gpio/gpio%d/value",
             GPIO_INTERRUPT_PIN);

    gpio_pin_fd = open(gpio_pin_filename, O_RDONLY | O_NONBLOCK);
    if (gpio_pin_fd <= 0)
    {
        result = false;
        goto done;
    }

    // if we haven't already, create the epoll and the GPIO pin fd's
    epoll_fd = epoll_create(1);
    if (epoll_fd <= 0)
    {
        close(gpio_pin_fd);
        result = false;
        goto done;
    }

    struct epoll_event epoll_ctl_events;
    epoll_ctl_events.events = EPOLLIN | EPOLLPRI | EPOLLET;
    epoll_ctl_events.data.fd = gpio_pin_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, gpio_pin_fd, &epoll_ctl_events) != 0)
    {
        close(gpio_pin_fd);
        close(epoll_fd);
        result = false;
        goto done;
    }

    server_ctx->gpio_pin_fd = gpio_pin_fd;
    server_ctx->epoll_fd = epoll_fd;

    struct uloop_fd * uloop_fd = &server_ctx->gpio_interrupt_fd;
    uloop_fd->fd = server_ctx->epoll_fd;
    uloop_fd_add(uloop_fd, ULOOP_READ);

    result = true;

done:
    return result;
}

static void listen_for_gpio_interrupts(
    ubus_server_ctx_st * const server_ctx)
{
    pifacedigital_enable_interrupts();
    if (!init_epoll(server_ctx))
    {
        pifacedigital_disable_interrupts();
    }
}

static void ubus_server_context_free(ubus_server_ctx_st * const server_ctx)
{
    if (server_ctx->epoll_fd >= 0)
    {
        close(server_ctx->epoll_fd);
    }
    if (server_ctx->gpio_pin_fd >= 0)
    {
        close(server_ctx->gpio_pin_fd);
    }
    ubus_gpio_server_done(server_ctx->ubus_gpio_server_ctx);
    free(server_ctx);
}

static ubus_server_ctx_st * ubus_server_context_alloc(
    struct ubus_context * const ubus_ctx,
    int hw_addr)
{
    ubus_server_ctx_st * server_ctx = calloc(1, sizeof *server_ctx);

    if (server_ctx == NULL)
    {
        goto done;
    }

    server_ctx->ubus_ctx = ubus_ctx;
    server_ctx->gpio_interrupt_fd.cb = handle_input_state_change;
    server_ctx->hw_addr = hw_addr;
    server_ctx->epoll_fd = -1;
    server_ctx->gpio_pin_fd = -1;
    server_ctx->ubus_gpio_server_ctx = 
        ubus_gpio_server_initialise(
            ubus_ctx,
            piface_ubus_name,
            &ubus_gpio_server_handlers,
            server_ctx);
    if (server_ctx->ubus_gpio_server_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS server\n");
        ubus_server_context_free(server_ctx);
        server_ctx = NULL;
        goto done;
    }

done:
    return server_ctx;
}

int run_ubus_server(int const piface_hw_address,
                    char const * const ubus_socket_name,
                    bool const send_state_change_notifications)
{
    int result;
    struct ubus_context * const ubus_ctx = ubus_initialise(ubus_socket_name);

    if (ubus_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS\n");
        result = -1;
        goto done;
    }

    ubus_server_ctx_st * const server_ctx =
        ubus_server_context_alloc(
            ubus_ctx, 
            piface_hw_address);
    if (server_ctx == NULL)
    {
        goto done;
    }

    if (send_state_change_notifications)
    {
        listen_for_gpio_interrupts(server_ctx);
    }

    uloop_run();
    uloop_done(); 

    ubus_server_context_free(server_ctx);

    ubus_done();

    result = 0;

done:
    return result;
}
