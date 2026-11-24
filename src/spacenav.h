#pragma once

#include <QLibrary>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>

struct SpaceNavMotion {
    int x = 0;
    int y = 0;
    int z = 0;
    int rx = 0;
    int ry = 0;
    int rz = 0;
};

struct GlobeMotionDelta {
    double dLatDeg = 0.0;
    double dLonDeg = 0.0;
    double zoomFactor = 1.0;
    double dPitchDeg = 0.0;
    double dHeadingDeg = 0.0;
};

// Optional 3D SpaceMouse integration using spacenavd's native AF_UNIX socket
// protocol via runtime-loaded libspnav (no X11 dependency, no hard link dep).
class SpaceNavController : public QObject {
    Q_OBJECT
public:
    explicit SpaceNavController(QObject *parent = nullptr);
    ~SpaceNavController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool connected() const { return m_connected; }
    bool libraryAvailable() const { return m_libLoaded; }
    QString deviceName() const { return m_deviceName; }
    QString statusText() const;

    double sensitivity() const { return m_sensitivity; }
    void setSensitivity(double value);

    bool invertAxes() const { return m_invertAxes; }
    void setInvertAxes(bool invert);

    // Pure math helpers (exposed for unit tests).
    static double shapedAxis(int raw, int deadzone = 12, double maxRaw = 350.0);
    static GlobeMotionDelta mapMotionToGlobe(const SpaceNavMotion &m,
                                             double sensitivity,
                                             bool invertAxes,
                                             double altitudeRadii,
                                             double latDeg,
                                             double headingDeg);

signals:
    void stateChanged();
    void motionReceived(const SpaceNavMotion &motion);
    void buttonPressed(int buttonNumber);

private:
    bool ensureLibraryLoaded();
    void tryConnect();
    void disconnectDaemon();
    void onSocketActivated();
    void refreshDeviceName();

    // Runtime-resolved libspnav C symbols (ABI matches spnav.h).
    using FnOpen = int (*)();
    using FnClose = int (*)();
    using FnFd = int (*)();
    using FnPollEvent = int (*)(void *);
    using FnRemoveEvents = int (*)(int);
    using FnClientName = int (*)(const char *);
    using FnDevName = int (*)(char *, int);

    QLibrary m_lib;
    bool m_libTried = false;
    bool m_libLoaded = false;

    FnOpen m_open = nullptr;
    FnClose m_close = nullptr;
    FnFd m_fd = nullptr;
    FnPollEvent m_pollEvent = nullptr;
    FnRemoveEvents m_removeEvents = nullptr;
    FnClientName m_clientName = nullptr;
    FnDevName m_devName = nullptr;

    bool m_enabled = false;
    bool m_connected = false;
    double m_sensitivity = 1.0;
    bool m_invertAxes = false;
    QString m_deviceName;

    QSocketNotifier *m_notifier = nullptr;
    QTimer m_reconnectTimer;
};
