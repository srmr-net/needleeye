#include "backend.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {

struct BuiltinPlace {
    const char *name;
    const char *region;
    double lat;
    double lon;
};

constexpr BuiltinPlace kBuiltinPlaces[] = {
    {"Tokyo", "Japan", 35.6762, 139.6503},
    {"New York", "United States", 40.7128, -74.0060},
    {"London", "United Kingdom", 51.5072, -0.1276},
    {"Copenhagen", "Denmark", 55.6761, 12.5683},
    {"San Francisco", "United States", 37.7749, -122.4194},
    {"Reykjavik", "Iceland", 64.1466, -21.9426},
    {"Sydney", "Australia", -33.8688, 151.2093},
    {"Singapore", "Singapore", 1.3521, 103.8198},
    {"Istanbul", "Turkey", 41.0082, 28.9784},
    {"Mexico City", "Mexico", 19.4326, -99.1332},
    {"Santiago", "Chile", -33.4489, -70.6693},
    {"Anchorage", "United States", 61.2181, -149.9003},
    {"Honolulu", "United States", 21.3069, -157.8583},
    {"Manila", "Philippines", 14.5995, 120.9842},
    {"Jakarta", "Indonesia", -6.2088, 106.8456},
    {"Wellington", "New Zealand", -41.2865, 174.7762},
    {"Kathmandu", "Nepal", 27.7172, 85.3240},
    {"Mount Everest", "Himalayas", 27.9881, 86.9250},
    {"Mount Fuji", "Japan", 35.3606, 138.7274},
    {"Mount Etna", "Italy", 37.7510, 14.9934},
    {"Kilauea", "Hawaii", 19.4069, -155.2834},
    {"Grand Canyon", "United States", 36.1069, -112.1129},
    {"Ring of Fire", "Pacific", 10.0, 160.0},
    {"Mid-Atlantic Ridge", "Atlantic", 25.0, -45.0},
    {"Zurich", "Switzerland", 47.3769, 8.5417},
    {"Berlin", "Germany", 52.5200, 13.4050},
    {"Paris", "France", 48.8566, 2.3522},
    {"Cairo", "Egypt", 30.0444, 31.2357},
    {"Nairobi", "Kenya", -1.2921, 36.8219},
    {"Cape Town", "South Africa", -33.9249, 18.4241},
    {"Rio de Janeiro", "Brazil", -22.9068, -43.1729},
    {"Buenos Aires", "Argentina", -34.6037, -58.3816},
    {"Toronto", "Canada", 43.6532, -79.3832},
    {"Vancouver", "Canada", 49.2827, -123.1207},
    {"Seoul", "South Korea", 37.5665, 126.9780},
    {"Taipei", "Taiwan", 25.0330, 121.5654},
    {"Mumbai", "India", 19.0760, 72.8777},
    {"Dubai", "UAE", 25.2048, 55.2708},
    {"Athens", "Greece", 37.9838, 23.7275},
    {"Lisbon", "Portugal", 38.7223, -9.1393},
    {"Oslo", "Norway", 59.9139, 10.7522},
    {"Stockholm", "Sweden", 59.3293, 18.0686}
};

} // namespace

