#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

struct wl_display;
struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;

// Native Wayland idle/screensaver inhibitor (zwp_idle_inhibit_manager_v1).
//
// Prevents Hyprland / Omarchy's IdleMonitor from triggering the screensaver or
// screen lock when:
//   1. The user is actively using a 3D SpaceMouse (whose HID events go through
//      spacenavd and are invisible to Hyprland's libinput idle tracker) — held
//      for `activityTimeoutMs` (default 90 s) after the last motion/button event.
//   2. The application window is fullscreen.
//
// Safely no-ops on non-Wayland / offscreen platforms while keeping its state
// machine intact for unit testing.
class IdleInhibitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool spaceMouseActive READ spaceMouseActive NOTIFY activeChanged)
    Q_PROPERTY(bool fullscreenActive READ fullscreenActive NOTIFY activeChanged)
    Q_PROPERTY(bool waylandSupported READ waylandSupported CONSTANT)

public:
    explicit IdleInhibitor(QWindow *window = nullptr,
                           int activityTimeoutMs = 90000,
                           QObject *parent = nullptr);
    ~IdleInhibitor() override;

    bool active() const { return m_spaceMouseActive || m_fullscreenActive; }
    bool spaceMouseActive() const { return m_spaceMouseActive; }
    bool fullscreenActive() const { return m_fullscreenActive; }
    bool waylandSupported() const { return m_manager != nullptr; }

    int activityTimeoutMs() const { return m_activityTimeoutMs; }
    void setActivityTimeoutMs(int ms);

public slots:
    void notifyActivity();
    void setFullscreen(bool fullscreen);

signals:
    void activeChanged();

private:
    void initWayland();
    void attachWindowSignals();
    void updateFullscreenFromWindow();
    void syncWaylandInhibitor();
    void releaseWaylandInhibitor();
    void cleanupWayland();

    QPointer<QWindow> m_window;
    QTimer m_activityTimer;
    int m_activityTimeoutMs = 90000;
    bool m_spaceMouseActive = false;
    bool m_fullscreenActive = false;
    bool m_windowSignalsConnected = false;

    wl_display *m_display = nullptr;
    zwp_idle_inhibit_manager_v1 *m_manager = nullptr;
    zwp_idle_inhibitor_v1 *m_inhibitor = nullptr;
};
