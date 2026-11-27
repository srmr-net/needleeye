#include "idleinhibitor.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>

#include <wayland-client.h>

#include <cstring>

namespace {

// Minimal Wayland client bindings for idle-inhibit-unstable-v1 (version 1).
// Matches /usr/share/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml.
constexpr uint32_t kManagerDestroyOpcode = 0;
constexpr uint32_t kManagerCreateInhibitorOpcode = 1;
constexpr uint32_t kInhibitorDestroyOpcode = 0;

extern const wl_interface kIdleInhibitorInterface;

const wl_interface *kIdleInhibitTypes[] = {
    &kIdleInhibitorInterface,
    &wl_surface_interface,
};

const wl_message kIdleInhibitManagerRequests[] = {
    { "destroy", "", kIdleInhibitTypes + 0 },
    { "create_inhibitor", "no", kIdleInhibitTypes + 0 },
};

const wl_interface kIdleInhibitManagerInterface = {
    "zwp_idle_inhibit_manager_v1", 1,
    2, kIdleInhibitManagerRequests,
    0, nullptr,
};

const wl_message kIdleInhibitorRequests[] = {
    { "destroy", "", kIdleInhibitTypes + 0 },
};

const wl_interface kIdleInhibitorInterface = {
    "zwp_idle_inhibitor_v1", 1,
    1, kIdleInhibitorRequests,
    0, nullptr,
};

zwp_idle_inhibitor_v1 *createWaylandInhibitor(zwp_idle_inhibit_manager_v1 *manager,
                                              wl_surface *surface) {
    auto *proxy = wl_proxy_marshal_flags(
        reinterpret_cast<wl_proxy *>(manager),
        kManagerCreateInhibitorOpcode,
        &kIdleInhibitorInterface,
        wl_proxy_get_version(reinterpret_cast<wl_proxy *>(manager)),
        0,
        nullptr,
        surface);
    return reinterpret_cast<zwp_idle_inhibitor_v1 *>(proxy);
}

void destroyWaylandInhibitor(zwp_idle_inhibitor_v1 *inhibitor) {
    wl_proxy_marshal_flags(
        reinterpret_cast<wl_proxy *>(inhibitor),
        kInhibitorDestroyOpcode,
        nullptr,
        wl_proxy_get_version(reinterpret_cast<wl_proxy *>(inhibitor)),
        WL_MARSHAL_FLAG_DESTROY);
}

void destroyWaylandManager(zwp_idle_inhibit_manager_v1 *manager) {
    wl_proxy_marshal_flags(
        reinterpret_cast<wl_proxy *>(manager),
        kManagerDestroyOpcode,
        nullptr,
        wl_proxy_get_version(reinterpret_cast<wl_proxy *>(manager)),
        WL_MARSHAL_FLAG_DESTROY);
}

struct RegistryBindState {
    zwp_idle_inhibit_manager_v1 *manager = nullptr;
};

void handleRegistryGlobal(void *data,
                          wl_registry *registry,
                          uint32_t name,
                          const char *interface,
                          uint32_t /*version*/) {
    auto *state = static_cast<RegistryBindState *>(data);
    if (!state->manager && std::strcmp(interface, kIdleInhibitManagerInterface.name) == 0) {
        state->manager = static_cast<zwp_idle_inhibit_manager_v1 *>(
            wl_registry_bind(registry, name, &kIdleInhibitManagerInterface, 1));
    }
}

void handleRegistryGlobalRemove(void * /*data*/, wl_registry * /*registry*/, uint32_t /*name*/) {
}

const wl_registry_listener kRegistryListener = {
    handleRegistryGlobal,
    handleRegistryGlobalRemove,
};

} // namespace

IdleInhibitor::IdleInhibitor(QWindow *window, int activityTimeoutMs, QObject *parent)
    : QObject(parent),
      m_window(window),
      m_activityTimeoutMs(qMax(100, activityTimeoutMs)) {
    m_activityTimer.setSingleShot(true);
    connect(&m_activityTimer, &QTimer::timeout, this, [this]() {
        if (!m_spaceMouseActive)
            return;
        const bool wasActive = active();
        m_spaceMouseActive = false;
        syncWaylandInhibitor();
        if (wasActive != active())
            emit activeChanged();
    });

    initWayland();

    if (m_window) {
        connect(m_window, &QWindow::visibilityChanged, this, [this]() {
            attachWindowSignals();
            updateFullscreenFromWindow();
            syncWaylandInhibitor();
        });
        connect(m_window, &QWindow::windowStateChanged, this, [this]() {
            updateFullscreenFromWindow();
        });
        connect(m_window, &QObject::destroyed, this, [this]() {
            releaseWaylandInhibitor();
        });
        attachWindowSignals();
        updateFullscreenFromWindow();
    }

    if (auto *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &IdleInhibitor::cleanupWayland);
    }
}

