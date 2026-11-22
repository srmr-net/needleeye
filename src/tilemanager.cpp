#include "tilemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <cmath>

TileManager::TileManager(const QString &cacheDir, QObject *parent)
    : QObject(parent),
      m_cacheDir(cacheDir.isEmpty()
                     ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles")
                     : cacheDir),
      m_memCache(512) {
    QDir().mkpath(m_cacheDir);
}

QString TileManager::layerSlug(int layer) {
    switch (static_cast<BaseLayer>(layer)) {
    case BaseLayer::DarkMatter:    return QStringLiteral("dark");
    case BaseLayer::EsriSatellite: return QStringLiteral("esri");
    case BaseLayer::Sentinel2:     return QStringLiteral("sentinel2");
    case BaseLayer::OpenStreetMap: return QStringLiteral("osm");
    case BaseLayer::ElevationDem:  return QStringLiteral("terrarium");
    }
    return QStringLiteral("dark");
}

int TileManager::maxZoomForLayer(int layer) {
    switch (static_cast<BaseLayer>(layer)) {
    case BaseLayer::DarkMatter:    return 18;
    case BaseLayer::EsriSatellite: return 17;
    case BaseLayer::Sentinel2:     return 13;
    case BaseLayer::OpenStreetMap: return 18;
    case BaseLayer::ElevationDem:  return 12;
    }
    return 16;
}

QString TileManager::tileUrl(int layer, int z, int x, int y) {
    switch (static_cast<BaseLayer>(layer)) {
    case BaseLayer::DarkMatter:
        return QStringLiteral("https://basemaps.cartocdn.com/dark_all/%1/%2/%3.png").arg(z).arg(x).arg(y);
    case BaseLayer::EsriSatellite:
        return QStringLiteral("https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3")
            .arg(z).arg(y).arg(x);
    case BaseLayer::Sentinel2:
        return QStringLiteral("https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2021_3857/default/GoogleMapsCompatible/%1/%2/%3.jpg")
            .arg(z).arg(y).arg(x);
    case BaseLayer::OpenStreetMap:
        return QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y);
    case BaseLayer::ElevationDem:
        return QStringLiteral("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png").arg(z).arg(x).arg(y);
    }
    return {};
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
        // Cap queue length by prioritizing the most recent camera view.
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

        const QImage tile = cachedTile(static_cast<int>(BaseLayer::ElevationDem), z, tx, ty);
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