Backend::Backend(TileManager *tileManager, FilePicker *filePicker, bool autoLoadDefaultFeeds, QObject *parent)
    : QObject(parent),
      m_tileManager(tileManager),
      m_filePicker(filePicker) {
    if (!m_tileManager) {
        m_tileManager = new TileManager({}, this);
        m_ownsTileManager = true;
    }
    if (!m_filePicker) {
        m_filePicker = new PortalFilePicker(this);
        m_ownsFilePicker = true;
    }

    connect(m_tileManager, &TileManager::networkError, this, [this](const QString &msg) {
        setError(msg);
    });

    m_timelineTimer.setInterval(50);
    connect(&m_timelineTimer, &QTimer::timeout, this, [this]() {
        if (!hasTimeline()) {
            setTimelinePlaying(false);
            return;
        }
        double next = m_timelineProgress + 0.004;
        if (next > 1.0)
            next = 0.0;
        setTimelineProgress(next);
    });

    m_autoRefreshTimer.setInterval(60000);
    connect(&m_autoRefreshTimer, &QTimer::timeout, this, [this]() {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (const auto &feed : m_feeds) {
            if (!feed.enabled || feed.isLocal || feed.loading || feed.refreshIntervalSec <= 0)
                continue;
            if (!feed.lastUpdated.isValid() || feed.lastUpdated.secsTo(now) >= feed.refreshIntervalSec)
                refreshFeed(feed.id);
        }
    });
    m_autoRefreshTimer.start();

    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(260);
    connect(&m_searchDebounce, &QTimer::timeout, this, [this]() {
        if (m_pendingSearchQuery.size() >= 2)
            runPhotonSearch(m_pendingSearchQuery);
    });

    setupDefaultFeeds();
    if (autoLoadDefaultFeeds)
        refreshAllFeeds();
}

void Backend::setupDefaultFeeds() {
    m_feeds = {
        {QStringLiteral("usgs-week-25"),
         tr("USGS Earthquakes (7d, M2.5+)"),
         QStringLiteral("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_week_age.kml"),
         false, true, false, {}, {}, 300, {}, {}, {}},
        {QStringLiteral("gdacs-live"),
         tr("GDACS Global Disasters"),
         QStringLiteral("https://www.gdacs.org/contentdata/xml/gdacs.kml"),
         false, true, false, {}, {}, 600, {}, {}, {}},
        {QStringLiteral("usgs-month-45-anim"),
         tr("USGS Earthquakes (30d, M4.5+ Animated)"),
         QStringLiteral("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_month_depth_animated.kml"),
         false, false, false, {}, {}, 900, {}, {}, {}},
        {QStringLiteral("usgs-day-all"),
         tr("USGS All Earthquakes (24h)"),
         QStringLiteral("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_day_age.kml"),
         false, false, false, {}, {}, 300, {}, {}, {}}
    };
}

void Backend::setBaseLayer(int layer) {
    layer = std::clamp(layer, 0, 3);
    if (m_baseLayer == layer)
        return;
    m_baseLayer = layer;
    emit settingsChanged();
}

QString Backend::baseLayerName() const {
    switch (static_cast<BaseLayer>(m_baseLayer)) {
    case BaseLayer::DarkMatter:    return tr("Dark");
    case BaseLayer::EsriSatellite: return tr("Satellite");
    case BaseLayer::Sentinel2:     return tr("Sentinel-2");
    case BaseLayer::OpenStreetMap: return tr("OSM");
    default:                       return tr("Dark");
    }
}

void Backend::setTerrainEnabled(bool enabled) {
    if (m_terrainEnabled == enabled)
        return;
    m_terrainEnabled = enabled;
    emit settingsChanged();
}

void Backend::setGraticuleEnabled(bool enabled) {
    if (m_graticuleEnabled == enabled)
        return;
    m_graticuleEnabled = enabled;
    emit settingsChanged();
}

void Backend::setAtmosphereEnabled(bool enabled) {
    if (m_atmosphereEnabled == enabled)
        return;
    m_atmosphereEnabled = enabled;
    emit settingsChanged();
}

QVariantList Backend::feedsVariant() const {
    QVariantList list;
    list.reserve(m_feeds.size());
    for (const auto &f : m_feeds) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), f.id);
        m.insert(QStringLiteral("name"), f.name);
        m.insert(QStringLiteral("source"), f.source);
        m.insert(QStringLiteral("isLocal"), f.isLocal);
        m.insert(QStringLiteral("enabled"), f.enabled);
        m.insert(QStringLiteral("loading"), f.loading);
        m.insert(QStringLiteral("error"), f.error);
        m.insert(QStringLiteral("count"), f.placemarks.size());
        m.insert(QStringLiteral("updated"),
                 f.lastUpdated.isValid() ? f.lastUpdated.toLocalTime().toString(QStringLiteral("HH:mm")) : QString());
        list.append(m);
    }
    return list;
}

