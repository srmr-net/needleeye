#include "spacenav.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Minimal ABI-compatible subset of <spnav.h> so needleeye compiles and runs
// cleanly even on machines without libspnav headers or runtime library.
constexpr int kSpnavEventMotion = 1;
constexpr int kSpnavEventButton = 2;
constexpr int kSpnavEventDev = 3;

struct RawSpnavMotion {
    int type;
    int x, y, z;
    int rx, ry, rz;
    unsigned int period;
    int *data;
};

struct RawSpnavButton {
    int type;
    int press;
    int bnum;
};

struct RawSpnavDev {
    int type;
    int op;
    int id;
    int devtype;
    int usbid[2];
};

union RawSpnavEvent {
    int type;
    RawSpnavMotion motion;
    RawSpnavButton button;
    RawSpnavDev dev;
    char pad[64];
};

constexpr double kDegToRad = M_PI / 180.0;

} // namespace

SpaceNavController::SpaceNavController(QObject *parent) : QObject(parent) {
    m_reconnectTimer.setInterval(3000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_enabled && !m_connected)
            tryConnect();
    });
}

SpaceNavController::~SpaceNavController() {
    disconnectDaemon();
}

void SpaceNavController::setEnabled(bool enabled) {
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (m_enabled) {
        tryConnect();
        if (!m_connected && m_libLoaded)
            m_reconnectTimer.start();
    } else {
        m_reconnectTimer.stop();
        disconnectDaemon();
    }
    emit stateChanged();
}

void SpaceNavController::setSensitivity(double value) {
    value = std::clamp(value, 0.2, 3.0);
    if (std::abs(m_sensitivity - value) < 1e-4)
        return;
    m_sensitivity = value;
    emit stateChanged();
}

void SpaceNavController::setInvertAxes(bool invert) {
    if (m_invertAxes == invert)
        return;
    m_invertAxes = invert;
    emit stateChanged();
}

QString SpaceNavController::statusText() const {
    if (!m_enabled)
        return tr("Disabled");
    if (!m_libLoaded)
        return tr("libspnav not installed");
    if (!m_connected)
        return tr("spacenavd not running (waiting…)");
    if (!m_deviceName.isEmpty())
        return tr("Connected: %1").arg(m_deviceName);
    return tr("Connected to spacenavd");
}

bool SpaceNavController::ensureLibraryLoaded() {
    if (m_libTried)
        return m_libLoaded;
    m_libTried = true;

    const QStringList candidates = {
        QStringLiteral("spnav"),
        QStringLiteral("libspnav.so.0"),
        QStringLiteral("libspnav.so")
    };
    for (const QString &name : candidates) {
        m_lib.setFileName(name);
        if (m_lib.load())
            break;
    }
    if (!m_lib.isLoaded())
        return false;

    m_open = reinterpret_cast<FnOpen>(m_lib.resolve("spnav_open"));
    m_close = reinterpret_cast<FnClose>(m_lib.resolve("spnav_close"));
    m_fd = reinterpret_cast<FnFd>(m_lib.resolve("spnav_fd"));
    m_pollEvent = reinterpret_cast<FnPollEvent>(m_lib.resolve("spnav_poll_event"));
    m_removeEvents = reinterpret_cast<FnRemoveEvents>(m_lib.resolve("spnav_remove_events"));
    m_clientName = reinterpret_cast<FnClientName>(m_lib.resolve("spnav_client_name"));
    m_devName = reinterpret_cast<FnDevName>(m_lib.resolve("spnav_dev_name"));

    m_libLoaded = (m_open && m_close && m_fd && m_pollEvent);
    if (!m_libLoaded)
        m_lib.unload();
    return m_libLoaded;
}

void SpaceNavController::tryConnect() {
    if (m_connected)
        return;
    if (!ensureLibraryLoaded())
        return;

    // Avoid calling spnav_open() if the AF_UNIX socket isn't present yet so
    // libspnav doesn't spam stderr on every reconnect tick.
    if (!QFileInfo::exists(QStringLiteral("/var/run/spnav.sock")) &&
        !QFileInfo::exists(QStringLiteral("/run/spnav.sock"))) {
        return;
    }

    if (m_open() == -1)
        return;

    const int fd = m_fd();
    if (fd < 0) {
        m_close();
        return;
    }

    if (m_clientName)
        m_clientName("needleeye");

    m_connected = true;
    m_reconnectTimer.stop();
    refreshDeviceName();

    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SpaceNavController::onSocketActivated);
    emit stateChanged();
}

