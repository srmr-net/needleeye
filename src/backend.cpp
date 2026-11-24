#include "backend.h"
#include "czmlparser.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
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

QString formatByteSize(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024LL)
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

KmlDocument parseGeospatialBytes(const QByteArray &data, const QString &sourceName, const QString &feedId) {
    if (CzmlParser::looksLikeCzml(data, sourceName))
        return CzmlParser::parseBytes(data, sourceName, feedId);
    return KmlParser::parseBytes(data, sourceName, feedId);
}

KmlDocument parseGeospatialFile(const QString &filePath, const QString &feedId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        KmlDocument doc;
        doc.error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return doc;
    }
    return parseGeospatialBytes(file.readAll(), QFileInfo(filePath).fileName(), feedId);
}

} // namespace

Backend::Backend(TileManager *tileManager, FilePicker *filePicker,
                 bool autoLoadDefaultFeeds, const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_tileManager(tileManager),
      m_filePicker(filePicker),
      m_settingsPath(settingsPath.isEmpty()
                         ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/needleeye/settings.json")
                         : settingsPath) {
    if (!m_tileManager) {
        m_tileManager = new TileManager({}, this);
        m_ownsTileManager = true;
    }
    if (!m_filePicker) {
        m_filePicker = new PortalFilePicker(this);
        m_ownsFilePicker = true;
    }

    m_spaceNav = new SpaceNavController(this);
    connect(m_spaceNav, &SpaceNavController::stateChanged, this, &Backend::spaceNavChanged);

    connect(m_tileManager, &TileManager::networkError, this, [this](const QString &msg) {
        setError(msg);
    });

    m_baseMaps = TileManager::defaultBaseMaps();
    loadSettings();
    syncBaseMapsToTileManager();
    ensureValidActiveBaseMap();
    refreshCacheStats();

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
        // Keep the live solar terminator advancing smoothly every minute.
        if (m_dayNightEnabled && !timelineActive())
            emit settingsChanged();
        if (!m_autoRefreshEnabled)
            return;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const int defaultSec = std::max(60, m_autoRefreshMinutes * 60);
        for (const auto &feed : m_feeds) {
            if (!feed.enabled || feed.isLocal || feed.loading)
                continue;
            const int interval = feed.refreshIntervalSec > 0 ? std::min(feed.refreshIntervalSec, defaultSec) : defaultSec;
            if (!feed.lastUpdated.isValid() || feed.lastUpdated.secsTo(now) >= interval)
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

void Backend::loadSettings() {
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("terrainEnabled")))
        m_terrainEnabled = root.value(QStringLiteral("terrainEnabled")).toBool(true);
    if (root.contains(QStringLiteral("terrainExaggeration")))
        m_terrainExaggeration = std::clamp(root.value(QStringLiteral("terrainExaggeration")).toDouble(1.0), 0.25, 4.0);
    if (root.contains(QStringLiteral("graticuleEnabled")))
        m_graticuleEnabled = root.value(QStringLiteral("graticuleEnabled")).toBool(true);
    if (root.contains(QStringLiteral("atmosphereEnabled")))
        m_atmosphereEnabled = root.value(QStringLiteral("atmosphereEnabled")).toBool(true);
    if (root.contains(QStringLiteral("dayNightEnabled")))
        m_dayNightEnabled = root.value(QStringLiteral("dayNightEnabled")).toBool(false);
    if (root.contains(QStringLiteral("rightDragZoom")))
        m_rightDragZoom = root.value(QStringLiteral("rightDragZoom")).toBool(false);
    if (root.contains(QStringLiteral("spaceNavSensitivity")))
        m_spaceNav->setSensitivity(root.value(QStringLiteral("spaceNavSensitivity")).toDouble(1.0));
    if (root.contains(QStringLiteral("spaceNavInvert")))
        m_spaceNav->setInvertAxes(root.value(QStringLiteral("spaceNavInvert")).toBool(false));
    if (root.contains(QStringLiteral("spaceNavEnabled")))
        m_spaceNav->setEnabled(root.value(QStringLiteral("spaceNavEnabled")).toBool(false));
    if (root.contains(QStringLiteral("autoRefreshEnabled")))
        m_autoRefreshEnabled = root.value(QStringLiteral("autoRefreshEnabled")).toBool(true);
    if (root.contains(QStringLiteral("autoRefreshMinutes")))
        m_autoRefreshMinutes = std::clamp(root.value(QStringLiteral("autoRefreshMinutes")).toInt(5), 1, 120);

    const QJsonArray mapsArr = root.value(QStringLiteral("baseMaps")).toArray();
    if (!mapsArr.isEmpty()) {
        QVector<BaseMapSpec> loaded;
        QSet<QString> seenIds;
        int nextUid = 0;

        for (const auto &v : mapsArr) {
            const QJsonObject obj = v.toObject();
            const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
            const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
            const QString url = obj.value(QStringLiteral("urlTemplate")).toString().trimmed();
            if (id.isEmpty() || name.isEmpty() || url.isEmpty() || seenIds.contains(id))
                continue;

            BaseMapSpec spec;
            spec.uid = nextUid++;
            spec.id = id;
            spec.name = name;
            spec.urlTemplate = url;
            spec.attribution = obj.value(QStringLiteral("attribution")).toString().trimmed();
            spec.maxZoom = std::clamp(obj.value(QStringLiteral("maxZoom")).toInt(18), 1, 20);
            spec.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
            spec.builtin = obj.value(QStringLiteral("builtin")).toBool(false);
            spec.projection = TileManager::projectionFromString(obj.value(QStringLiteral("projection")).toString());
            seenIds.insert(id);
            loaded.append(spec);
        }

        if (!loaded.isEmpty()) {
            // Append any newly introduced built-in maps missing from older configs.
            for (auto def : TileManager::defaultBaseMaps()) {
                if (!seenIds.contains(def.id)) {
                    def.uid = nextUid++;
                    loaded.append(def);
                }
            }
            m_baseMaps = loaded;
            m_nextBaseMapUid = std::max(100, nextUid + 1);
        }
    }

    const QString activeId = root.value(QStringLiteral("activeBaseMapId")).toString();
    const int activeIdx = findBaseMapIndexById(activeId);
    if (activeIdx >= 0 && m_baseMaps[activeIdx].enabled)
        m_activeBaseMapUid = m_baseMaps[activeIdx].uid;
}

void Backend::saveSettings() const {
    if (m_settingsPath.isEmpty())
        return;

    QJsonObject root;
    root.insert(QStringLiteral("activeBaseMapId"), baseLayerId());
    root.insert(QStringLiteral("terrainEnabled"), m_terrainEnabled);
    root.insert(QStringLiteral("terrainExaggeration"), m_terrainExaggeration);
    root.insert(QStringLiteral("graticuleEnabled"), m_graticuleEnabled);
    root.insert(QStringLiteral("atmosphereEnabled"), m_atmosphereEnabled);
    root.insert(QStringLiteral("dayNightEnabled"), m_dayNightEnabled);
    root.insert(QStringLiteral("rightDragZoom"), m_rightDragZoom);
    root.insert(QStringLiteral("spaceNavEnabled"), m_spaceNav ? m_spaceNav->enabled() : false);
    root.insert(QStringLiteral("spaceNavSensitivity"), m_spaceNav ? m_spaceNav->sensitivity() : 1.0);
    root.insert(QStringLiteral("spaceNavInvert"), m_spaceNav ? m_spaceNav->invertAxes() : false);
    root.insert(QStringLiteral("autoRefreshEnabled"), m_autoRefreshEnabled);
    root.insert(QStringLiteral("autoRefreshMinutes"), m_autoRefreshMinutes);

    QJsonArray mapsArr;
    for (const auto &m : m_baseMaps) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), m.id);
        obj.insert(QStringLiteral("name"), m.name);
        obj.insert(QStringLiteral("urlTemplate"), m.urlTemplate);
        obj.insert(QStringLiteral("attribution"), m.attribution);
        obj.insert(QStringLiteral("maxZoom"), m.maxZoom);
        obj.insert(QStringLiteral("enabled"), m.enabled);
        obj.insert(QStringLiteral("builtin"), m.builtin);
        obj.insert(QStringLiteral("projection"), TileManager::projectionToString(m.projection));
        mapsArr.append(obj);
    }
    root.insert(QStringLiteral("baseMaps"), mapsArr);

    // Save atomically beside target, then rename so crashes never corrupt settings.
    QDir().mkpath(QFileInfo(m_settingsPath).absolutePath());
    const QString tmpPath = m_settingsPath + QStringLiteral(".tmp");
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    tmp.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    tmp.close();
    QFile::remove(m_settingsPath);
    tmp.rename(m_settingsPath);
}