QVariantList Backend::foldersVariant() const {
    QVariantList list;
    for (const auto &f : m_feeds) {
        if (!f.enabled)
            continue;
        for (const auto &folder : f.folders) {
            if (folder.placemarkCount <= 0)
                continue;
            const QString key = f.id + QStringLiteral("|") + folder.path;
            QVariantMap m;
            m.insert(QStringLiteral("feedId"), f.id);
            m.insert(QStringLiteral("feedName"), f.name);
            m.insert(QStringLiteral("path"), folder.path);
            m.insert(QStringLiteral("name"), folder.name);
            m.insert(QStringLiteral("count"), folder.placemarkCount);
            m.insert(QStringLiteral("visible"), m_folderVisibility.value(key, folder.visible));
            list.append(m);
        }
    }
    return list;
}

QVariantMap Backend::selectedPlacemarkVariant() const {
    if (m_selectedId.isEmpty())
        return {};

    for (const auto &feed : m_feeds) {
        for (const auto &pm : feed.placemarks) {
            if (pm.id != m_selectedId)
                continue;
            const GeoCoord c = pm.anchor();
            QVariantMap m;
            m.insert(QStringLiteral("id"), pm.id);
            m.insert(QStringLiteral("name"), pm.name);
            m.insert(QStringLiteral("feedName"), feed.name);
            m.insert(QStringLiteral("folder"), pm.folderPath);
            m.insert(QStringLiteral("lat"), c.lat);
            m.insert(QStringLiteral("lon"), c.lon);
            m.insert(QStringLiteral("alt"), c.alt);
            m.insert(QStringLiteral("description"), pm.descriptionText);
            m.insert(QStringLiteral("linkUrl"), pm.linkUrl);
            m.insert(QStringLiteral("color"), pm.style.iconColor.name());
            m.insert(QStringLiteral("time"),
                     pm.timeStart.isValid() ? pm.timeStart.toLocalTime().toString(Qt::ISODate).replace('T', ' ') : QString());
            QVariantList extList;
            for (auto it = pm.extendedData.cbegin(); it != pm.extendedData.cend(); ++it) {
                QVariantMap kv;
                kv.insert(QStringLiteral("key"), it.key());
                kv.insert(QStringLiteral("value"), it.value());
                extList.append(kv);
            }
            m.insert(QStringLiteral("extendedData"), extList);
            return m;
        }
    }
    return {};
}

void Backend::setTimelineActive(bool active) {
    if (m_timelineActive == active)
        return;
    m_timelineActive = active;
    if (!m_timelineActive)
        setTimelinePlaying(false);
    rebuildVisiblePlacemarks();
    emit timelineChanged();
}

void Backend::setTimelinePlaying(bool playing) {
    if (m_timelinePlaying == playing)
        return;
    m_timelinePlaying = playing;
    if (m_timelinePlaying) {
        if (!m_timelineActive)
            m_timelineActive = true;
        m_timelineTimer.start();
    } else {
        m_timelineTimer.stop();
    }
    emit timelineChanged();
}

void Backend::setTimelineProgress(double progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    if (std::abs(m_timelineProgress - progress) < 1e-5)
        return;
    m_timelineProgress = progress;
    if (!m_timelineActive)
        m_timelineActive = true;
    rebuildVisiblePlacemarks();
    emit timelineChanged();
}