void SpaceNavController::disconnectDaemon() {
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_connected && m_close)
        m_close();
    const bool wasConnected = m_connected;
    m_connected = false;
    m_deviceName.clear();
    if (wasConnected)
        emit stateChanged();
}

void SpaceNavController::refreshDeviceName() {
    m_deviceName.clear();
    if (!m_connected || !m_devName)
        return;
    char buf[256];
    std::memset(buf, 0, sizeof(buf));
    if (m_devName(buf, static_cast<int>(sizeof(buf))) > 0)
        m_deviceName = QString::fromUtf8(buf).trimmed();
}

void SpaceNavController::onSocketActivated() {
    if (!m_connected || !m_pollEvent)
        return;

    RawSpnavEvent ev;
    bool hasMotion = false;
    SpaceNavMotion latestMotion;
    int drained = 0;

    // Coalesce queued motion events so camera movement stays crisp with zero
    // post-release lag, while preserving every button press and device change.
    while (drained < 64 && m_pollEvent(&ev) != 0) {
        ++drained;
        if (ev.type == kSpnavEventMotion) {
            latestMotion = {ev.motion.x, ev.motion.y, ev.motion.z,
                            ev.motion.rx, ev.motion.ry, ev.motion.rz};
            hasMotion = true;
        } else if (ev.type == kSpnavEventButton) {
            if (ev.button.press)
                emit buttonPressed(ev.button.bnum);
        } else if (ev.type == kSpnavEventDev) {
            refreshDeviceName();
            emit stateChanged();
        }
    }

    // If the daemon closed the socket, spnav_fd() drops to -1.
    if (m_fd() < 0) {
        disconnectDaemon();
        if (m_enabled)
            m_reconnectTimer.start();
        return;
    }

    if (hasMotion)
        emit motionReceived(latestMotion);
}

double SpaceNavController::shapedAxis(int raw, int deadzone, double maxRaw) {
    const int mag = std::abs(raw);
    if (mag <= deadzone || maxRaw <= deadzone)
        return 0.0;
    const double norm = std::clamp(static_cast<double>(mag - deadzone) / (maxRaw - deadzone), 0.0, 1.0);
    // Blend linear + quadratic curve for fine street-level precision and brisk global glide.
    const double curved = 0.35 * norm + 0.65 * norm * norm;
    return raw >= 0 ? curved : -curved;
}

GlobeMotionDelta SpaceNavController::mapMotionToGlobe(const SpaceNavMotion &m,
                                                      double sensitivity,
                                                      bool invertAxes,
                                                      double altitudeRadii,
                                                      double latDeg,
                                                      double headingDeg) {
    GlobeMotionDelta out;
    const double s = std::clamp(sensitivity, 0.2, 3.0);
    const double sign = invertAxes ? -1.0 : 1.0;

    // libspnav AF_UNIX axes: +x right, +y up, +z toward user (-z forward),
    // +rx tilt forward, +ry twist yaw, +rz roll.
    const double tx = shapedAxis(m.x) * sign;
    const double ty = shapedAxis(m.y) * sign;
    const double tz = shapedAxis(m.z) * sign;
    const double rx = shapedAxis(m.rx) * sign;
    const double ry = shapedAxis(m.ry) * sign;

    // Pan speed scales with camera altitude so orbital flight and city inspection both feel natural.
    const double panSpeed = std::clamp(altitudeRadii * 1.45, 0.0012, 3.2) * s;
    const double cosLat = std::max(0.2, std::cos(latDeg * kDegToRad));
    const double hRad = headingDeg * kDegToRad;

    // Pushing forward (-z) moves forward along current heading; pushing right (+x) strafes right.
    const double forward = -tz * panSpeed;
    const double strafe = tx * panSpeed;
    out.dLatDeg = forward * std::cos(hRad) - strafe * std::sin(hRad);
    out.dLonDeg = (forward * std::sin(hRad) + strafe * std::cos(hRad)) / cosLat;

    // Pulling puck up (+y) ascends (zooms out); pressing down (-y) dives closer (zooms in).
    out.zoomFactor = std::exp(ty * 0.045 * s);

    // Tilt puck forward/back adjusts horizon pitch; twist rotates compass heading.
    out.dPitchDeg = rx * 1.35 * s;
    out.dHeadingDeg = -ry * 1.65 * s;
    return out;
}
