#include "daemonize.h"
#include "debug.h"
#include "ubus_server.h"

#include <pifacedigital.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

static void usage(char const * const program_name)
{
    fprintf(stdout, "Usage: %s [options]\n", program_name);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -d %-21s %s\n", "", "Run as a daemon");
    fprintf(stdout, "  -s %-21s %s\n", "ubus socket", "Ubus socket path");
    fprintf(stdout, "  -h %-21s %s\n", "", "PiFace SPI address");
    fprintf(stdout, "  -d %-21s %s\n", "", "Send state change notifications");
}

int main(int argc, char * * argv)
{
    bool daemonise = false;
    int daemonise_result;
    int exit_code;
    int option;
    char const * ubus_socket_name = NULL;
    int hw_addr = 0;
    bool send_state_change_notifications = false;

    while ((option = getopt(argc, argv, "h:s:?dn")) != -1)
    {
        switch (option)
        {
            case 'd':
                daemonise = true;
                break;
            case 's':
                ubus_socket_name = argv[optind];
                break;
            case 'h':
                hw_addr = atoi(argv[optind]);
                break;
            case 'n':
                send_state_change_notifications = true;
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

    if (pifacedigital_open(hw_addr) < 0)
    {
        fprintf(stderr, "Failed to open connection to piface module\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    if (run_ubus_server(hw_addr, 
                        ubus_socket_name,
                        send_state_change_notifications) < 0)
    {
        fprintf(stderr, "Error running UBUS server\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    exit_code = EXIT_SUCCESS;

done:
    pifacedigital_close(hw_addr);

    exit(exit_code);
}