QString Backend::currentTimeLabel() const {
    if (!hasTimeline())
        return {};
    const qint64 cur = m_timeMinMs + static_cast<qint64>((m_timeMaxMs - m_timeMinMs) * m_timelineProgress);
    return QDateTime::fromMSecsSinceEpoch(cur, QTimeZone::utc()).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString Backend::timeRangeLabel() const {
    if (!hasTimeline())
        return {};
    const QString a = QDateTime::fromMSecsSinceEpoch(m_timeMinMs, QTimeZone::utc()).toLocalTime().toString(QStringLiteral("MMM dd"));
    const QString b = QDateTime::fromMSecsSinceEpoch(m_timeMaxMs, QTimeZone::utc()).toLocalTime().toString(QStringLiteral("MMM dd"));
    return QStringLiteral("%1 – %2").arg(a, b);
}

void Backend::primaryAction() {
    if (hasTimeline() && m_timelineActive) {
        setTimelinePlaying(!m_timelinePlaying);
    } else if (hasTimeline()) {
        m_timelineActive = true;
        m_timelineProgress = 0.0;
        setTimelinePlaying(true);
        rebuildVisiblePlacemarks();
        emit timelineChanged();
    } else {
        refreshAllFeeds();
    }
}

int Backend::findFeedIndex(const QString &feedId) const {
    for (int i = 0; i < m_feeds.size(); ++i)
        if (m_feeds[i].id == feedId)
            return i;
    return -1;
}

void Backend::toggleFeed(const QString &feedId) {
    const int idx = findFeedIndex(feedId);
    if (idx < 0)
        return;
    m_feeds[idx].enabled = !m_feeds[idx].enabled;
    emit feedsChanged();
    if (m_feeds[idx].enabled && m_feeds[idx].placemarks.isEmpty() && !m_feeds[idx].isLocal) {
        refreshFeed(feedId);
    } else {
        recomputeTimeRange();
        rebuildVisiblePlacemarks();
        emit foldersChanged();
    }
}

void Backend::refreshAllFeeds() {
    for (const auto &feed : m_feeds) {
        if (feed.enabled)
            refreshFeed(feed.id);
    }
}

void Backend::refreshFeed(const QString &feedId) {
    const int idx = findFeedIndex(feedId);
    if (idx < 0)
        return;
    auto &feed = m_feeds[idx];
    feed.error.clear();
    if (feed.isLocal) {
        KmlDocument doc = KmlParser::parseFile(feed.source, feed.id);
        if (!doc.error.isEmpty()) {
            feed.error = doc.error;
            setError(doc.error);
        } else {
            feed.placemarks = doc.placemarks;
            feed.folders = doc.folders;
            feed.networkLinks = doc.networkLinks;
            feed.lastUpdated = QDateTime::currentDateTimeUtc();
        }
        emit feedsChanged();
        emit foldersChanged();
        recomputeTimeRange();
        rebuildVisiblePlacemarks();
        return;
    }

    feed.loading = true;
    feed.placemarks.clear();
    feed.folders.clear();
    feed.networkLinks.clear();
    emit feedsChanged();
    setStatus(tr("Refreshing %1…").arg(feed.name));

    QSet<QString> visited;
    fetchUrlForFeed(feed.id, feed.source, 0, visited);
}

void Backend::fetchUrlForFeed(const QString &feedId, const QString &urlStr, int depth, QSet<QString> visited) {
    if (depth > 4 || visited.contains(urlStr))
        return;
    visited.insert(urlStr);

    const QUrl url(urlStr);
    if (!url.isValid()) {
        const int idx = findFeedIndex(feedId);
        if (idx >= 0) {
            m_feeds[idx].loading = false;
            m_feeds[idx].error = tr("Invalid feed URL: %1").arg(urlStr);
            setError(m_feeds[idx].error);
            emit feedsChanged();
        }
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Needleeye/0.1 (Omarchy Linux)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);

    ++m_loadingCount;
    emit statusChanged();

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, feedId, url, depth, visited]() mutable {
        reply->deleteLater();
        m_loadingCount = std::max(0, m_loadingCount - 1);

        const int idx = findFeedIndex(feedId);
        if (idx < 0) {
            emit statusChanged();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            m_feeds[idx].loading = false;
            m_feeds[idx].error = reply->errorString();
            setError(tr("%1: %2").arg(m_feeds[idx].name, reply->errorString()));
            emit feedsChanged();
            emit statusChanged();
            return;
        }

        const QByteArray payload = reply->readAll();
        KmlDocument doc = KmlParser::parseBytes(payload, m_feeds[idx].name, feedId);
        if (!doc.error.isEmpty()) {
            m_feeds[idx].loading = false;
            m_feeds[idx].error = doc.error;
            setError(tr("%1: %2").arg(m_feeds[idx].name, doc.error));
            emit feedsChanged();
            emit statusChanged();
            return;
        }

        auto &feed = m_feeds[idx];
        if (depth == 0 && !doc.name.isEmpty() && feed.name.startsWith(QStringLiteral("http")))
            feed.name = doc.name;

        feed.placemarks.append(doc.placemarks);
        for (const auto &folder : doc.folders) {
            bool merged = false;
            for (auto &existing : feed.folders) {
                if (existing.path == folder.path) {
                    existing.placemarkCount += folder.placemarkCount;
                    merged = true;
                    break;
                }
            }
            if (!merged)
                feed.folders.append(folder);
        }
        feed.lastUpdated = QDateTime::currentDateTimeUtc();

        // Follow any embedded KML <NetworkLink> targets automatically (up to depth 4).
        int spawnedLinks = 0;
        for (const auto &nl : doc.networkLinks) {
            if (!nl.visible || nl.href.isEmpty())
                continue;
            const QUrl resolved = url.resolved(QUrl(nl.href));
            const QString target = resolved.toString();
            if (!visited.contains(target) && spawnedLinks < 12) {
                ++spawnedLinks;
                if (nl.refreshIntervalSec > 0 && nl.refreshIntervalSec < feed.refreshIntervalSec)
                    feed.refreshIntervalSec = std::max(60, nl.refreshIntervalSec);
                fetchUrlForFeed(feedId, target, depth + 1, visited);
            }
        }

        if (spawnedLinks == 0)
            feed.loading = false;

        recomputeTimeRange();
        rebuildVisiblePlacemarks();
        emit feedsChanged();
        emit foldersChanged();
        setStatus(tr("%n features loaded", "", m_totalPlacemarks));
    });
}

