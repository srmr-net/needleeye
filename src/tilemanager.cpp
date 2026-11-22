#include "tilemanager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

TileManager::TileManager(const QString &cacheDir, QObject *parent)
    : QObject(parent),
      m_cacheDir(cacheDir.isEmpty()
                     ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles")
                     : cacheDir),
      m_memCache(512) {
    QDir().mkpath(m_cacheDir);
    setBaseMaps(defaultBaseMaps());
}

QVector<BaseMapSpec> TileManager::defaultBaseMaps() {
    return {
        {0, QStringLiteral("dark-matter"), QStringLiteral("Dark"),
         QStringLiteral("https://basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png"),
         QStringLiteral("© OpenStreetMap, © CARTO"), 18, true, true},
        {1, QStringLiteral("esri-satellite"), QStringLiteral("Satellite"),
         QStringLiteral("https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
         QStringLiteral("© Esri, Maxar, Earthstar Geographics"), 17, true, true},
        {2, QStringLiteral("sentinel-2"), QStringLiteral("Sentinel-2"),
         QStringLiteral("https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2021_3857/default/GoogleMapsCompatible/{z}/{y}/{x}.jpg"),
         QStringLiteral("Sentinel-2 cloudless by EOX (CC BY 4.0)"), 13, true, true},
        {3, QStringLiteral("osm"), QStringLiteral("OSM"),
         QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png"),
         QStringLiteral("© OpenStreetMap contributors"), 18, true, true},
        {4, QStringLiteral("opentopomap"), QStringLiteral("OpenTopoMap"),
         QStringLiteral("https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png"),
         QStringLiteral("© OpenTopoMap (CC-BY-SA)"), 17, false, true},
        {5, QStringLiteral("carto-voyager"), QStringLiteral("Voyager"),
         QStringLiteral("https://basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png"),
         QStringLiteral("© OpenStreetMap, © CARTO"), 18, false, true},
        {6, QStringLiteral("esri-topo"), QStringLiteral("Esri Topo"),
         QStringLiteral("https://services.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}"),
         QStringLiteral("© Esri, HERE, Garmin, OpenStreetMap"), 17, false, true},
    };
}

void TileManager::setBaseMaps(const QVector<BaseMapSpec> &maps) {
    QMutexLocker lock(&m_cacheMutex);
    m_layers.clear();
    for (const auto &m : maps)
        m_layers.insert(m.uid, m);
}

void TileManager::invalidateLayerCache(int layerUid) {
    QString slug;
    {
        QMutexLocker lock(&m_cacheMutex);
        slug = m_layers.contains(layerUid) ? m_layers.value(layerUid).id : QString::number(layerUid);
        const auto keys = m_memCache.keys();
        for (const auto &k : keys) {
            if (k.layer == layerUid)
                m_memCache.remove(k);
        }
    }

    // Clear failed state for this layer so edited URLs retry immediately.
    for (auto it = m_failed.begin(); it != m_failed.end(); ) {
        if (it->layer == layerUid)
            it = m_failed.erase(it);
        else
            ++it;
    }

    if (!slug.isEmpty())
        QDir(m_cacheDir + QStringLiteral("/") + slug).removeRecursively();

    ++m_cacheRevision;
    emit cacheCleared();
}

void TileManager::clearAllCache() {
    {
        QMutexLocker lock(&m_cacheMutex);
        m_memCache.clear();
    }
    m_failed.clear();
    m_queue.clear();
    m_queuedSet.clear();

    QDir dir(m_cacheDir);
    if (dir.exists()) {
        const auto entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QString &entry : entries) {
            const QString full = dir.filePath(entry);
            QFileInfo fi(full);
            if (fi.isDir())
                QDir(full).removeRecursively();
            else
                QFile::remove(full);
        }
    }

    ++m_cacheRevision;
    emit cacheCleared();
}

CacheStats TileManager::diskCacheStats() const {
    CacheStats stats;
    QDirIterator it(m_cacheDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        stats.bytes += it.fileInfo().size();
        ++stats.fileCount;
    }
    return stats;
}

QString TileManager::expandUrlTemplate(const QString &tpl, int z, int x, int y) {
    const int n = 1 << z;
    const int tmsY = (n - 1) - y;
    static const char subdomains[3] = {'a', 'b', 'c'};
    const QChar sub(subdomains[(std::abs(x) + std::abs(y)) % 3]);

    QString url = tpl;
    url.replace(QStringLiteral("{z}"), QString::number(z));
    url.replace(QStringLiteral("{x}"), QString::number(x));
    url.replace(QStringLiteral("{-y}"), QString::number(tmsY));
    url.replace(QStringLiteral("{y}"), QString::number(y));
    url.replace(QStringLiteral("{s}"), QString(sub));
    return url;
}

QString TileManager::layerSlug(int layer) const {
    if (layer == ElevationDemLayer)
        return QStringLiteral("terrarium");
    QMutexLocker lock(&m_cacheMutex);
    auto it = m_layers.constFind(layer);
    if (it != m_layers.cend() && !it.value().id.isEmpty())
        return it.value().id;
    return QStringLiteral("layer_%1").arg(layer);
}

int TileManager::maxZoomForLayer(int layer) const {
    if (layer == ElevationDemLayer)
        return 12;
    QMutexLocker lock(&m_cacheMutex);
    auto it = m_layers.constFind(layer);
    if (it != m_layers.cend())
        return std::clamp(it.value().maxZoom, 1, 20);
    return 16;
}

