#ifndef __UBUS_SERVER_H__
#define __UBUS_SERVER_H__

#include <stdbool.h>

int run_ubus_server(int const piface_hw_address,
                    char const * const ubus_socket_name,
                    bool const send_state_change_notifications);

#endif /* __UBUS_SERVER_H__ */
