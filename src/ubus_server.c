#include "ubus_server.h"
#include "ubus_private.h"
#include "debug.h"
#include "relay_states.h"

#define BIT(x) (1UL << (x))

#include <libubox/blobmsg.h>

#include <string.h>
#include <stdio.h>

static char const gpio_object_name[] = "piface.gpio";
static char const gpio_object_type_name[] = "gpio";
static char const gpio_get_method_name[] = "get";
static char const gpio_set_method_name[] = "set";
static char const gpio_count_name[] = "count";
static char const gpio_io_type_bi[] = "bi";
static char const gpio_io_type_bo[] = "bo"; 

static char const pin_str[] = "pin";
static char const state_str[] = "state";
static char const result_str[] = "result";

static char const gpio_counts_str[] = "counts";
static char const gpio_count_str[] = "count";
static char const gpios_str[] = "gpios";
static char const results_str[] = "results";
static char const instance_str[] = "instance";
static char const value_str[] = "value";
static char const error_str[] = "error";
static char const gpio_io_type_str[] = "io type";
static char const gpio_io_type_binary_input[] = "binary-input"; 
static char const gpio_io_type_binary_output[] = "binary-output"; 
static char const invalid_instance_str[] = "Invalid instance";

struct ubus_context * ubus_ctx;

static state_handler_st const * handlers;
static void * user_info;

static int
gpio_set_multiple_handler(
    struct ubus_context * ctx,
    struct ubus_request_data * req,
    struct blob_attr * array_blob)
{
    int result;
    struct blob_buf b;
    relay_states_st * const relay_states = relay_states_create(); 

    if (relay_states == NULL)
    {
        result = UBUS_STATUS_UNKNOWN_ERROR;
        goto done;
    }

