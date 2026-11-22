#pragma once

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

struct GeoCoord {
    double lon = 0.0;
    double lat = 0.0;
    double alt = 0.0;
};

struct KmlLookAt {
    bool valid = false;
    double lon = 0.0;
    double lat = 0.0;
    double range = 1500000.0; // meters
    double tilt = 0.0;
    double heading = 0.0;
};

struct KmlStyle {
    QString id;
    QColor iconColor = QColor(255, 214, 10, 255);
    double iconScale = 1.0;
    QString iconHref;
    QColor lineColor = QColor(255, 214, 10, 230);
    double lineWidth = 2.0;
    QColor polyColor = QColor(255, 214, 10, 90);
    bool polyFill = true;
    bool polyOutline = true;
    QColor labelColor = Qt::white;
    double labelScale = 1.0;
};

struct KmlPolygon {
    QVector<GeoCoord> outer;
    QVector<QVector<GeoCoord>> inners;
    bool extrude = false;
};

struct KmlPlacemark {
    QString id;
    QString feedId;
    QString folderPath;
    QString name;
    QString descriptionHtml;
    QString descriptionText;
    QString linkUrl;
    QHash<QString, QString> extendedData;
    QString styleUrl;
    KmlStyle style;
    bool hasInlineStyle = false;
    bool visible = true;

    bool hasPoint = false;
    GeoCoord point;
    QVector<QVector<GeoCoord>> lines;
    QVector<KmlPolygon> polygons;

    KmlLookAt lookAt;
    QDateTime timeStart;
    QDateTime timeEnd;

    // Best representative coordinate for fly-to and picking.
    GeoCoord anchor() const;
};

struct KmlNetworkLink {
    QString name;
    QString folderPath;
    QString href;
    int refreshIntervalSec = 0;
    bool visible = true;
};

struct KmlGroundOverlay {
    QString name;
    QString folderPath;
    QString iconHref;
    double north = 0.0;
    double south = 0.0;
    double east = 0.0;
    double west = 0.0;
    double rotation = 0.0;
    QColor color = Qt::white;
    bool visible = true;
};

struct KmlFolder {
    QString path;
    QString name;
    bool visible = true;
    int placemarkCount = 0;
};

struct KmlDocument {
    QString name;
    QString description;
    QHash<QString, KmlStyle> styles;
    QHash<QString, QString> styleMaps; // styleMapId -> normal styleUrl
    QVector<KmlFolder> folders;
    QVector<KmlPlacemark> placemarks;
    QVector<KmlNetworkLink> networkLinks;
    QVector<KmlGroundOverlay> groundOverlays;
    QHash<QString, QImage> embeddedImages;
    QString error;
};

class KmlParser {
public:
    static KmlDocument parseBytes(const QByteArray &data, const QString &sourceName = {},
                                  const QString &feedId = {});
    static KmlDocument parseFile(const QString &filePath, const QString &feedId = {});
    static KmlDocument parseKmzBytes(const QByteArray &zipData, const QString &sourceName = {},
                                     const QString &feedId = {});

    // Converts KML aabbggrr hex string to QColor.
    static QColor parseKmlColor(const QString &aabbggrr, const QColor &fallback = Qt::white);
    static QVector<GeoCoord> parseCoordinates(const QString &text);
    static QString stripHtml(const QString &html);
    static QString extractFirstLink(const QString &html);

    // Ear-clipping triangulation for a 2D simple polygon (lon, lat pairs).
    // Returns triangle vertex indices into `ring`.
    static QVector<int> triangulateRing(const QVector<GeoCoord> &ring);
};