void Backend::syncBaseMapsToTileManager() {
    if (m_tileManager)
        m_tileManager->setBaseMaps(m_baseMaps);
}

void Backend::ensureValidActiveBaseMap() {
    const int idx = findBaseMapIndexByUid(m_activeBaseMapUid);
    if (idx >= 0 && m_baseMaps[idx].enabled)
        return;

    for (const auto &m : m_baseMaps) {
        if (m.enabled) {
            m_activeBaseMapUid = m.uid;
            return;
        }
    }
    if (!m_baseMaps.isEmpty()) {
        m_baseMaps[0].enabled = true;
        m_activeBaseMapUid = m_baseMaps[0].uid;
    }
}

int Backend::findBaseMapIndexById(const QString &mapId) const {
    for (int i = 0; i < m_baseMaps.size(); ++i)
        if (m_baseMaps[i].id == mapId)
            return i;
    return -1;
}

int Backend::findBaseMapIndexByUid(int uid) const {
    for (int i = 0; i < m_baseMaps.size(); ++i)
        if (m_baseMaps[i].uid == uid)
            return i;
    return -1;
}

QString Backend::makeUniqueBaseMapId(const QString &name) const {
    QString base = name.toLower().trimmed();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (base.isEmpty())
        base = QStringLiteral("custom-map");
    QString candidate = base;
    int suffix = 2;
    while (findBaseMapIndexById(candidate) >= 0)
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    return candidate;
}