bool Backend::ingestKmlBytesForFeed(const QString &feedId, const QByteArray &data, const QString &sourceName) {
    int idx = findFeedIndex(feedId);
    if (idx < 0) {
        FeedEntry entry;
        entry.id = feedId;
        entry.name = sourceName.isEmpty() ? feedId : sourceName;
        entry.isLocal = true;
        entry.enabled = true;
        m_feeds.append(entry);
        idx = m_feeds.size() - 1;
    }

    KmlDocument doc = KmlParser::parseBytes(data, sourceName, feedId);
    if (!doc.error.isEmpty()) {
        m_feeds[idx].error = doc.error;
        setError(doc.error);
        emit feedsChanged();
        return false;
    }

    m_feeds[idx].enabled = true;
    m_feeds[idx].loading = false;
    m_feeds[idx].error.clear();
    m_feeds[idx].placemarks = doc.placemarks;
    m_feeds[idx].folders = doc.folders;
    m_feeds[idx].networkLinks = doc.networkLinks;
    m_feeds[idx].lastUpdated = QDateTime::currentDateTimeUtc();

    recomputeTimeRange();
    rebuildVisiblePlacemarks();
    emit feedsChanged();
    emit foldersChanged();
    return true;
}

void Backend::addFeedUrl(const QString &url, const QString &name) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;

    const QUrl parsed(trimmed);
    if (!parsed.isValid() || (parsed.scheme() != QLatin1String("http") && parsed.scheme() != QLatin1String("https"))) {
        setError(tr("Enter a valid http:// or https:// KML/KMZ URL."));
        return;
    }

    const QString id = QStringLiteral("custom-%1").arg(QDateTime::currentMSecsSinceEpoch());
    FeedEntry entry;
    entry.id = id;
    entry.name = name.trimmed().isEmpty() ? parsed.fileName() : name.trimmed();
    if (entry.name.isEmpty())
        entry.name = parsed.host();
    entry.source = trimmed;
    entry.enabled = true;
    m_feeds.append(entry);

    emit feedsChanged();
    refreshFeed(id);
}

