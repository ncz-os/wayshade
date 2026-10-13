/* SPDX-License-Identifier: Apache-2.0 */
/*
 * no-idle-compositor - a minimal Wayland server for the idled exit-status test.
 *
 * It advertises a wl_seat (so ncz-screensaver-idled gets past its seat check)
 * but deliberately does NOT advertise ext-idle-notifier-v1, which is exactly
 * the permanent misconfiguration the daemon must report as EX_CONFIG (78) and
 * the unit must leave failed. It prints the socket name it listens on to
 * stdout and then serves clients until killed.
 *
 * usage: no-idle-compositor
 *
 * Build-only test fixture; never installed.
 */

#include <stdio.h>
#include <stdlib.h>

#include <wayland-server.h>

static void seat_get_pointer(struct wl_client *client, struct wl_resource *res, uint32_t id)
{
    (void)client; (void)res; (void)id;
}
static void seat_get_keyboard(struct wl_client *client, struct wl_resource *res, uint32_t id)
{
    (void)client; (void)res; (void)id;
}
static void seat_get_touch(struct wl_client *client, struct wl_resource *res, uint32_t id)
{
    (void)client; (void)res; (void)id;
}
static void seat_release(struct wl_client *client, struct wl_resource *res)
{
    (void)client; (void)res;
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release,
};

static void bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(client, &wl_seat_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &seat_impl, NULL, NULL);
}

int main(void)
{
    struct wl_display *display = wl_display_create();
    if (!display) {
        fputs("no-idle-compositor: wl_display_create failed\n", stderr);
        return 1;
    }
    /* wl_seat only; no ext_idle_notifier_v1 global on purpose. */
    if (!wl_global_create(display, &wl_seat_interface, 5, NULL, bind_seat)) {
        fputs("no-idle-compositor: wl_global_create(wl_seat) failed\n", stderr);
        wl_display_destroy(display);
        return 1;
    }
    const char *socket = wl_display_add_socket_auto(display);
    if (!socket) {
        fputs("no-idle-compositor: wl_display_add_socket_auto failed\n", stderr);
        wl_display_destroy(display);
        return 1;
    }
    printf("%s\n", socket);
    fflush(stdout);
    wl_display_run(display);
    wl_display_destroy(display);
    return 0;
}
