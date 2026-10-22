// Read-only Wayland input-idle adapter. No surfaces, profile access, input capture or
// desktop configuration changes. stdin EOF is the service's joined shutdown signal.
#include <wayland-client.h>
#include "ext-idle-notify-v1-client-protocol.h"
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

struct Monitor {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    ext_idle_notifier_v1 *notifier = nullptr;
    ext_idle_notification_v1 *notification = nullptr, *bootstrap = nullptr;
    uint32_t seatName = 0, notifierName = 0;
    bool removed = false;
};
static void report(const char *state) { std::puts(state); std::fflush(stdout); }
static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    auto &m = *static_cast<Monitor *>(data);
    if (!m.seat && std::strcmp(interface, "wl_seat") == 0) {
        m.seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1)); m.seatName = name;
    } else if (!m.notifier && version >= 2 && std::strcmp(interface, "ext_idle_notifier_v1") == 0) {
        // v2's input-only notifications ignore idle inhibitors (video playback etc.).
        m.notifier = static_cast<ext_idle_notifier_v1 *>(wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 2)); m.notifierName = name;
    }
}
static void removed(void *data, wl_registry *, uint32_t name) {
    auto &m = *static_cast<Monitor *>(data);
    if (name == m.seatName || name == m.notifierName) m.removed = true;
}
static void idle(void *, ext_idle_notification_v1 *) { report("idle"); }
static void resumed(void *, ext_idle_notification_v1 *) { report("active"); }
static void bootstrapIdle(void *, ext_idle_notification_v1 *) {}
static void bootstrapResumed(void *data, ext_idle_notification_v1 *notification) {
    // One affirmative input event seeds the normal 30s notification, whose initial
    // non-idle state alone is not evidence of recent activity.
    report("active"); ext_idle_notification_v1_destroy(notification);
    static_cast<Monitor *>(data)->bootstrap = nullptr;
}

int main() {
    Monitor m;
    m.display = wl_display_connect(nullptr);
    if (!m.display) return 2;
    m.registry = wl_display_get_registry(m.display);
    const wl_registry_listener registryListener{global, removed};
    wl_registry_add_listener(m.registry, &registryListener, &m);
    int result = 2;
    if (wl_display_roundtrip(m.display) >= 0 && m.seat && m.notifier) {
        // Original threshold: 30 seconds. Bootstrap at zero timeout gets the next
        // real input promptly, without guessing from the initial protocol state.
        m.notification = ext_idle_notifier_v1_get_input_idle_notification(m.notifier, 30000, m.seat);
        const ext_idle_notification_v1_listener listener{idle, resumed};
        ext_idle_notification_v1_add_listener(m.notification, &listener, &m);
        m.bootstrap = ext_idle_notifier_v1_get_input_idle_notification(m.notifier, 0, m.seat);
        const ext_idle_notification_v1_listener bootstrapListener{bootstrapIdle, bootstrapResumed};
        ext_idle_notification_v1_add_listener(m.bootstrap, &bootstrapListener, &m);
        report("ready"); result = 0;
        while (!m.removed) {
            if (wl_display_dispatch_pending(m.display) < 0 || wl_display_flush(m.display) < 0) { result = 2; break; }
            pollfd fds[] = {{wl_display_get_fd(m.display), POLLIN, 0}, {0, POLLIN | POLLHUP, 0}};
            const int ready = poll(fds, 2, -1);
            if (ready < 0) { if (errno == EINTR) continue; result = 2; break; }
            if (fds[1].revents) break; // No commands accepted; EOF/input both end the helper.
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) { result = 2; break; }
            if ((fds[0].revents & POLLIN) && wl_display_dispatch(m.display) < 0) { result = 2; break; }
        }
    }
    if (m.bootstrap) ext_idle_notification_v1_destroy(m.bootstrap);
    if (m.notification) ext_idle_notification_v1_destroy(m.notification);
    if (m.notifier) ext_idle_notifier_v1_destroy(m.notifier);
    if (m.seat) wl_seat_destroy(m.seat);
    wl_registry_destroy(m.registry);
    wl_display_disconnect(m.display);
    return m.removed ? 2 : result;
}
