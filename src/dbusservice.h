#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class Backend;
class GlobeView;
class QQuickWindow;

// Session D-Bus automation interface (`org.needleeye.App` at `/org/needleeye/App`).
// All methods accept primitive D-Bus types for clean `busctl` / `dbus-mcp`
// introspection and return compact JSON strings (`{"ok":true,...}` or `{"ok":false,"error":"..."}`).
class DbusService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.needleeye.App")

public:
    static constexpr const char *ServiceName = "org.needleeye.App";
    static constexpr const char *ObjectPath = "/org/needleeye/App";
    static constexpr const char *InterfaceName = "org.needleeye.App";

    explicit DbusService(Backend *backend, GlobeView *globe = nullptr,
                         QQuickWindow *window = nullptr, QObject *parent = nullptr);

    bool registerOnSessionBus();

public slots:
    QString GetState();
    QString FlyTo(double lat, double lon, double altKm);
    QString SetView(double lat, double lon, double altKm, double pitch, double heading, bool instant);
    QString ResetNorth();
    QString ResetHome();

    QString ListBaseMaps();
    QString SetBaseMap(const QString &idOrName);

    QString SetTerrain(bool enabled);
    QString SetGrid(bool enabled);
    QString SetDayNight(bool enabled);
    QString SetAtmosphere(bool enabled);
    QString SetHudHidden(bool hidden);
    QString SetSidebarVisible(bool visible);

    QString ListFeeds();
    QString LoadSource(const QString &pathOrUrl, const QString &name);
    QString SetFeedEnabled(const QString &feedIdOrName, bool enabled);
    QString RemoveFeed(const QString &feedId);
    QString RefreshFeeds();

    QString ListFeatures(const QString &query, int limit);
    QString SelectFeature(const QString &idOrName, bool flyTo);
    QString SetTimeline(bool active, double progress, bool playing);

    QString CaptureScreenshot(const QString &filePath);
    QString Quit();

private:
    QString okJson(const QVariantMap &fields = {}) const;
    QString errorJson(const QString &message) const;

    QPointer<Backend> m_backend;
    QPointer<GlobeView> m_globe;
    QPointer<QQuickWindow> m_window;
};