    if (blobmsg_check_array(array_blob, BLOBMSG_TYPE_TABLE) < 0)
    {
        /* The array needs to contain a set of tables, and it doesn't.*/
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    local_blob_buf_init(&b, 0);

    void * const a = blobmsg_open_array(&b, results_str);

    struct blob_attr * cur;
    int rem;

    blobmsg_for_each_attr(cur, array_blob, rem)
    {
        enum
        {
            GPIO_TYPE,
                GPIO_INSTANCE,
                GPIO_VALUE,
                __GPIO_MAX
        };
        static struct blobmsg_policy const gpio_policy[__GPIO_MAX] = {
            [GPIO_TYPE] = { .name = gpio_io_type_str, .type = BLOBMSG_TYPE_STRING },
            [GPIO_INSTANCE] = { .name = instance_str, .type = BLOBMSG_TYPE_INT32 },
            [GPIO_VALUE] = { .name = value_str, .type = BLOBMSG_TYPE_BOOL }
        };
        struct blob_attr * tb[__GPIO_MAX];

        blobmsg_parse(
            gpio_policy,
            ARRAY_SIZE(gpio_policy),
            tb,
            blobmsg_data(cur),
            blobmsg_data_len(cur));

        if (tb[GPIO_TYPE] == NULL
            || tb[GPIO_INSTANCE] == NULL
            || tb[GPIO_VALUE] == NULL)
        {
            result = UBUS_STATUS_INVALID_ARGUMENT;
            goto buf_free_done;
        }

        bool const io_type_is_binary_output =
            strcmp(blobmsg_get_string(tb[GPIO_TYPE]), gpio_io_type_binary_output) == 0;
        uint32_t const gpio_instance = blobmsg_get_u32(tb[GPIO_INSTANCE]);
        bool wrote_gpio;

        void * const table_out = blobmsg_open_table(&b, NULL);

        if (!io_type_is_binary_output)
        {
            blobmsg_add_string(&b, error_str, "Can't write this IO type");
            wrote_gpio = false;
        }
        else if (gpio_instance >= piface_num_outputs())
        {
            blobmsg_add_string(&b, error_str, invalid_instance_str);
            wrote_gpio = false;
        }
        else
        {
            bool const state = blobmsg_get_bool(tb[GPIO_VALUE]);

            relay_states_set_state(relay_states, gpio_instance, state);
            wrote_gpio = true;
        }

        /* TODO: It would be better to actually confirm that the GPIO 
         * has been written before indicating success. 
         */
        blobmsg_add_string(&b, gpio_io_type_str, blobmsg_get_string(tb[GPIO_TYPE]));
        blobmsg_add_u32(&b, instance_str, gpio_instance);
        blobmsg_add_u8(&b, result_str, wrote_gpio);

        blobmsg_close_table(&b, table_out);
    }

    blobmsg_close_array(&b, a);

    ubus_send_reply(ctx, req, b.head);

    if (handlers->set_state_handler != NULL)
    {
        handlers->set_state_handler(user_info, relay_states);
    }

    result = UBUS_STATUS_OK;

buf_free_done:
    blob_buf_free(&b);

done:
    relay_states_free(relay_states);

    return result;
}

enum
{
    GPIO_SET_PIN,
    GPIO_SET_STATE,
    GPIO_SET_GPIOS,
    __GPIO_SET_MAX
};

static struct blobmsg_policy const gpio_set_policy[__GPIO_SET_MAX] = {
    [GPIO_SET_PIN] = { .name = pin_str, .type = BLOBMSG_TYPE_INT32 },
    [GPIO_SET_STATE] = { .name = state_str, .type = BLOBMSG_TYPE_BOOL },
    [GPIO_SET_GPIOS] = { .name = gpios_str, .type = BLOBMSG_TYPE_ARRAY }
};

static int
gpio_set_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_SET_MAX];
    struct blob_buf b; 

    blobmsg_parse(gpio_set_policy,
                  ARRAY_SIZE(gpio_set_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_SET_GPIOS] != NULL)
    {
        result = gpio_set_multiple_handler(ctx, req, tb[GPIO_SET_GPIOS]);
        goto done;
    }

    if (tb[GPIO_SET_PIN] == NULL || tb[GPIO_SET_STATE] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    uint32_t const pin = blobmsg_get_u32(tb[GPIO_SET_PIN]);
    bool const state = blobmsg_get_bool(tb[GPIO_SET_STATE]);

    relay_states_st * const relay_states = relay_states_create();

    if (relay_states != NULL)
    {
        relay_states_set_state(relay_states, pin, state);

        if (handlers->set_state_handler != NULL)
        {
            handlers->set_state_handler(user_info, relay_states);
        }

        relay_states_free(relay_states);
    }

    bool const success = true;

    local_blob_buf_init(&b, 0);

    blobmsg_add_u8(&b, result_str, success);

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

enum
{
    GPIO_GET_PIN,
    GPIO_GET_GPIOS,
    __GPIO_GET_MAX
};

static int
gpio_get_multiple_handler(
    struct ubus_context * ctx,
    struct ubus_request_data * req,
    struct blob_attr * array_blob)
{
    int result;
    struct blob_buf b;
    uint32_t states;
    bool got_states;

    if (handlers->get_state_handler != NULL)
    {
        states = handlers->get_state_handler(user_info, 0xffffffff);
        got_states = true;
    }
    else
    {
        states = 0;
        got_states = false;
    }


    if (blobmsg_check_array(array_blob, BLOBMSG_TYPE_TABLE) < 0)
    {
        /* The array needs to contain a set of tables, and it doesn't.*/
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    local_blob_buf_init(&b, 0);

    void * const a = blobmsg_open_array(&b, results_str);

    struct blob_attr * cur;
    int rem;
    blobmsg_for_each_attr(cur, array_blob, rem)
    {
        enum
        {
            GPIO_TYPE,
                GPIO_INSTANCE,
                __GPIO_MAX
        };
        static struct blobmsg_policy const gpio_policy[__GPIO_MAX] = {
            [GPIO_TYPE] = { .name = gpio_io_type_str, .type = BLOBMSG_TYPE_STRING },
            [GPIO_INSTANCE] = { .name = instance_str, .type = BLOBMSG_TYPE_INT32 },
        };
        struct blob_attr * tb[__GPIO_MAX];

        blobmsg_parse(
            gpio_policy,
            ARRAY_SIZE(gpio_policy),
            tb,
            blobmsg_data(cur),
            blobmsg_data_len(cur));

        if (tb[GPIO_TYPE] == NULL
            || tb[GPIO_INSTANCE] == NULL)
        {
            result = UBUS_STATUS_INVALID_ARGUMENT;
            goto buf_free_done;
        }

        bool io_type_is_binary_input =
            strcmp(blobmsg_get_string(tb[GPIO_TYPE]), gpio_io_type_binary_input) == 0;
        uint32_t const instance = blobmsg_get_u32(tb[GPIO_INSTANCE]);
        bool read_gpio;
        void * table_out = blobmsg_open_table(&b, NULL);

        if (!io_type_is_binary_input)
        {
            blobmsg_add_string(&b, error_str, "Can't read this IO type");
        }
        else if (instance >= piface_num_inputs())
        {
            blobmsg_add_string(&b, error_str, invalid_instance_str);
        }
        else
        {
            read_gpio = got_states;
        }
        if (read_gpio)
        {
            blobmsg_add_u8(&b, value_str, (states & BIT(instance)) != 0);
        }
        blobmsg_add_string(&b, gpio_io_type_str, blobmsg_get_string(tb[GPIO_TYPE]));
        blobmsg_add_u32(&b, instance_str, instance);
        blobmsg_add_u8(&b, result_str, read_gpio);

        blobmsg_close_table(&b, table_out);
    }

    blobmsg_close_array(&b, a);

    ubus_send_reply(ctx, req, b.head);

    result = UBUS_STATUS_OK;

buf_free_done:
    blob_buf_free(&b);

done:
    return result;
}

static struct blobmsg_policy const gpio_get_policy[__GPIO_GET_MAX] = {
    [GPIO_GET_PIN] = { .name = pin_str, .type = BLOBMSG_TYPE_INT32 },
    [GPIO_GET_GPIOS] = { .name = gpios_str, .type = BLOBMSG_TYPE_ARRAY }
};

static int
gpio_get_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_GET_MAX];
    struct blob_buf b; 

    blobmsg_parse(gpio_get_policy,
                  ARRAY_SIZE(gpio_get_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_GET_GPIOS] != NULL)
    {
        result = gpio_get_multiple_handler(ctx, req, tb[GPIO_GET_GPIOS]);
        goto done;
    }

    if (tb[GPIO_GET_PIN] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    uint32_t const pin = blobmsg_get_u32(tb[GPIO_GET_PIN]);
    uint32_t const mask = BIT(pin);
    bool state;
    bool success; 

    if (handlers->get_state_handler != NULL)
    {
        state = handlers->get_state_handler(user_info, mask) & mask;
        success = true;
    }
    else
    {
        state = false;
        success = false;
    }

    local_blob_buf_init(&b, 0);

    blobmsg_add_u8(&b, result_str, success);
    if (success)
    {
        /* The state will read true if the input is open, and I want 
         * the input to read as active/ON when the input is low (i.e. 
         * switch ON/closed). 
         * Therefore, the state should be reversed. 
         */
        blobmsg_add_u8(&b, state_str, !state);
    }

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

enum
{
    GPIO_COUNT_TYPE,
    __GPIO_COUNT_MAX
};

static struct blobmsg_policy const gpio_count_policy[__GPIO_COUNT_MAX] = {
    [GPIO_COUNT_TYPE] = { .name = gpio_io_type_str, .type = BLOBMSG_TYPE_STRING }
};

static int
gpio_count_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_COUNT_MAX];
    struct blob_buf b;

    blobmsg_parse(gpio_count_policy,
                  ARRAY_SIZE(gpio_count_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_COUNT_TYPE] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    char const * const io_type = blobmsg_get_string(tb[GPIO_COUNT_TYPE]);
    int count;

    if (strcmp(io_type, gpio_io_type_bi) == 0)
    {
        count = piface_num_inputs();
    }
    else if (strcmp(io_type, gpio_io_type_bo) == 0)
    {
        count = piface_num_outputs();
    }
    else
    {
        count = 0;
    }

    local_blob_buf_init(&b, 0);

    blobmsg_add_u32(&b, io_type, count);

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

static int
gpio_counts_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    struct blob_buf b;

    local_blob_buf_init(&b, 0);

    void * const a = blobmsg_open_array(&b, gpio_counts_str);
    void * table_out;

    table_out = blobmsg_open_table(&b, NULL);

    blobmsg_add_string(&b, gpio_io_type_str, gpio_io_type_binary_input);
    blobmsg_add_u32(&b, gpio_count_str, piface_num_inputs());

    blobmsg_close_table(&b, table_out);

    table_out = blobmsg_open_table(&b, NULL);

    blobmsg_add_string(&b, gpio_io_type_str, gpio_io_type_binary_output);
    blobmsg_add_u32(&b, gpio_count_str, piface_num_outputs());

    blobmsg_close_table(&b, table_out);

    blobmsg_close_array(&b, a);

    ubus_send_reply(ctx, req, b.head);

    blob_buf_free(&b);

    return 0;
}

static bool
gpio_add_object(
    struct ubus_context * const ctx,
    struct ubus_object * const obj)
{
    int const ret = ubus_add_object(ctx, obj);

    if (ret != UBUS_STATUS_OK)
    {
        DPRINTF("Failed to publish object '%s': %s\n",
                obj->name,
                ubus_strerror(ret));
    }

    return ret == UBUS_STATUS_OK;
}

static struct ubus_method gpio_object_methods[] = {
    UBUS_METHOD(gpio_get_method_name, gpio_get_handler, gpio_get_policy),
    UBUS_METHOD(gpio_set_method_name, gpio_set_handler, gpio_set_policy),
    UBUS_METHOD(gpio_count_name, gpio_count_handler, gpio_count_policy),
    UBUS_METHOD_NOARG(gpio_counts_str, gpio_counts_handler)
};

static struct ubus_object_type gpio_object_type =
    UBUS_OBJECT_TYPE(gpio_object_type_name, gpio_object_methods);

static struct ubus_object gpio_object =
{
    .name = gpio_object_name,
    .type = &gpio_object_type,
    .methods = gpio_object_methods,
    .n_methods = ARRAY_SIZE(gpio_object_methods)
};

bool
ubus_server_initialise(
    struct ubus_context * const ctx,
    state_handler_st const * const handlers_in,
    void * const user_info_in)
{
    ubus_ctx = ctx;
    handlers = handlers_in;
    user_info = user_info_in;

    return gpio_add_object(ubus_ctx, &gpio_object);
}

void
ubus_server_done(void)
{
    ubus_ctx = NULL;
}


