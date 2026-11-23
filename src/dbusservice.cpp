#include "dbusservice.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "backend.h"
#include "globeview.h"

DbusService::DbusService(Backend *backend, GlobeView *globe, QQuickWindow *window, QObject *parent)
    : QObject(parent), m_backend(backend), m_globe(globe), m_window(window) {}

bool DbusService::registerOnSessionBus() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(QString::fromLatin1(ServiceName)))
        return false;
    return bus.registerObject(QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportAllSlots);
}

QString DbusService::okJson(const QVariantMap &fields) const {
    QJsonObject obj = QJsonObject::fromVariantMap(fields);
    obj.insert(QStringLiteral("ok"), true);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString DbusService::errorJson(const QString &message) const {
    QJsonObject obj;
    obj.insert(QStringLiteral("ok"), false);
    obj.insert(QStringLiteral("error"), message);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString DbusService::GetState() {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));

    QVariantMap out;
    if (m_globe) {
        QVariantMap cam;
        cam.insert(QStringLiteral("lat"), m_globe->cameraLat());
        cam.insert(QStringLiteral("lon"), m_globe->cameraLon());
        cam.insert(QStringLiteral("altKm"), m_globe->cameraAltitudeKm());
        cam.insert(QStringLiteral("pitch"), m_globe->cameraPitch());
        cam.insert(QStringLiteral("heading"), m_globe->cameraHeading());
        out.insert(QStringLiteral("camera"), cam);
    }

    out.insert(QStringLiteral("baseMapId"), m_backend->baseLayerId());
    out.insert(QStringLiteral("baseMapName"), m_backend->baseLayerName());
    out.insert(QStringLiteral("terrain"), m_backend->terrainEnabled());
    out.insert(QStringLiteral("terrainExaggeration"), m_backend->terrainExaggeration());
    out.insert(QStringLiteral("grid"), m_backend->graticuleEnabled());
    out.insert(QStringLiteral("atmosphere"), m_backend->atmosphereEnabled());
    out.insert(QStringLiteral("dayNight"), m_backend->dayNightEnabled());
    out.insert(QStringLiteral("hudHidden"), m_backend->hudHidden());
    out.insert(QStringLiteral("sidebarVisible"), m_backend->sidebarVisible());
    out.insert(QStringLiteral("visibleFeatures"), m_backend->visiblePlacemarks());
    out.insert(QStringLiteral("totalFeatures"), m_backend->totalPlacemarks());
    out.insert(QStringLiteral("loadingCount"), m_backend->loadingCount());
    out.insert(QStringLiteral("hasTimeline"), m_backend->hasTimeline());
    out.insert(QStringLiteral("timelineActive"), m_backend->timelineActive());
    out.insert(QStringLiteral("timelinePlaying"), m_backend->timelinePlaying());
    out.insert(QStringLiteral("timelineProgress"), m_backend->timelineProgress());
    out.insert(QStringLiteral("currentTime"), m_backend->currentTimeLabel());
    if (m_backend->hasSelection())
        out.insert(QStringLiteral("selectedFeature"), m_backend->selectedPlacemarkVariant());
    return okJson(out);
}

QString DbusService::FlyTo(double lat, double lon, double altKm) {
    if (!m_globe)
        return errorJson(QStringLiteral("Globe viewport unavailable."));
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return errorJson(QStringLiteral("Coordinates out of range (lat -90..90, lon -180..180)."));

    const double altRadii = altKm > 0.0 ? (altKm / 6371.0) : m_globe->cameraAltitudeRadii();
    m_globe->flyTo(lat, lon, altRadii);
    return okJson({
        {QStringLiteral("lat"), lat},
        {QStringLiteral("lon"), lon},
        {QStringLiteral("altKm"), altRadii * 6371.0}
    });
}

QString DbusService::SetView(double lat, double lon, double altKm, double pitch, double heading, bool instant) {
    if (!m_globe)
        return errorJson(QStringLiteral("Globe viewport unavailable."));
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return errorJson(QStringLiteral("Coordinates out of range (lat -90..90, lon -180..180)."));

    const double altRadii = altKm > 0.0 ? (altKm / 6371.0) : m_globe->cameraAltitudeRadii();
    const double p = std::isnan(pitch) ? m_globe->cameraPitch() : pitch;
    const double h = std::isnan(heading) ? m_globe->cameraHeading() : heading;
    m_globe->setView(lat, lon, altRadii, p, h, instant);
    return okJson({
        {QStringLiteral("lat"), m_globe->cameraLat()},
        {QStringLiteral("lon"), m_globe->cameraLon()},
        {QStringLiteral("altKm"), altRadii * 6371.0},
        {QStringLiteral("pitch"), std::clamp(p, 0.0, 62.0)},
        {QStringLiteral("heading"), h},
        {QStringLiteral("instant"), instant}
    });
}