QString TileManager::tileUrl(int layer, int z, int x, int y) const {
    if (layer == ElevationDemLayer) {
        return QStringLiteral("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png")
            .arg(z).arg(x).arg(y);
    }
    QString tpl;
    {
        QMutexLocker lock(&m_cacheMutex);
        auto it = m_layers.constFind(layer);
        if (it == m_layers.cend())
            return {};
        tpl = it.value().urlTemplate;
    }
    return expandUrlTemplate(tpl, z, x, y);
}

QString TileManager::diskPath(const TileKey &key) const {
    return QStringLiteral("%1/%2/%3/%4/%5.tile")
        .arg(m_cacheDir, layerSlug(key.layer))
        .arg(key.z).arg(key.x).arg(key.y);
}

QImage TileManager::cachedTile(int layer, int z, int x, int y) const {
    const TileKey key{layer, z, x, y};
    QMutexLocker lock(&m_cacheMutex);
    if (auto *img = m_memCache.object(key))
        return *img;
    return {};
}

QImage TileManager::requestTile(int layer, int z, int x, int y) {
    if (z < 0 || z > maxZoomForLayer(layer))
        return {};
    const int n = 1 << z;
    x = ((x % n) + n) % n;
    if (y < 0 || y >= n)
        return {};

    const TileKey key{layer, z, x, y};
    {
        QMutexLocker lock(&m_cacheMutex);
        if (auto *img = m_memCache.object(key))
            return *img;
    }

    const QString path = diskPath(key);
    if (QFileInfo::exists(path)) {
        QImage img;
        if (img.load(path)) {
            QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
            QMutexLocker lock(&m_cacheMutex);
            m_memCache.insert(key, new QImage(rgba));
            return rgba;
        }
    }

    if (!m_inFlight.contains(key) && !m_queuedSet.contains(key) && !m_failed.contains(key)) {
        if (m_queue.size() > 128) {
            const TileKey dropped = m_queue.dequeue();
            m_queuedSet.remove(dropped);
        }
        m_queue.enqueue(key);
        m_queuedSet.insert(key);
        pumpQueue();
    }
    return {};
}

void TileManager::pumpQueue() {
    while (m_inFlight.size() < MaxConcurrent && !m_queue.isEmpty()) {
        const TileKey key = m_queue.dequeue();
        m_queuedSet.remove(key);
        {
            QMutexLocker lock(&m_cacheMutex);
            if (m_memCache.contains(key))
                continue;
        }
        if (m_inFlight.contains(key))
            continue;

        const QUrl url(tileUrl(key.layer, key.z, key.x, key.y));
        if (!url.isValid())
            continue;

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Needleeye/0.1 (Omarchy Linux)"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(12000);

        m_inFlight.insert(key);
        QNetworkReply *reply = m_nam.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
            reply->deleteLater();
            m_inFlight.remove(key);

            if (reply->error() != QNetworkReply::NoError) {
                m_failed.insert(key);
                if (key.z <= 2)
                    emit networkError(tr("Tile load failed (%1): %2").arg(layerSlug(key.layer), reply->errorString()));
                pumpQueue();
                return;
            }

            const QByteArray bytes = reply->readAll();
            QImage img;
            if (!img.loadFromData(bytes)) {
                m_failed.insert(key);
                pumpQueue();
                return;
            }

            const QString path = diskPath(key);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile tmp(path + QStringLiteral(".tmp"));
            if (tmp.open(QIODevice::WriteOnly)) {
                tmp.write(bytes);
                tmp.close();
                QFile::remove(path);
                tmp.rename(path);
            }

            QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
            {
                QMutexLocker lock(&m_cacheMutex);
                m_memCache.insert(key, new QImage(rgba));
            }
            emit tileReady(key.layer, key.z, key.x, key.y);
            pumpQueue();
        });
    }
}

double TileManager::sampleElevationMeters(double latDeg, double lonDeg, int preferredZoom) const {
    const double clampedLat = std::clamp(latDeg, -85.05112878, 85.05112878);
    const double latRad = clampedLat * M_PI / 180.0;

    for (int z = std::min(preferredZoom, 10); z >= 0; --z) {
        const double n = static_cast<double>(1 << z);
        const double fx = (lonDeg + 180.0) / 360.0 * n;
        const double fy = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) * 0.5 * n;
        const int tx = ((static_cast<int>(std::floor(fx)) % (1 << z)) + (1 << z)) % (1 << z);
        const int ty = std::clamp(static_cast<int>(std::floor(fy)), 0, (1 << z) - 1);

        const QImage tile = cachedTile(ElevationDemLayer, z, tx, ty);
        if (tile.isNull())
            continue;

        const int px = std::clamp(static_cast<int>((fx - std::floor(fx)) * tile.width()), 0, tile.width() - 1);
        const int py = std::clamp(static_cast<int>((fy - std::floor(fy)) * tile.height()), 0, tile.height() - 1);
        const QRgb rgb = tile.pixel(px, py);
        // AWS Terrarium encoding: meters = (R * 256 + G + B / 256) - 32768
        const double meters = (qRed(rgb) * 256.0 + qGreen(rgb) + qBlue(rgb) / 256.0) - 32768.0;
        return std::max(0.0, meters);
    }
    return 0.0;
}
