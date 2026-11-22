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
    Q_PROPERTY(QString baseLayerId READ baseLayerId NOTIFY settingsChanged)
    Q_PROPERTY(QString baseLayerName READ baseLayerName NOTIFY settingsChanged)
    Q_PROPERTY(QString baseLayerAttribution READ baseLayerAttribution NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList baseMaps READ baseMapsVariant NOTIFY baseMapsChanged)
    Q_PROPERTY(QVariantList enabledBaseMaps READ enabledBaseMapsVariant NOTIFY baseMapsChanged)

    Q_PROPERTY(bool terrainEnabled READ terrainEnabled WRITE setTerrainEnabled NOTIFY settingsChanged)
    Q_PROPERTY(double terrainExaggeration READ terrainExaggeration WRITE setTerrainExaggeration NOTIFY settingsChanged)
    Q_PROPERTY(bool graticuleEnabled READ graticuleEnabled WRITE setGraticuleEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool atmosphereEnabled READ atmosphereEnabled WRITE setAtmosphereEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRefreshEnabled READ autoRefreshEnabled WRITE setAutoRefreshEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int autoRefreshMinutes READ autoRefreshMinutes WRITE setAutoRefreshMinutes NOTIFY settingsChanged)
    Q_PROPERTY(QString cacheSummary READ cacheSummary NOTIFY cacheStatsChanged)

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
                     bool autoLoadDefaultFeeds = true, const QString &settingsPath = {},
                     QObject *parent = nullptr);

    TileManager *tileManager() const { return m_tileManager; }

    int baseLayer() const { return m_activeBaseMapUid; }
    void setBaseLayer(int layerUid);
    QString baseLayerId() const;
    QString baseLayerName() const;
    QString baseLayerAttribution() const;
    QVariantList baseMapsVariant() const;
    QVariantList enabledBaseMapsVariant() const;

    bool terrainEnabled() const { return m_terrainEnabled; }
    void setTerrainEnabled(bool enabled);
    double terrainExaggeration() const { return m_terrainExaggeration; }
    void setTerrainExaggeration(double value);

    bool graticuleEnabled() const { return m_graticuleEnabled; }
    void setGraticuleEnabled(bool enabled);

    bool atmosphereEnabled() const { return m_atmosphereEnabled; }
    void setAtmosphereEnabled(bool enabled);

    bool autoRefreshEnabled() const { return m_autoRefreshEnabled; }
    void setAutoRefreshEnabled(bool enabled);
    int autoRefreshMinutes() const { return m_autoRefreshMinutes; }
    void setAutoRefreshMinutes(int minutes);

    QString cacheSummary() const { return m_cacheSummary; }

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

    // Base map management actions.
    Q_INVOKABLE void selectBaseMap(const QString &mapId);
    Q_INVOKABLE void selectBaseMapBySlot(int slotIndex);
    Q_INVOKABLE void cycleBaseMap();
    Q_INVOKABLE bool setBaseMapEnabled(const QString &mapId, bool enabled);
    Q_INVOKABLE bool moveBaseMap(const QString &mapId, int delta);
    Q_INVOKABLE QString addCustomBaseMap(const QString &name, const QString &urlTemplate,
                                         int maxZoom = 18, const QString &attribution = {},
                                         const QString &projection = QStringLiteral("xyz"));
    Q_INVOKABLE QString updateBaseMap(const QString &mapId, const QString &name,
                                      const QString &urlTemplate, int maxZoom,
                                      const QString &attribution,
                                      const QString &projection = QStringLiteral("xyz"));
    Q_INVOKABLE void browseBaseMapImage();
    Q_INVOKABLE bool removeBaseMap(const QString &mapId);
    Q_INVOKABLE void resetBaseMapsToDefaults();
    Q_INVOKABLE void refreshCacheStats();
    Q_INVOKABLE void clearTileCache();

    // KML & globe actions.
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
    static QString validateBaseMapInput(const QString &name, const QString &urlTemplate, int maxZoom,
                                        const QString &projection = QStringLiteral("xyz"));

signals:
    void settingsChanged();
    void baseMapsChanged();
    void baseMapImagePicked(const QString &path);
    void cacheStatsChanged();
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
    void loadSettings();
    void saveSettings() const;
    void syncBaseMapsToTileManager();
    void ensureValidActiveBaseMap();
    int findBaseMapIndexById(const QString &mapId) const;
    int findBaseMapIndexByUid(int uid) const;
    QString makeUniqueBaseMapId(const QString &name) const;

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

    QString m_settingsPath;
    QVector<BaseMapSpec> m_baseMaps;
    int m_nextBaseMapUid = 100;
    int m_activeBaseMapUid = 0;

    bool m_terrainEnabled = true;
    double m_terrainExaggeration = 1.0;
    bool m_graticuleEnabled = true;
    bool m_atmosphereEnabled = true;
    bool m_autoRefreshEnabled = true;
    int m_autoRefreshMinutes = 5;
    QString m_cacheSummary;

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