QString DbusService::ResetNorth() {
    if (!m_globe)
        return errorJson(QStringLiteral("Globe viewport unavailable."));
    m_globe->resetNorth();
    return okJson();
}

QString DbusService::ResetHome() {
    if (!m_globe)
        return errorJson(QStringLiteral("Globe viewport unavailable."));
    m_globe->resetHome();
    return okJson();
}

QString DbusService::ListBaseMaps() {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    return okJson({{QStringLiteral("baseMaps"), m_backend->baseMapsVariant()}});
}

QString DbusService::SetBaseMap(const QString &idOrName) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    if (!m_backend->selectBaseMapFlexible(idOrName))
        return errorJson(QStringLiteral("Base map not found: %1").arg(idOrName));
    return okJson({
        {QStringLiteral("baseMapId"), m_backend->baseLayerId()},
        {QStringLiteral("baseMapName"), m_backend->baseLayerName()}
    });
}

QString DbusService::SetTerrain(bool enabled) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setTerrainEnabled(enabled);
    return okJson({{QStringLiteral("terrain"), m_backend->terrainEnabled()}});
}

QString DbusService::SetGrid(bool enabled) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setGraticuleEnabled(enabled);
    return okJson({{QStringLiteral("grid"), m_backend->graticuleEnabled()}});
}

QString DbusService::SetDayNight(bool enabled) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setDayNightEnabled(enabled);
    return okJson({{QStringLiteral("dayNight"), m_backend->dayNightEnabled()}});
}

QString DbusService::SetAtmosphere(bool enabled) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setAtmosphereEnabled(enabled);
    return okJson({{QStringLiteral("atmosphere"), m_backend->atmosphereEnabled()}});
}

QString DbusService::SetHudHidden(bool hidden) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setHudHidden(hidden);
    return okJson({{QStringLiteral("hudHidden"), m_backend->hudHidden()}});
}

QString DbusService::SetSidebarVisible(bool visible) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->setSidebarVisible(visible);
    return okJson({{QStringLiteral("sidebarVisible"), m_backend->sidebarVisible()}});
}

QString DbusService::ListFeeds() {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    return okJson({{QStringLiteral("feeds"), m_backend->feedsVariant()}});
}

QString DbusService::LoadSource(const QString &pathOrUrl, const QString &name) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    const QString src = pathOrUrl.trimmed();
    if (src.isEmpty())
        return errorJson(QStringLiteral("Path or URL cannot be empty."));

    if (src.startsWith(QStringLiteral("http://")) || src.startsWith(QStringLiteral("https://"))) {
        const QString id = m_backend->addFeedUrl(src, name);
        if (id.isEmpty())
            return errorJson(m_backend->errorMessage().isEmpty() ? QStringLiteral("Invalid URL.") : m_backend->errorMessage());
        return okJson({{QStringLiteral("id"), id}, {QStringLiteral("source"), src}, {QStringLiteral("kind"), QStringLiteral("url")}});
    }

    const QUrl u(src);
    const QString localPath = u.isLocalFile() ? u.toLocalFile() : src;
    if (!QFileInfo::exists(localPath))
        return errorJson(QStringLiteral("File not found: %1").arg(localPath));
    if (!m_backend->loadLocalFile(localPath, name))
        return errorJson(m_backend->errorMessage().isEmpty() ? QStringLiteral("Failed to load file.") : m_backend->errorMessage());

    return okJson({
        {QStringLiteral("source"), localPath},
        {QStringLiteral("kind"), QStringLiteral("file")},
        {QStringLiteral("visibleFeatures"), m_backend->visiblePlacemarks()}
    });
}

QString DbusService::SetFeedEnabled(const QString &feedIdOrName, bool enabled) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    if (!m_backend->setFeedEnabled(feedIdOrName, enabled))
        return errorJson(QStringLiteral("Feed not found: %1").arg(feedIdOrName));
    return okJson({{QStringLiteral("feed"), feedIdOrName}, {QStringLiteral("enabled"), enabled}});
}

QString DbusService::RemoveFeed(const QString &feedId) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    if (!m_backend->removeFeed(feedId))
        return errorJson(QStringLiteral("Feed not found: %1").arg(feedId));
    return okJson({{QStringLiteral("removed"), feedId}});
}

