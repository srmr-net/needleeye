#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

struct TileKey {
    int layer = 0;
    int z = 0;
    int x = 0;
    int y = 0;

    bool operator==(const TileKey &other) const {
        return layer == other.layer && z == other.z && x == other.x && y == other.y;
    }
};

inline size_t qHash(const TileKey &k, size_t seed = 0) {
    return qHashMulti(seed, k.layer, k.z, k.x, k.y);
}

enum class BaseLayer : int {
    DarkMatter = 0,
    EsriSatellite = 1,
    Sentinel2 = 2,
    OpenStreetMap = 3,
    ElevationDem = 100
};

class TileManager : public QObject {
    Q_OBJECT
public:
    explicit TileManager(const QString &cacheDir = {}, QObject *parent = nullptr);

    // Returns cached image if immediately available; otherwise queues async fetch.
    QImage requestTile(int layer, int z, int x, int y);
    QImage cachedTile(int layer, int z, int x, int y) const;

    // Decodes elevation in meters from cached AWS Terrarium DEM tiles (returns 0 if unavailable).
    double sampleElevationMeters(double latDeg, double lonDeg, int preferredZoom = 6) const;

    static QString tileUrl(int layer, int z, int x, int y);
    static QString layerSlug(int layer);
    static int maxZoomForLayer(int layer);

    int activeRequests() const { return m_inFlight.size(); }

signals:
    void tileReady(int layer, int z, int x, int y);
    void networkError(const QString &message);

private:
    QString diskPath(const TileKey &key) const;
    void pumpQueue();

    QString m_cacheDir;
    QNetworkAccessManager m_nam;
    mutable QMutex m_cacheMutex;
    mutable QCache<TileKey, QImage> m_memCache;
    QSet<TileKey> m_inFlight;
    QSet<TileKey> m_queuedSet;
    QQueue<TileKey> m_queue;
    QSet<TileKey> m_failed;
    static constexpr int MaxConcurrent = 10;
};
