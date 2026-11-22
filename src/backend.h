#pragma once

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "kmlparser.h"
#include "portalfilepicker.h"
#include "tilemanager.h"

struct FeedEntry {
    QString id;
    QString name;
    QString source;      // URL or local path
    bool isLocal = false;
    bool enabled = false;
    bool loading = false;
    QString error;
    QDateTime lastUpdated;
    int refreshIntervalSec = 300;
    QVector<KmlFolder> folders;
    QVector<KmlPlacemark> placemarks;
    QVector<KmlNetworkLink> networkLinks;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int baseLayer READ baseLayer WRITE setBaseLayer NOTIFY settingsChanged)
    Q_PROPERTY(QString baseLayerName READ baseLayerName NOTIFY settingsChanged)
    Q_PROPERTY(bool terrainEnabled READ terrainEnabled WRITE setTerrainEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool graticuleEnabled READ graticuleEnabled WRITE setGraticuleEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool atmosphereEnabled READ atmosphereEnabled WRITE setAtmosphereEnabled NOTIFY settingsChanged)

    Q_PROPERTY(QVariantList feeds READ feedsVariant NOTIFY feedsChanged)
    Q_PROPERTY(QVariantList folders READ foldersVariant NOTIFY foldersChanged)
    Q_PROPERTY(QVariantMap selectedPlacemark READ selectedPlacemarkVariant NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

    Q_PROPERTY(int totalPlacemarks READ totalPlacemarks NOTIFY kmlDataChanged)
    Q_PROPERTY(int visiblePlacemarks READ visiblePlacemarks NOTIFY kmlDataChanged)
    Q_PROPERTY(int loadingCount READ loadingCount NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

    Q_PROPERTY(bool hasTimeline READ hasTimeline NOTIFY timelineChanged)
    Q_PROPERTY(bool timelineActive READ timelineActive WRITE setTimelineActive NOTIFY timelineChanged)
    Q_PROPERTY(bool timelinePlaying READ timelinePlaying WRITE setTimelinePlaying NOTIFY timelineChanged)
    Q_PROPERTY(double timelineProgress READ timelineProgress WRITE setTimelineProgress NOTIFY timelineChanged)
    Q_PROPERTY(QString currentTimeLabel READ currentTimeLabel NOTIFY timelineChanged)
    Q_PROPERTY(QString timeRangeLabel READ timeRangeLabel NOTIFY timelineChanged)

    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchResultsChanged)

public:
    explicit Backend(TileManager *tileManager = nullptr, FilePicker *filePicker = nullptr,
                     bool autoLoadDefaultFeeds = true, QObject *parent = nullptr);

    TileManager *tileManager() const { return m_tileManager; }

    int baseLayer() const { return m_baseLayer; }
    void setBaseLayer(int layer);
    QString baseLayerName() const;

    bool terrainEnabled() const { return m_terrainEnabled; }
    void setTerrainEnabled(bool enabled);

    bool graticuleEnabled() const { return m_graticuleEnabled; }
    void setGraticuleEnabled(bool enabled);

    bool atmosphereEnabled() const { return m_atmosphereEnabled; }
    void setAtmosphereEnabled(bool enabled);

    QVariantList feedsVariant() const;
    QVariantList foldersVariant() const;
    QVariantMap selectedPlacemarkVariant() const;
    bool hasSelection() const { return !m_selectedId.isEmpty(); }
    QString selectedPlacemarkId() const { return m_selectedId; }

    int totalPlacemarks() const { return m_totalPlacemarks; }
    int visiblePlacemarks() const { return m_visiblePlacemarks.size(); }
    int loadingCount() const { return m_loadingCount; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }

    bool hasTimeline() const { return m_timeMinMs > 0 && m_timeMaxMs > m_timeMinMs; }
    bool timelineActive() const { return m_timelineActive && hasTimeline(); }
    void setTimelineActive(bool active);
    bool timelinePlaying() const { return m_timelinePlaying; }
    void setTimelinePlaying(bool playing);
    double timelineProgress() const { return m_timelineProgress; }
    void setTimelineProgress(double progress);
    QString currentTimeLabel() const;
    QString timeRangeLabel() const;

    QVariantList searchResults() const { return m_searchResults; }
    bool searching() const { return m_searching; }

    // Direct access for GlobeView rendering.
    const QVector<KmlPlacemark> &visiblePlacemarkList() const { return m_visiblePlacemarks; }
    quint64 kmlRevision() const { return m_kmlRevision; }

    Q_INVOKABLE void primaryAction();
    Q_INVOKABLE void toggleFeed(const QString &feedId);
    Q_INVOKABLE void refreshAllFeeds();
    Q_INVOKABLE void refreshFeed(const QString &feedId);
    Q_INVOKABLE void addFeedUrl(const QString &url, const QString &name = {});
    Q_INVOKABLE void removeFeed(const QString &feedId);
    Q_INVOKABLE void openLocalFileDialog();
    Q_INVOKABLE bool loadLocalFile(const QString &filePath);
    Q_INVOKABLE void toggleFolder(const QString &feedId, const QString &folderPath);
    Q_INVOKABLE void selectPlacemark(const QString &placemarkId, bool flyTo = true);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void stepTimeline(double delta);
    Q_INVOKABLE void updateSearch(const QString &query);
    Q_INVOKABLE void activateSearchResult(int index);
    Q_INVOKABLE void openExternalLink(const QString &url);

    // Exposed for tests.
    bool ingestKmlBytesForFeed(const QString &feedId, const QByteArray &data, const QString &sourceName = {});

signals:
    void settingsChanged();
    void feedsChanged();
    void foldersChanged();
    void selectionChanged();
    void kmlDataChanged();
    void statusChanged();
    void errorChanged();
    void timelineChanged();
    void searchResultsChanged();
    void flyToRequested(double lat, double lon, double altitudeRadii);

private:
    void setupDefaultFeeds();
    void fetchUrlForFeed(const QString &feedId, const QString &url, int depth, QSet<QString> visited);
    void rebuildVisiblePlacemarks();
    void recomputeTimeRange();
    void runLocalSearch(const QString &query);
    void runPhotonSearch(const QString &query);
    void setError(const QString &msg);
    void setStatus(const QString &msg);
    int findFeedIndex(const QString &feedId) const;
    double rangeMetersToAltitudeRadii(double rangeMeters) const;

    TileManager *m_tileManager = nullptr;
    bool m_ownsTileManager = false;
    FilePicker *m_filePicker = nullptr;
    bool m_ownsFilePicker = false;
    QNetworkAccessManager m_nam;

    int m_baseLayer = static_cast<int>(BaseLayer::DarkMatter);
    bool m_terrainEnabled = true;
    bool m_graticuleEnabled = true;
    bool m_atmosphereEnabled = true;

    QVector<FeedEntry> m_feeds;
    QHash<QString, bool> m_folderVisibility; // "feedId|folderPath" -> visible
    QVector<KmlPlacemark> m_visiblePlacemarks;
    int m_totalPlacemarks = 0;
    quint64 m_kmlRevision = 1;

    QString m_selectedId;
    int m_loadingCount = 0;
    QString m_statusMessage;
    QString m_errorMessage;

    qint64 m_timeMinMs = 0;
    qint64 m_timeMaxMs = 0;
    bool m_timelineActive = false;
    bool m_timelinePlaying = false;
    double m_timelineProgress = 1.0;
    QTimer m_timelineTimer;
    QTimer m_autoRefreshTimer;

    QString m_pendingSearchQuery;
    QTimer m_searchDebounce;
    QVariantList m_localSearchResults;
    QVariantList m_remoteSearchResults;
    QVariantList m_searchResults;
    bool m_searching = false;
};