void Backend::removeFeed(const QString &feedId) {
    const int idx = findFeedIndex(feedId);
    if (idx < 0)
        return;
    m_feeds.removeAt(idx);
    if (!m_selectedId.isEmpty() && m_selectedId.startsWith(feedId + QStringLiteral(":"))) {
        m_selectedId.clear();
        emit selectionChanged();
    }
    recomputeTimeRange();
    rebuildVisiblePlacemarks();
    emit feedsChanged();
    emit foldersChanged();
}

void Backend::openLocalFileDialog() {
    m_filePicker->openKmlFile(tr("Open KML or KMZ file"), [this](const QString &path, const QString &error) {
        if (!error.isEmpty()) {
            setError(error);
            return;
        }
        if (!path.isEmpty())
            loadLocalFile(path);
    });
}

bool Backend::loadLocalFile(const QString &filePath) {
    const QString id = QStringLiteral("file-%1").arg(QDateTime::currentMSecsSinceEpoch());
    KmlDocument doc = KmlParser::parseFile(filePath, id);
    if (!doc.error.isEmpty()) {
        setError(doc.error);
        return false;
    }

    FeedEntry entry;
    entry.id = id;
    entry.name = doc.name.isEmpty() ? QFileInfo(filePath).fileName() : doc.name;
    entry.source = filePath;
    entry.isLocal = true;
    entry.enabled = true;
    entry.lastUpdated = QDateTime::currentDateTimeUtc();
    entry.folders = doc.folders;
    entry.placemarks = doc.placemarks;
    entry.networkLinks = doc.networkLinks;
    m_feeds.append(entry);

    recomputeTimeRange();
    rebuildVisiblePlacemarks();
    emit feedsChanged();
    emit foldersChanged();
    setStatus(tr("Opened %1 (%n features)", "", doc.placemarks.size()).arg(entry.name));

    // Fly to first placemark so opening a local KML jumps straight to its area.
    if (!doc.placemarks.isEmpty()) {
        const auto &pm = doc.placemarks.first();
        const GeoCoord c = pm.anchor();
        const double alt = pm.lookAt.valid ? rangeMetersToAltitudeRadii(pm.lookAt.range) : 0.45;
        emit flyToRequested(c.lat, c.lon, alt);
    }
    return true;
}

void Backend::toggleFolder(const QString &feedId, const QString &folderPath) {
    const QString key = feedId + QStringLiteral("|") + folderPath;
    const bool current = m_folderVisibility.value(key, true);
    m_folderVisibility.insert(key, !current);
    rebuildVisiblePlacemarks();
    emit foldersChanged();
}

void Backend::selectPlacemark(const QString &placemarkId, bool flyTo) {
    if (placemarkId.isEmpty()) {
        clearSelection();
        return;
    }

    for (const auto &feed : m_feeds) {
        for (const auto &pm : feed.placemarks) {
            if (pm.id != placemarkId)
                continue;
            m_selectedId = placemarkId;
            emit selectionChanged();
            if (flyTo) {
                const GeoCoord c = pm.anchor();
                const double alt = pm.lookAt.valid ? rangeMetersToAltitudeRadii(pm.lookAt.range) : 0.35;
                emit flyToRequested(c.lat, c.lon, alt);
            }
            return;
        }
    }
}

void Backend::clearSelection() {
    if (m_selectedId.isEmpty())
        return;
    m_selectedId.clear();
    emit selectionChanged();
}

void Backend::dismissError() {
    if (m_errorMessage.isEmpty())
        return;
    m_errorMessage.clear();
    emit errorChanged();
}