IdleInhibitor::~IdleInhibitor() {
    cleanupWayland();
}

void IdleInhibitor::setActivityTimeoutMs(int ms) {
    m_activityTimeoutMs = qMax(100, ms);
    if (m_spaceMouseActive)
        m_activityTimer.start(m_activityTimeoutMs);
}

void IdleInhibitor::notifyActivity() {
    const bool wasActive = active();
    m_spaceMouseActive = true;
    m_activityTimer.start(m_activityTimeoutMs);
    syncWaylandInhibitor();
    if (!wasActive)
        emit activeChanged();
}

void IdleInhibitor::setFullscreen(bool fullscreen) {
    if (m_fullscreenActive == fullscreen)
        return;
    const bool wasActive = active();
    m_fullscreenActive = fullscreen;
    syncWaylandInhibitor();
    if (wasActive != active())
        emit activeChanged();
}

void IdleInhibitor::initWayland() {
#if QT_CONFIG(wayland)
    auto *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!guiApp)
        return;

    auto *waylandApp = guiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!waylandApp)
        return;

    m_display = waylandApp->display();
    if (!m_display)
        return;

    // Use a private Wayland event queue for the one-shot registry roundtrip so we
    // never dispatch Qt's own Wayland events out of order.
    wl_event_queue *queue = wl_display_create_queue(m_display);
    if (!queue)
        return;

    wl_registry *registry = wl_display_get_registry(m_display);
    if (!registry) {
        wl_event_queue_destroy(queue);
        return;
    }

    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);

    RegistryBindState bindState;
    wl_registry_add_listener(registry, &kRegistryListener, &bindState);
    wl_display_roundtrip_queue(m_display, queue);

    if (bindState.manager) {
        // Detach from the temporary queue before destroying it. Both manager and
        // inhibitor interfaces have requests only (zero events).
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(bindState.manager), nullptr);
        m_manager = bindState.manager;
    }

    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
#endif
}

void IdleInhibitor::attachWindowSignals() {
#if QT_CONFIG(wayland)
    if (m_windowSignalsConnected || !m_window || !m_manager)
        return;

    auto *waylandWindow = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!waylandWindow)
        return;

    m_windowSignalsConnected = true;
    connect(waylandWindow, &QNativeInterface::Private::QWaylandWindow::surfaceCreated,
            this, &IdleInhibitor::syncWaylandInhibitor);
    connect(waylandWindow, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed,
            this, &IdleInhibitor::releaseWaylandInhibitor);
#endif
}

void IdleInhibitor::updateFullscreenFromWindow() {
    if (!m_window)
        return;
    const bool isFullscreen = (m_window->visibility() == QWindow::FullScreen)
                              || m_window->windowStates().testFlag(Qt::WindowFullScreen);
    setFullscreen(isFullscreen);
}

void IdleInhibitor::syncWaylandInhibitor() {
#if QT_CONFIG(wayland)
    if (!m_manager || !m_display)
        return;

    if (!active()) {
        releaseWaylandInhibitor();
        return;
    }

    if (m_inhibitor || !m_window)
        return;

    attachWindowSignals();
    auto *waylandWindow = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!waylandWindow)
        return;

    wl_surface *surface = waylandWindow->surface();
    if (!surface)
        return;

    m_inhibitor = createWaylandInhibitor(m_manager, surface);
    wl_display_flush(m_display);
#endif
}

void IdleInhibitor::releaseWaylandInhibitor() {
#if QT_CONFIG(wayland)
    if (!m_inhibitor)
        return;
    destroyWaylandInhibitor(m_inhibitor);
    m_inhibitor = nullptr;
    if (m_display)
        wl_display_flush(m_display);
#endif
}

void IdleInhibitor::cleanupWayland() {
#if QT_CONFIG(wayland)
    releaseWaylandInhibitor();
    if (m_manager) {
        destroyWaylandManager(m_manager);
        m_manager = nullptr;
        if (m_display)
            wl_display_flush(m_display);
    }
    m_display = nullptr;
#endif
}