QString DbusService::RefreshFeeds() {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    m_backend->refreshAllFeeds();
    return okJson({{QStringLiteral("loadingCount"), m_backend->loadingCount()}});
}

QString DbusService::ListFeatures(const QString &query, int limit) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));

    const QString q = query.trimmed().toLower();
    const int cap = limit > 0 ? std::min(limit, 500) : 50;
    QVariantList items;
    const auto &all = m_backend->visiblePlacemarkList();
    for (const auto &pm : all) {
        if (!q.isEmpty() &&
            !pm.name.toLower().contains(q) &&
            !pm.folderPath.toLower().contains(q) &&
            !pm.descriptionText.toLower().contains(q)) {
            continue;
        }
        const GeoCoord a = pm.anchor();
        QVariantMap row;
        row.insert(QStringLiteral("id"), pm.id);
        row.insert(QStringLiteral("name"), pm.name);
        row.insert(QStringLiteral("feedId"), pm.feedId);
        row.insert(QStringLiteral("folder"), pm.folderPath);
        row.insert(QStringLiteral("lat"), a.lat);
        row.insert(QStringLiteral("lon"), a.lon);
        row.insert(QStringLiteral("alt"), a.alt);
        if (pm.timeStart.isValid())
            row.insert(QStringLiteral("time"), pm.timeStart.toUTC().toString(Qt::ISODate));
        row.insert(QStringLiteral("hasPath"), !pm.lines.isEmpty());
        row.insert(QStringLiteral("hasPolygon"), !pm.polygons.isEmpty());
        items.append(row);
        if (items.size() >= cap)
            break;
    }

    return okJson({
        {QStringLiteral("totalVisible"), all.size()},
        {QStringLiteral("returned"), items.size()},
        {QStringLiteral("features"), items}
    });
}

QString DbusService::SelectFeature(const QString &idOrName, bool flyTo) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    const QString trimmed = idOrName.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
        m_backend->clearSelection();
        return okJson({{QStringLiteral("selected"), false}});
    }
    if (!m_backend->selectPlacemark(trimmed, flyTo))
        return errorJson(QStringLiteral("Feature not found: %1").arg(trimmed));
    return okJson({
        {QStringLiteral("selected"), true},
        {QStringLiteral("feature"), m_backend->selectedPlacemarkVariant()}
    });
}

QString DbusService::SetTimeline(bool active, double progress, bool playing) {
    if (!m_backend)
        return errorJson(QStringLiteral("Backend unavailable."));
    if (!m_backend->hasTimeline())
        return errorJson(QStringLiteral("No timestamped features loaded."));

    if (progress >= 0.0)
        m_backend->setTimelineProgress(progress);
    m_backend->setTimelineActive(active);
    m_backend->setTimelinePlaying(playing);
    return okJson({
        {QStringLiteral("timelineActive"), m_backend->timelineActive()},
        {QStringLiteral("timelinePlaying"), m_backend->timelinePlaying()},
        {QStringLiteral("timelineProgress"), m_backend->timelineProgress()},
        {QStringLiteral("currentTime"), m_backend->currentTimeLabel()}
    });
}

QString DbusService::CaptureScreenshot(const QString &filePath) {
    if (!m_window)
        return errorJson(QStringLiteral("No active GUI window to capture."));
    const QString target = filePath.trimmed();
    if (target.isEmpty())
        return errorJson(QStringLiteral("Output file path cannot be empty."));

    const QFileInfo fi(target);
    QDir().mkpath(fi.absolutePath());
    const QImage img = m_window->grabWindow();
    if (img.isNull())
        return errorJson(QStringLiteral("Failed to grab window framebuffer."));

    const QString format = fi.suffix().isEmpty() ? QStringLiteral("PNG") : fi.suffix().toUpper();
    const QString tmpPath = target + QStringLiteral(".tmp");
    if (!img.save(tmpPath, format.toLatin1().constData())) {
        QFile::remove(tmpPath);
        return errorJson(QStringLiteral("Failed to save screenshot to %1").arg(target));
    }
    QFile::remove(target);
    if (!QFile::rename(tmpPath, target)) {
        QFile::remove(tmpPath);
        return errorJson(QStringLiteral("Failed to finalize screenshot file %1").arg(target));
    }

    return okJson({
        {QStringLiteral("path"), fi.absoluteFilePath()},
        {QStringLiteral("width"), img.width()},
        {QStringLiteral("height"), img.height()}
    });
}

QString DbusService::Quit() {
    QTimer::singleShot(10, qApp, &QCoreApplication::quit);
    return okJson();
}