QString Backend::validateBaseMapInput(const QString &name, const QString &urlTemplate,
                                      int maxZoom, const QString &projection) {
    if (name.trimmed().isEmpty())
        return tr("Base map name cannot be empty.");

    const QString tpl = urlTemplate.trimmed();
    const MapProjection proj = TileManager::projectionFromString(projection);

    if (proj == MapProjection::TiledXyz) {
        if (!tpl.startsWith(QStringLiteral("http://")) && !tpl.startsWith(QStringLiteral("https://")))
            return tr("URL template must start with http:// or https://.");

        if (!tpl.contains(QStringLiteral("{z}")) || !tpl.contains(QStringLiteral("{x}")) ||
            (!tpl.contains(QStringLiteral("{y}")) && !tpl.contains(QStringLiteral("{-y}")))) {
            return tr("URL template must include {z}, {x}, and {y} (or {-y}) placeholders.");
        }

        if (maxZoom < 1 || maxZoom > 20)
            return tr("Max zoom must be between 1 and 20.");
    } else {
        if (tpl.isEmpty())
            return tr("Enter an image URL (http/https) or local image file path.");
        const bool isRemote = tpl.startsWith(QStringLiteral("http://")) || tpl.startsWith(QStringLiteral("https://"));
        const QUrl u(tpl);
        const QString localPath = u.isLocalFile() ? u.toLocalFile() : tpl;
        if (!isRemote && !QFileInfo::exists(localPath))
            return tr("Image must be an http(s) URL or an existing local image file.");
    }

    return {};
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

void Backend::setBaseLayer(int layerUid) {
    const int idx = findBaseMapIndexByUid(layerUid);
    if (idx < 0 || m_activeBaseMapUid == layerUid)
        return;
    m_activeBaseMapUid = layerUid;
    saveSettings();
    emit settingsChanged();
    emit baseMapsChanged();
}

QString Backend::baseLayerId() const {
    const int idx = findBaseMapIndexByUid(m_activeBaseMapUid);
    return idx >= 0 ? m_baseMaps[idx].id : QString();
}

QString Backend::baseLayerName() const {
    const int idx = findBaseMapIndexByUid(m_activeBaseMapUid);
    if (idx < 0)
        return tr("Dark");
    const auto &m = m_baseMaps[idx];
    if (m.id == QLatin1String("dark-matter") && m.name == QLatin1String("Dark"))
        return tr("Dark");
    if (m.id == QLatin1String("esri-satellite") && m.name == QLatin1String("Satellite"))
        return tr("Satellite");
    return m.name;
}

QString Backend::baseLayerAttribution() const {
    const int idx = findBaseMapIndexByUid(m_activeBaseMapUid);
    return idx >= 0 ? m_baseMaps[idx].attribution : QString();
}

QVariantList Backend::baseMapsVariant() const {
    QVariantList list;
    list.reserve(m_baseMaps.size());
    int enabledSlot = 0;
    for (int i = 0; i < m_baseMaps.size(); ++i) {
        const auto &m = m_baseMaps[i];
        QVariantMap item;
        item.insert(QStringLiteral("uid"), m.uid);
        item.insert(QStringLiteral("id"), m.id);
        item.insert(QStringLiteral("name"), m.name);
        item.insert(QStringLiteral("urlTemplate"), m.urlTemplate);
        item.insert(QStringLiteral("attribution"), m.attribution);
        item.insert(QStringLiteral("maxZoom"), m.maxZoom);
        item.insert(QStringLiteral("enabled"), m.enabled);
        item.insert(QStringLiteral("builtin"), m.builtin);
        item.insert(QStringLiteral("projection"), TileManager::projectionToString(m.projection));
        item.insert(QStringLiteral("active"), m.uid == m_activeBaseMapUid);
        item.insert(QStringLiteral("slot"), m.enabled ? (++enabledSlot) : 0);
        item.insert(QStringLiteral("canMoveUp"), i > 0);
        item.insert(QStringLiteral("canMoveDown"), i + 1 < m_baseMaps.size());
        list.append(item);
    }
    return list;
}

QVariantList Backend::enabledBaseMapsVariant() const {
    QVariantList list;
    int slot = 0;
    for (const auto &m : m_baseMaps) {
        if (!m.enabled)
            continue;
        ++slot;
        QVariantMap item;
        item.insert(QStringLiteral("uid"), m.uid);
        item.insert(QStringLiteral("id"), m.id);
        item.insert(QStringLiteral("name"), m.name);
        item.insert(QStringLiteral("slot"), slot);
        item.insert(QStringLiteral("active"), m.uid == m_activeBaseMapUid);
        list.append(item);
    }
    return list;
}

void Backend::selectBaseMap(const QString &mapId) {
    const int idx = findBaseMapIndexById(mapId);
    if (idx < 0)
        return;
    if (!m_baseMaps[idx].enabled) {
        m_baseMaps[idx].enabled = true;
        syncBaseMapsToTileManager();
    }
    m_activeBaseMapUid = m_baseMaps[idx].uid;
    saveSettings();
    emit settingsChanged();
    emit baseMapsChanged();
}

bool Backend::selectBaseMapFlexible(const QString &idNameOrSlot) {
    const QString key = idNameOrSlot.trimmed();
    if (key.isEmpty())
        return false;

    // 1. Exact ID match.
    int idx = findBaseMapIndexById(key);
    if (idx >= 0) {
        selectBaseMap(m_baseMaps[idx].id);
        return true;
    }

    // 2. 1-based enabled slot number ("1".."9").
    bool isNum = false;
    const int slot = key.toInt(&isNum);
    if (isNum && slot >= 1) {
        int seen = 0;
        for (const auto &m : m_baseMaps) {
            if (!m.enabled)
                continue;
            if (++seen == slot) {
                setBaseLayer(m.uid);
                return true;
            }
        }
    }

    // 3. Case-insensitive name match (exact first, then substring).
    for (const auto &m : m_baseMaps) {
        if (m.name.compare(key, Qt::CaseInsensitive) == 0) {
            selectBaseMap(m.id);
            return true;
        }
    }
    for (const auto &m : m_baseMaps) {
        if (m.name.contains(key, Qt::CaseInsensitive) || m.id.contains(key, Qt::CaseInsensitive)) {
            selectBaseMap(m.id);
            return true;
        }
    }
    return false;
}

void Backend::selectBaseMapBySlot(int slotIndex) {
    if (slotIndex < 0)
        return;
    int seen = 0;
    for (const auto &m : m_baseMaps) {
        if (!m.enabled)
            continue;
        if (seen == slotIndex) {
            setBaseLayer(m.uid);
            return;
        }
        ++seen;
    }
}

void Backend::cycleBaseMap() {
    QVector<int> enabledUids;
    for (const auto &m : m_baseMaps)
        if (m.enabled)
            enabledUids.append(m.uid);
    if (enabledUids.size() <= 1)
        return;
    const int pos = enabledUids.indexOf(m_activeBaseMapUid);
    const int nextUid = enabledUids[(pos + 1) % enabledUids.size()];
    setBaseLayer(nextUid);
}

bool Backend::setBaseMapEnabled(const QString &mapId, bool enabled) {
    const int idx = findBaseMapIndexById(mapId);
    if (idx < 0)
        return false;
    if (m_baseMaps[idx].enabled == enabled)
        return true;

    // Never leave the globe with zero enabled base maps.
    if (!enabled) {
        int enabledCount = 0;
        for (const auto &m : m_baseMaps)
            if (m.enabled)
                ++enabledCount;
        if (enabledCount <= 1) {
            setError(tr("At least one base map must remain enabled."));
            return false;
        }
    }

    m_baseMaps[idx].enabled = enabled;
    ensureValidActiveBaseMap();
    syncBaseMapsToTileManager();
    saveSettings();
    emit baseMapsChanged();
    emit settingsChanged();
    return true;
}

bool Backend::moveBaseMap(const QString &mapId, int delta) {
    const int idx = findBaseMapIndexById(mapId);
    const int target = idx + delta;
    if (idx < 0 || target < 0 || target >= m_baseMaps.size())
        return false;
    std::swap(m_baseMaps[idx], m_baseMaps[target]);
    saveSettings();
    emit baseMapsChanged();
    return true;
}

QString Backend::addCustomBaseMap(const QString &name, const QString &urlTemplate,
                                  int maxZoom, const QString &attribution,
                                  const QString &projection) {
    const QString err = validateBaseMapInput(name, urlTemplate, maxZoom, projection);
    if (!err.isEmpty())
        return err;

    BaseMapSpec spec;
    spec.uid = m_nextBaseMapUid++;
    spec.id = makeUniqueBaseMapId(name);
    spec.name = name.trimmed();
    spec.urlTemplate = urlTemplate.trimmed();
    spec.attribution = attribution.trimmed();
    spec.projection = TileManager::projectionFromString(projection);
    spec.maxZoom = spec.projection == MapProjection::TiledXyz ? std::clamp(maxZoom, 1, 20) : 10;
    spec.enabled = true;
    spec.builtin = false;

    m_baseMaps.append(spec);
    m_activeBaseMapUid = spec.uid;
    syncBaseMapsToTileManager();
    saveSettings();
    emit baseMapsChanged();
    emit settingsChanged();
    setStatus(tr("Added base map: %1").arg(spec.name));
    return {};
}

QString Backend::updateBaseMap(const QString &mapId, const QString &name,
                               const QString &urlTemplate, int maxZoom,
                               const QString &attribution,
                               const QString &projection) {
    const int idx = findBaseMapIndexById(mapId);
    if (idx < 0)
        return tr("Base map not found.");

    const QString err = validateBaseMapInput(name, urlTemplate, maxZoom, projection);
    if (!err.isEmpty())
        return err;

    const MapProjection proj = TileManager::projectionFromString(projection);
    const bool urlChanged = (m_baseMaps[idx].urlTemplate != urlTemplate.trimmed()) ||
                            (m_baseMaps[idx].projection != proj);
    m_baseMaps[idx].name = name.trimmed();
    m_baseMaps[idx].urlTemplate = urlTemplate.trimmed();
    m_baseMaps[idx].projection = proj;
    m_baseMaps[idx].maxZoom = proj == MapProjection::TiledXyz ? std::clamp(maxZoom, 1, 20) : 10;
    m_baseMaps[idx].attribution = attribution.trimmed();

    syncBaseMapsToTileManager();
    if (urlChanged && m_tileManager)
        m_tileManager->invalidateLayerCache(m_baseMaps[idx].uid);
    saveSettings();
    emit baseMapsChanged();
    emit settingsChanged();
    refreshCacheStats();
    return {};
}

void Backend::browseBaseMapImage() {
    m_filePicker->openImageFile(tr("Select world map image"), [this](const QString &path, const QString &error) {
        if (!error.isEmpty()) {
            setError(error);
            return;
        }
        if (!path.isEmpty())
            emit baseMapImagePicked(path);
    });
}

bool Backend::removeBaseMap(const QString &mapId) {
    const int idx = findBaseMapIndexById(mapId);
    if (idx < 0 || m_baseMaps[idx].builtin)
        return false;

    const int removedUid = m_baseMaps[idx].uid;
    m_baseMaps.removeAt(idx);
    ensureValidActiveBaseMap();
    syncBaseMapsToTileManager();
    if (m_tileManager)
        m_tileManager->invalidateLayerCache(removedUid);
    saveSettings();
    emit baseMapsChanged();
    emit settingsChanged();
    refreshCacheStats();
    return true;
}

void Backend::resetBaseMapsToDefaults() {
    m_baseMaps = TileManager::defaultBaseMaps();
    m_activeBaseMapUid = m_baseMaps.first().uid;
    syncBaseMapsToTileManager();
    saveSettings();
    emit baseMapsChanged();
    emit settingsChanged();
    setStatus(tr("Base maps reset to defaults."));
}

void Backend::refreshCacheStats() {
    if (!m_tileManager)
        return;
    const CacheStats stats = m_tileManager->diskCacheStats();
    const QString summary = stats.fileCount == 0
        ? tr("Cache empty")
        : tr("%1 (%n tiles)", "", stats.fileCount).arg(formatByteSize(stats.bytes));
    if (summary != m_cacheSummary) {
        m_cacheSummary = summary;
        emit cacheStatsChanged();
    }
}

void Backend::clearTileCache() {
    if (!m_tileManager)
        return;
    m_tileManager->clearAllCache();
    refreshCacheStats();
    emit settingsChanged();
    setStatus(tr("Tile cache cleared."));
}

void Backend::setTerrainEnabled(bool enabled) {
    if (m_terrainEnabled == enabled)
        return;
    m_terrainEnabled = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setTerrainExaggeration(double value) {
    value = std::clamp(value, 0.25, 4.0);
    if (std::abs(m_terrainExaggeration - value) < 1e-4)
        return;
    m_terrainExaggeration = value;
    saveSettings();
    emit settingsChanged();
}

void Backend::setGraticuleEnabled(bool enabled) {
    if (m_graticuleEnabled == enabled)
        return;
    m_graticuleEnabled = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setAtmosphereEnabled(bool enabled) {
    if (m_atmosphereEnabled == enabled)
        return;
    m_atmosphereEnabled = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setDayNightEnabled(bool enabled) {
    if (m_dayNightEnabled == enabled)
        return;
    m_dayNightEnabled = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setRightDragZoom(bool enabled) {
    if (m_rightDragZoom == enabled)
        return;
    m_rightDragZoom = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setHudHidden(bool hidden) {
    if (m_hudHidden == hidden)
        return;
    m_hudHidden = hidden;
    emit hudChanged();
}

void Backend::setSidebarVisible(bool visible) {
    if (m_sidebarVisible == visible)
        return;
    m_sidebarVisible = visible;
    emit hudChanged();
}

bool Backend::spaceNavEnabled() const {
    return m_spaceNav && m_spaceNav->enabled();
}

void Backend::setSpaceNavEnabled(bool enabled) {
    if (!m_spaceNav || m_spaceNav->enabled() == enabled)
        return;
    m_spaceNav->setEnabled(enabled);
    saveSettings();
    emit spaceNavChanged();
}

double Backend::spaceNavSensitivity() const {
    return m_spaceNav ? m_spaceNav->sensitivity() : 1.0;
}

void Backend::setSpaceNavSensitivity(double value) {
    if (!m_spaceNav)
        return;
    m_spaceNav->setSensitivity(value);
    saveSettings();
    emit spaceNavChanged();
}

bool Backend::spaceNavInvert() const {
    return m_spaceNav && m_spaceNav->invertAxes();
}

void Backend::setSpaceNavInvert(bool invert) {
    if (!m_spaceNav || m_spaceNav->invertAxes() == invert)
        return;
    m_spaceNav->setInvertAxes(invert);
    saveSettings();
    emit spaceNavChanged();
}

bool Backend::spaceNavConnected() const {
    return m_spaceNav && m_spaceNav->connected();
}

QString Backend::spaceNavStatus() const {
    return m_spaceNav ? m_spaceNav->statusText() : QString();
}

QVector3D Backend::computeSunDirection(const QDateTime &utcTime,
                                       double *outSubsolarLatDeg,
                                       double *outSubsolarLonDeg) {
    // Low-precision astronomical solar position (accurate within ~0.01 deg).
    const double unixSec = static_cast<double>(utcTime.toMSecsSinceEpoch()) / 1000.0;
    const double jd = unixSec / 86400.0 + 2440587.5;
    const double n = jd - 2451545.0;

    auto norm360 = [](double deg) {
        deg = std::fmod(deg, 360.0);
        return deg < 0.0 ? deg + 360.0 : deg;
    };

    const double meanLon = norm360(280.460 + 0.9856474 * n);
    const double meanAnomRad = norm360(357.528 + 0.9856003 * n) * M_PI / 180.0;
    const double eclLonRad = norm360(meanLon + 1.915 * std::sin(meanAnomRad) + 0.020 * std::sin(2.0 * meanAnomRad)) * M_PI / 180.0;
    const double obliqRad = (23.439 - 0.0000004 * n) * M_PI / 180.0;

    const double sinDecl = std::sin(obliqRad) * std::sin(eclLonRad);
    const double declRad = std::asin(std::clamp(sinDecl, -1.0, 1.0));
    const double raRad = std::atan2(std::cos(obliqRad) * std::sin(eclLonRad), std::cos(eclLonRad));

    const double gmstDeg = norm360(280.46061837 + 360.98564736629 * n);
    double subLonDeg = std::fmod(raRad * 180.0 / M_PI - gmstDeg + 540.0, 360.0) - 180.0;
    const double subLatDeg = declRad * 180.0 / M_PI;

    if (outSubsolarLatDeg)
        *outSubsolarLatDeg = subLatDeg;
    if (outSubsolarLonDeg)
        *outSubsolarLonDeg = subLonDeg;

    const double lonRad = subLonDeg * M_PI / 180.0;
    return QVector3D(
        static_cast<float>(std::cos(declRad) * std::sin(lonRad)),
        static_cast<float>(std::sin(declRad)),
        static_cast<float>(std::cos(declRad) * std::cos(lonRad))).normalized();
}

QVector3D Backend::sunDirection() const {
    if (timelineActive()) {
        const qint64 cur = m_timeMinMs + static_cast<qint64>((m_timeMaxMs - m_timeMinMs) * m_timelineProgress);
        return computeSunDirection(QDateTime::fromMSecsSinceEpoch(cur, QTimeZone::utc()));
    }
    return computeSunDirection(QDateTime::currentDateTimeUtc());
}

void Backend::setAutoRefreshEnabled(bool enabled) {
    if (m_autoRefreshEnabled == enabled)
        return;
    m_autoRefreshEnabled = enabled;
    saveSettings();
    emit settingsChanged();
}

void Backend::setAutoRefreshMinutes(int minutes) {
    minutes = std::clamp(minutes, 1, 120);
    if (m_autoRefreshMinutes == minutes)
        return;
    m_autoRefreshMinutes = minutes;
    saveSettings();
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

    auto buildMap = [](const KmlPlacemark &pm, const QString &feedName) {
        const GeoCoord c = pm.anchor();
        QVariantMap m;
        m.insert(QStringLiteral("id"), pm.id);
        m.insert(QStringLiteral("name"), pm.name);
        m.insert(QStringLiteral("feedName"), feedName);
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
    };

    // Prefer the live interpolated version from m_visiblePlacemarks when available.
    for (const auto &pm : m_visiblePlacemarks) {
        if (pm.id != m_selectedId)
            continue;
        const int fIdx = findFeedIndex(pm.feedId);
        const QString feedName = fIdx >= 0 ? m_feeds[fIdx].name : QString();
        return buildMap(pm, feedName);
    }

    for (const auto &feed : m_feeds) {
        for (const auto &pm : feed.placemarks) {
            if (pm.id == m_selectedId)
                return buildMap(pm, feed.name);
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
    setFeedEnabled(feedId, !m_feeds[idx].enabled);
}

bool Backend::setFeedEnabled(const QString &feedIdOrName, bool enabled) {
    int idx = findFeedIndex(feedIdOrName);
    if (idx < 0) {
        for (int i = 0; i < m_feeds.size(); ++i) {
            if (m_feeds[i].name.compare(feedIdOrName, Qt::CaseInsensitive) == 0 ||
                m_feeds[i].name.contains(feedIdOrName, Qt::CaseInsensitive)) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0)
        return false;
    if (m_feeds[idx].enabled == enabled)
        return true;

    m_feeds[idx].enabled = enabled;
    emit feedsChanged();
    if (m_feeds[idx].enabled && m_feeds[idx].placemarks.isEmpty() && !m_feeds[idx].isLocal) {
        refreshFeed(m_feeds[idx].id);
    } else {
        recomputeTimeRange();
        rebuildVisiblePlacemarks();
        emit foldersChanged();
    }
    return true;
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
        KmlDocument doc = parseGeospatialFile(feed.source, feed.id);
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
        KmlDocument doc = parseGeospatialBytes(payload, url.fileName().isEmpty() ? m_feeds[idx].name : url.fileName(), feedId);
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

    KmlDocument doc = parseGeospatialBytes(data, sourceName, feedId);
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

QString Backend::addFeedUrl(const QString &url, const QString &name) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return {};

    const QUrl parsed(trimmed);
    if (!parsed.isValid() || (parsed.scheme() != QLatin1String("http") && parsed.scheme() != QLatin1String("https"))) {
        setError(tr("Enter a valid http:// or https:// KML, KMZ, or CZML URL."));
        return {};
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
    return id;
}

bool Backend::removeFeed(const QString &feedId) {
    const int idx = findFeedIndex(feedId);
    if (idx < 0)
        return false;
    m_feeds.removeAt(idx);
    if (!m_selectedId.isEmpty() && m_selectedId.startsWith(feedId + QStringLiteral(":"))) {
        m_selectedId.clear();
        emit selectionChanged();
    }
    recomputeTimeRange();
    rebuildVisiblePlacemarks();
    emit feedsChanged();
    emit foldersChanged();
    return true;
}

void Backend::openLocalFileDialog() {
    m_filePicker->openKmlFile(tr("Open KML, KMZ, or CZML file"), [this](const QString &path, const QString &error) {
        if (!error.isEmpty()) {
            setError(error);
            return;
        }
        if (!path.isEmpty())
            loadLocalFile(path);
    });
}

bool Backend::loadLocalFile(const QString &filePath, const QString &displayName) {
    const QString id = QStringLiteral("file-%1").arg(QDateTime::currentMSecsSinceEpoch());
    KmlDocument doc = parseGeospatialFile(filePath, id);
    if (!doc.error.isEmpty()) {
        setError(doc.error);
        return false;
    }

    FeedEntry entry;
    entry.id = id;
    entry.name = !displayName.trimmed().isEmpty()
        ? displayName.trimmed()
        : (doc.name.isEmpty() ? QFileInfo(filePath).fileName() : doc.name);
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

QString Backend::findPlacemarkId(const QString &idOrName) const {
    const QString q = idOrName.trimmed();
    if (q.isEmpty())
        return {};
    for (const auto &pm : m_visiblePlacemarks) {
        if (pm.id == q)
            return pm.id;
    }
    for (const auto &pm : m_visiblePlacemarks) {
        if (pm.name.compare(q, Qt::CaseInsensitive) == 0)
            return pm.id;
    }
    for (const auto &pm : m_visiblePlacemarks) {
        if (pm.name.contains(q, Qt::CaseInsensitive) || pm.id.endsWith(QStringLiteral(":") + q))
            return pm.id;
    }
    for (const auto &feed : m_feeds) {
        for (const auto &pm : feed.placemarks) {
            if (pm.id == q || pm.name.compare(q, Qt::CaseInsensitive) == 0)
                return pm.id;
        }
    }
    return {};
}

bool Backend::selectPlacemark(const QString &placemarkId, bool flyTo) {
    if (placemarkId.isEmpty()) {
        clearSelection();
        return true;
    }

    const QString resolvedId = findPlacemarkId(placemarkId);
    if (resolvedId.isEmpty())
        return false;

    for (const auto &feed : m_feeds) {
        for (const auto &pm : feed.placemarks) {
            if (pm.id != resolvedId)
                continue;
            m_selectedId = resolvedId;
            emit selectionChanged();
            if (flyTo) {
                const GeoCoord c = pm.anchor();
                const double alt = pm.lookAt.valid ? rangeMetersToAltitudeRadii(pm.lookAt.range) : 0.35;
                emit flyToRequested(c.lat, c.lon, alt);
            }
            return true;
        }
    }
    return false;
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
    const qint64 windowMs = std::max<qint64>(3600 * 1000 * 12, (m_timeMaxMs - m_timeMinMs) / 8);

    bool selectedMoved = false;
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
            KmlPlacemark item = pm;
            if (filterTime && item.timeStart.isValid()) {
                const qint64 startMs = item.timeStart.toMSecsSinceEpoch();
                const qint64 endMs = item.timeEnd.isValid() ? item.timeEnd.toMSecsSinceEpoch() : startMs;
                if (!item.samples.isEmpty()) {
                    // Time-sampled CZML/track entities stay visible across their availability
                    // interval and interpolate their live position smoothly along the path.
                    if (cursorMs < startMs || cursorMs > endMs)
                        continue;
                    item.point = item.positionAt(cursorMs);
                    item.hasPoint = true;
                    if (item.id == m_selectedId)
                        selectedMoved = true;
                } else {
                    if (startMs > cursorMs)
                        continue;
                    if (endMs < cursorMs - windowMs && m_timelineProgress < 0.999)
                        continue;
                }
            }
            m_visiblePlacemarks.append(item);
        }
    }

    ++m_kmlRevision;
    emit kmlDataChanged();
    if (selectedMoved)
        emit selectionChanged();
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
