/*
 * main.cpp - role dispatcher.
 *
 * A single firmware image; the active role is selected at build time by
 * CONFIG_LDSE_ROLE (menuconfig -> "LDSE forest-monitor configuration").
 *   0 = gateway (ESP32-WROOM-32)
 *   1 = relay   (ESP32-S3)
 *   2 = node    (ESP32-S3)
 */

#include "sdkconfig.h"

void ldse_gateway_main();
void ldse_relay_main();
void ldse_node_main();

extern "C" void app_main(void)
{
#if CONFIG_LDSE_ROLE == 0
    ldse_gateway_main();
#elif CONFIG_LDSE_ROLE == 1
    ldse_relay_main();
#else
    ldse_node_main();
#endif
}