void Backend::stepTimeline(double delta) {
    if (!hasTimeline())
        return;
    setTimelineProgress(m_timelineProgress + delta);
}

void Backend::recomputeTimeRange() {
    qint64 minMs = 0;
    qint64 maxMs = 0;
    for (const auto &feed : m_feeds) {
        if (!feed.enabled)
            continue;
        for (const auto &pm : feed.placemarks) {
            if (pm.timeStart.isValid()) {
                const qint64 ms = pm.timeStart.toMSecsSinceEpoch();
                if (minMs == 0 || ms < minMs) minMs = ms;
                if (ms > maxMs) maxMs = ms;
            }
            if (pm.timeEnd.isValid()) {
                const qint64 ms = pm.timeEnd.toMSecsSinceEpoch();
                if (minMs == 0 || ms < minMs) minMs = ms;
                if (ms > maxMs) maxMs = ms;
            }
        }
    }

    if (minMs != m_timeMinMs || maxMs != m_timeMaxMs) {
        m_timeMinMs = minMs;
        m_timeMaxMs = maxMs;
        emit timelineChanged();
    }
}

void Backend::rebuildVisiblePlacemarks() {
    m_visiblePlacemarks.clear();
    m_totalPlacemarks = 0;

    const bool filterTime = timelineActive();
    const qint64 cursorMs = filterTime
        ? m_timeMinMs + static_cast<qint64>((m_timeMaxMs - m_timeMinMs) * m_timelineProgress)
        : 0;
    // Show events within a rolling window behind the timeline cursor so
    // animated earthquake and storm feeds build cleanly over time.
    const qint64 windowMs = std::max<qint64>(3600 * 1000 * 12, (m_timeMaxMs - m_timeMinMs) / 8);

    for (const auto &feed : m_feeds) {
        if (!feed.enabled)
            continue;
        m_totalPlacemarks += feed.placemarks.size();

        for (const auto &pm : feed.placemarks) {
            if (!pm.visible)
                continue;
            if (!pm.folderPath.isEmpty()) {
                const QString key = feed.id + QStringLiteral("|") + pm.folderPath;
                if (!m_folderVisibility.value(key, true))
                    continue;
            }
            if (filterTime && pm.timeStart.isValid()) {
                const qint64 startMs = pm.timeStart.toMSecsSinceEpoch();
                const qint64 endMs = pm.timeEnd.isValid() ? pm.timeEnd.toMSecsSinceEpoch() : startMs;
                if (startMs > cursorMs)
                    continue;
                if (endMs < cursorMs - windowMs && m_timelineProgress < 0.999)
                    continue;
            }
            m_visiblePlacemarks.append(pm);
        }
    }

    ++m_kmlRevision;
    emit kmlDataChanged();
}

void Backend::updateSearch(const QString &query) {
    m_pendingSearchQuery = query.trimmed();
    m_remoteSearchResults.clear();
    if (m_pendingSearchQuery.isEmpty()) {
        m_searchDebounce.stop();
        m_localSearchResults.clear();
        m_searchResults.clear();
        m_searching = false;
        emit searchResultsChanged();
        return;
    }

    runLocalSearch(m_pendingSearchQuery);
    m_searchDebounce.start();
}

