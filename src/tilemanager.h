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
#include <QVector>
#include <atomic>

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

struct BaseMapSpec {
    int uid = 0;
    QString id;
    QString name;
    QString urlTemplate;
    QString attribution;
    int maxZoom = 18;
    bool enabled = true;
    bool builtin = false;
};

struct CacheStats {
    qint64 bytes = 0;
    int fileCount = 0;
};

class TileManager : public QObject {
    Q_OBJECT
public:
    static constexpr int ElevationDemLayer = 10000;

    explicit TileManager(const QString &cacheDir = {}, QObject *parent = nullptr);

    void setBaseMaps(const QVector<BaseMapSpec> &maps);
    void invalidateLayerCache(int layerUid);
    void clearAllCache();
    CacheStats diskCacheStats() const;

    quint64 cacheRevision() const { return m_cacheRevision.load(); }

    // Returns cached image if immediately available; otherwise queues async fetch.
    QImage requestTile(int layer, int z, int x, int y);
    QImage cachedTile(int layer, int z, int x, int y) const;

    // Decodes elevation in meters from cached AWS Terrarium DEM tiles (returns 0 if unavailable).
    double sampleElevationMeters(double latDeg, double lonDeg, int preferredZoom = 6) const;

    QString tileUrl(int layer, int z, int x, int y) const;
    QString layerSlug(int layer) const;
    int maxZoomForLayer(int layer) const;

    static QString expandUrlTemplate(const QString &tpl, int z, int x, int y);
    static QVector<BaseMapSpec> defaultBaseMaps();

    int activeRequests() const { return m_inFlight.size(); }

signals:
    void tileReady(int layer, int z, int x, int y);
    void cacheCleared();
    void networkError(const QString &message);

private:
    QString diskPath(const TileKey &key) const;
    void pumpQueue();

    QString m_cacheDir;
    QNetworkAccessManager m_nam;
    mutable QMutex m_cacheMutex;
    QHash<int, BaseMapSpec> m_layers;
    mutable QCache<TileKey, QImage> m_memCache;
    QSet<TileKey> m_inFlight;
    QSet<TileKey> m_queuedSet;
    QQueue<TileKey> m_queue;
    QSet<TileKey> m_failed;
    std::atomic<quint64> m_cacheRevision{1};
    static constexpr int MaxConcurrent = 10;
};