void Backend::runLocalSearch(const QString &query) {
    m_localSearchResults.clear();
    const QString q = query.toLower();

    // 1. Loaded KML placemarks first so users can jump straight to any event.
    int kmlHits = 0;
    for (const auto &pm : m_visiblePlacemarks) {
        if (pm.name.toLower().contains(q) || pm.folderPath.toLower().contains(q) ||
            pm.descriptionText.toLower().contains(q)) {
            const GeoCoord c = pm.anchor();
            QVariantMap m;
            m.insert(QStringLiteral("kind"), QStringLiteral("kml"));
            m.insert(QStringLiteral("id"), pm.id);
            m.insert(QStringLiteral("title"), pm.name);
            m.insert(QStringLiteral("subtitle"), pm.folderPath.isEmpty() ? tr("KML Feature") : pm.folderPath);
            m.insert(QStringLiteral("lat"), c.lat);
            m.insert(QStringLiteral("lon"), c.lon);
            m.insert(QStringLiteral("color"), pm.style.iconColor.name());
            m_localSearchResults.append(m);
            if (++kmlHits >= 12)
                break;
        }
    }

    // 2. Built-in offline places (cities, volcanoes, landmarks).
    int placeHits = 0;
    for (const auto &place : kBuiltinPlaces) {
        const QString name = QString::fromUtf8(place.name);
        const QString region = QString::fromUtf8(place.region);
        if (name.toLower().contains(q) || region.toLower().contains(q)) {
            QVariantMap m;
            m.insert(QStringLiteral("kind"), QStringLiteral("place"));
            m.insert(QStringLiteral("title"), name);
            m.insert(QStringLiteral("subtitle"), region);
            m.insert(QStringLiteral("lat"), place.lat);
            m.insert(QStringLiteral("lon"), place.lon);
            m_localSearchResults.append(m);
            if (++placeHits >= 8)
                break;
        }
    }

    m_searchResults = m_localSearchResults;
    emit searchResultsChanged();
}

void Backend::runPhotonSearch(const QString &query) {
    QUrl url(QStringLiteral("https://photon.komoot.io/api/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("6"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Needleeye/0.1 (Omarchy Linux)"));
    req.setTransferTimeout(8000);

    m_searching = true;
    emit searchResultsChanged();

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        reply->deleteLater();
        m_searching = false;
        if (query != m_pendingSearchQuery)
            return;

        m_remoteSearchResults.clear();
        if (reply->error() == QNetworkReply::NoError) {
            const auto features = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("features")).toArray();
            for (const auto &fv : features) {
                const auto obj = fv.toObject();
                const auto props = obj.value(QStringLiteral("properties")).toObject();
                const auto coords = obj.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("coordinates")).toArray();
                if (coords.size() < 2)
                    continue;
                const QString name = props.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    continue;
                QStringList subParts;
                const QString city = props.value(QStringLiteral("city")).toString();
                const QString state = props.value(QStringLiteral("state")).toString();
                const QString country = props.value(QStringLiteral("country")).toString();
                if (!city.isEmpty() && city != name) subParts << city;
                if (!state.isEmpty() && state != name) subParts << state;
                if (!country.isEmpty()) subParts << country;

                QVariantMap m;
                m.insert(QStringLiteral("kind"), QStringLiteral("geocode"));
                m.insert(QStringLiteral("title"), name);
                m.insert(QStringLiteral("subtitle"), subParts.join(QStringLiteral(", ")));
                m.insert(QStringLiteral("lon"), coords[0].toDouble());
                m.insert(QStringLiteral("lat"), coords[1].toDouble());
                m_remoteSearchResults.append(m);
            }
        }

        m_searchResults = m_localSearchResults + m_remoteSearchResults;
        emit searchResultsChanged();
    });
}

void Backend::activateSearchResult(int index) {
    if (index < 0 || index >= m_searchResults.size())
        return;
    const QVariantMap item = m_searchResults[index].toMap();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("kml")) {
        selectPlacemark(item.value(QStringLiteral("id")).toString(), true);
    } else {
        const double lat = item.value(QStringLiteral("lat")).toDouble();
        const double lon = item.value(QStringLiteral("lon")).toDouble();
        emit flyToRequested(lat, lon, 0.25);
    }
}

void Backend::openExternalLink(const QString &url) {
    const QUrl u(url);
    if (u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")))
        QDesktopServices::openUrl(u);
}

void Backend::setError(const QString &msg) {
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorChanged();
}

void Backend::setStatus(const QString &msg) {
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusChanged();
}

double Backend::rangeMetersToAltitudeRadii(double rangeMeters) const {
    const double radii = rangeMeters / 6371000.0;
    return std::clamp(radii, 0.004, 2.2);
}
