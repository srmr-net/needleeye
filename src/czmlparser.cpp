#include "czmlparser.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84E2 = 0.0066943799901413165;

bool parseIsoInterval(const QString &interval, QDateTime &startOut, QDateTime &endOut) {
    const int slash = interval.indexOf('/');
    if (slash <= 0)
        return false;
    const QDateTime a = QDateTime::fromString(interval.left(slash).trimmed(), Qt::ISODateWithMs);
    const QDateTime b = QDateTime::fromString(interval.mid(slash + 1).trimmed(), Qt::ISODateWithMs);
    if (a.isValid())
        startOut = a.toUTC();
    if (b.isValid())
        endOut = b.toUTC();
    return startOut.isValid() || endOut.isValid();
}

QDateTime parseIsoTime(const QString &str) {
    QDateTime dt = QDateTime::fromString(str.trimmed(), Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(str.trimmed(), Qt::ISODate);
    return dt.isValid() ? dt.toUTC() : QDateTime();
}

bool parseShowValue(const QJsonValue &val, bool fallback = true) {
    if (val.isBool())
        return val.toBool();
    if (val.isObject()) {
        const auto obj = val.toObject();
        if (obj.contains(QStringLiteral("boolean")))
            return obj.value(QStringLiteral("boolean")).toBool(fallback);
    } else if (val.isArray() && !val.toArray().isEmpty()) {
        return parseShowValue(val.toArray().first(), fallback);
    }
    return fallback;
}

double parseNumberValue(const QJsonValue &val, double fallback) {
    if (val.isDouble())
        return val.toDouble();
    if (val.isObject()) {
        const auto obj = val.toObject();
        if (obj.contains(QStringLiteral("number"))) {
            const QJsonValue num = obj.value(QStringLiteral("number"));
            if (num.isDouble())
                return num.toDouble();
            if (num.isArray() && num.toArray().size() >= 2)
                return num.toArray().at(1).toDouble(fallback);
        }
    }
    return fallback;
}

QString parseStringValue(const QJsonValue &val) {
    if (val.isString())
        return val.toString();
    if (val.isObject()) {
        const auto obj = val.toObject();
        if (obj.contains(QStringLiteral("string")))
            return obj.value(QStringLiteral("string")).toString();
    } else if (val.isArray() && !val.toArray().isEmpty()) {
        return parseStringValue(val.toArray().first());
    }
    return {};
}

QColor parseColorValue(const QJsonValue &val, const QColor &fallback) {
    if (val.isArray()) {
        const auto arr = val.toArray();
        if (!arr.isEmpty() && arr.first().isObject())
            return parseColorValue(arr.first(), fallback);
        return fallback;
    }
    if (!val.isObject())
        return fallback;
    const QJsonObject obj = val.toObject();

    if (obj.contains(QStringLiteral("rgba"))) {
        const auto a = obj.value(QStringLiteral("rgba")).toArray();
        // Either [r, g, b, a] or time-tagged [t, r, g, b, a, ...]
        int off = 0;
        if (a.size() >= 5 && (a.size() % 5 == 0))
            off = 1;
        if (a.size() >= off + 4) {
            return QColor(std::clamp(a[off].toInt(), 0, 255),
                          std::clamp(a[off + 1].toInt(), 0, 255),
                          std::clamp(a[off + 2].toInt(), 0, 255),
                          std::clamp(a[off + 3].toInt(), 0, 255));
        }
    }
    if (obj.contains(QStringLiteral("rgbaf"))) {
        const auto a = obj.value(QStringLiteral("rgbaf")).toArray();
        int off = 0;
        if (a.size() >= 5 && (a.size() % 5 == 0))
            off = 1;
        if (a.size() >= off + 4) {
            return QColor::fromRgbF(std::clamp(a[off].toDouble(), 0.0, 1.0),
                                    std::clamp(a[off + 1].toDouble(), 0.0, 1.0),
                                    std::clamp(a[off + 2].toDouble(), 0.0, 1.0),
                                    std::clamp(a[off + 3].toDouble(), 0.0, 1.0));
        }
    }
    return fallback;
}

QColor parseMaterialColor(const QJsonObject &materialObj, const QColor &fallback) {
    if (materialObj.contains(QStringLiteral("solidColor")))
        return parseColorValue(materialObj.value(QStringLiteral("solidColor")).toObject().value(QStringLiteral("color")), fallback);
    if (materialObj.contains(QStringLiteral("polylineOutline")))
        return parseColorValue(materialObj.value(QStringLiteral("polylineOutline")).toObject().value(QStringLiteral("color")), fallback);
    if (materialObj.contains(QStringLiteral("polylineGlow")))
        return parseColorValue(materialObj.value(QStringLiteral("polylineGlow")).toObject().value(QStringLiteral("color")), fallback);
    if (materialObj.contains(QStringLiteral("polylineDash")))
        return parseColorValue(materialObj.value(QStringLiteral("polylineDash")).toObject().value(QStringLiteral("color")), fallback);
    if (materialObj.contains(QStringLiteral("color")))
        return parseColorValue(materialObj.value(QStringLiteral("color")), fallback);
    return fallback;
}

enum class CoordKind {
    CartographicDegrees,
    CartographicRadians,
    Cartesian
};

GeoCoord convertTuple(double a, double b, double c, CoordKind kind) {
    switch (kind) {
    case CoordKind::CartographicDegrees:
        return {a, b, c};
    case CoordKind::CartographicRadians:
        return {a * kRadToDeg, b * kRadToDeg, c};
    case CoordKind::Cartesian:
        return CzmlParser::ecefToGeodetic(a, b, c);
    }
    return {};
}

// Parses a CZML PositionList object (polyline / polygon positions).
QVector<GeoCoord> parsePositionList(const QJsonObject &posObj) {
    QJsonArray arr;
    CoordKind kind = CoordKind::CartographicDegrees;
    if (posObj.contains(QStringLiteral("cartographicDegrees"))) {
        arr = posObj.value(QStringLiteral("cartographicDegrees")).toArray();
        kind = CoordKind::CartographicDegrees;
    } else if (posObj.contains(QStringLiteral("cartographicRadians"))) {
        arr = posObj.value(QStringLiteral("cartographicRadians")).toArray();
        kind = CoordKind::CartographicRadians;
    } else if (posObj.contains(QStringLiteral("cartesian"))) {
        arr = posObj.value(QStringLiteral("cartesian")).toArray();
        kind = CoordKind::Cartesian;
    } else {
        return {};
    }

    QVector<GeoCoord> out;
    out.reserve(arr.size() / 3);
    for (int i = 0; i + 2 < arr.size(); i += 3) {
        out.append(convertTuple(arr[i].toDouble(), arr[i + 1].toDouble(), arr[i + 2].toDouble(), kind));
    }
    return out;
}

// Parses a CZML Position property (static 3-tuple OR time-tagged 4-tuples with optional epoch).
void parseEntityPosition(const QJsonValue &posVal, const QDateTime &fallbackEpoch,
                         bool &hasPoint, GeoCoord &pointOut, QVector<TimedGeoCoord> &samplesOut) {
    if (posVal.isArray()) {
        // Time-intervaled array of position objects: merge all samples in order.
        for (const auto &elem : posVal.toArray())
            parseEntityPosition(elem, fallbackEpoch, hasPoint, pointOut, samplesOut);
        return;
    }
    if (!posVal.isObject())
        return;

    const QJsonObject obj = posVal.toObject();
    QDateTime epoch = obj.contains(QStringLiteral("epoch"))
        ? parseIsoTime(obj.value(QStringLiteral("epoch")).toString())
        : fallbackEpoch;

    QJsonArray arr;
    CoordKind kind = CoordKind::CartographicDegrees;
    if (obj.contains(QStringLiteral("cartographicDegrees"))) {
        arr = obj.value(QStringLiteral("cartographicDegrees")).toArray();
        kind = CoordKind::CartographicDegrees;
    } else if (obj.contains(QStringLiteral("cartographicRadians"))) {
        arr = obj.value(QStringLiteral("cartographicRadians")).toArray();
        kind = CoordKind::CartographicRadians;
    } else if (obj.contains(QStringLiteral("cartesian"))) {
        arr = obj.value(QStringLiteral("cartesian")).toArray();
        kind = CoordKind::Cartesian;
    } else {
        return;
    }

    if (arr.size() == 3) {
        pointOut = convertTuple(arr[0].toDouble(), arr[1].toDouble(), arr[2].toDouble(), kind);
        hasPoint = true;
        return;
    }

    // Time-tagged sampled position array: [t0, x0, y0, z0, t1, x1, y1, z1, ...].
    if (arr.size() >= 4 && (arr.size() % 4 == 0)) {
        samplesOut.reserve(samplesOut.size() + arr.size() / 4);
        for (int i = 0; i + 3 < arr.size(); i += 4) {
            qint64 tMs = 0;
            if (arr[i].isString()) {
                const QDateTime dt = parseIsoTime(arr[i].toString());
                if (dt.isValid())
                    tMs = dt.toMSecsSinceEpoch();
            } else if (arr[i].isDouble()) {
                const double secOffset = arr[i].toDouble();
                if (epoch.isValid())
                    tMs = epoch.toMSecsSinceEpoch() + static_cast<qint64>(std::llround(secOffset * 1000.0));
                else
                    tMs = static_cast<qint64>(std::llround(secOffset * 1000.0));
            }
            const GeoCoord c = convertTuple(arr[i + 1].toDouble(), arr[i + 2].toDouble(), arr[i + 3].toDouble(), kind);
            samplesOut.append({tMs, c});
        }
        if (!samplesOut.isEmpty()) {
            hasPoint = true;
            pointOut = samplesOut.first().coord;
        }
    }
}

QVector<GeoCoord> buildEllipseRing(const GeoCoord &center, double semiMajorM, double semiMinorM, double rotationRad) {
    QVector<GeoCoord> ring;
    if (semiMajorM <= 0.0 || semiMinorM <= 0.0)
        return ring;

    constexpr int steps = 48;
    ring.reserve(steps + 1);
    const double latRad = center.lat * kDegToRad;
    const double cosLat = std::max(0.05, std::cos(latRad));
    const double mPerDegLat = 111320.0;
    const double mPerDegLon = 111320.0 * cosLat;
    const double cosR = std::cos(rotationRad);
    const double sinR = std::sin(rotationRad);

    for (int i = 0; i <= steps; ++i) {
        const double theta = (2.0 * M_PI * i) / steps;
        const double ex = semiMajorM * std::cos(theta);
        const double ey = semiMinorM * std::sin(theta);
        const double rx = ex * cosR - ey * sinR;
        const double ry = ex * sinR + ey * cosR;
        const double dLat = ry / mPerDegLat;
        const double dLon = rx / mPerDegLon;
        ring.append({center.lon + dLon, std::clamp(center.lat + dLat, -89.5, 89.5), center.alt});
    }
    return ring;
}

} // namespace

GeoCoord CzmlParser::ecefToGeodetic(double x, double y, double z) {
    const double p = std::hypot(x, y);
    if (p < 1e-6 && std::abs(z) < 1e-6)
        return {0.0, 0.0, 0.0};

    const double lon = std::atan2(y, x);
    double lat = std::atan2(z, p * (1.0 - kWgs84E2));
    for (int i = 0; i < 6; ++i) {
        const double sinLat = std::sin(lat);
        const double N = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sinLat * sinLat);
        lat = std::atan2(z + kWgs84E2 * N * sinLat, p);
    }

    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double N = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sinLat * sinLat);
    const double alt = (std::abs(cosLat) > 1e-6)
        ? (p / cosLat - N)
        : (std::abs(z) / std::max(1e-9, std::abs(sinLat)) - N * (1.0 - kWgs84E2));

    return {lon * kRadToDeg, lat * kRadToDeg, alt};
}

bool CzmlParser::looksLikeCzml(const QByteArray &data, const QString &sourceName) {
    if (sourceName.endsWith(QStringLiteral(".czml"), Qt::CaseInsensitive))
        return true;
    for (char ch : data) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        return ch == '[';
    }
    return false;
}

KmlDocument CzmlParser::parseBytes(const QByteArray &data, const QString &sourceName, const QString &feedId) {
    KmlDocument doc;

    QJsonParseError jerr;
    const QJsonDocument jdoc = QJsonDocument::fromJson(data, &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        doc.error = QStringLiteral("CZML JSON parse error: %1").arg(jerr.errorString());
        return doc;
    }
    if (!jdoc.isArray()) {
        doc.error = QStringLiteral("CZML document must be a JSON array of packets.");
        return doc;
    }

    const QJsonArray packets = jdoc.array();
    QDateTime docStart;
    QDateTime docEnd;
    QHash<QString, int> folderIndexByPath;
    QHash<QString, QString> packetNames;

    // First pass: capture document metadata, clock interval, and packet names for parent folders.
    for (const auto &pv : packets) {
        if (!pv.isObject())
            continue;
        const QJsonObject pkt = pv.toObject();
        const QString id = pkt.value(QStringLiteral("id")).toString();
        const QString name = pkt.value(QStringLiteral("name")).toString().trimmed();
        if (!id.isEmpty() && !name.isEmpty())
            packetNames.insert(id, name);

        if (id == QLatin1String("document")) {
            if (!name.isEmpty())
                doc.name = name;
            const QString desc = parseStringValue(pkt.value(QStringLiteral("description")));
            if (!desc.isEmpty())
                doc.description = KmlParser::stripHtml(desc);
            if (pkt.contains(QStringLiteral("clock"))) {
                const QJsonObject clk = pkt.value(QStringLiteral("clock")).toObject();
                parseIsoInterval(clk.value(QStringLiteral("interval")).toString(), docStart, docEnd);
            }
        }
    }

    int pmIndex = 0;
    for (const auto &pv : packets) {
        if (!pv.isObject())
            continue;
        const QJsonObject pkt = pv.toObject();
        const QString rawId = pkt.value(QStringLiteral("id")).toString();
        if (rawId == QLatin1String("document"))
            continue;

        KmlPlacemark pm;
        pm.feedId = feedId;
        pm.id = rawId.isEmpty()
            ? QStringLiteral("%1_czml_%2").arg(feedId.isEmpty() ? QStringLiteral("czml") : feedId).arg(pmIndex)
            : (feedId.isEmpty() ? rawId : (feedId + QStringLiteral(":") + rawId));
        ++pmIndex;

        pm.name = pkt.value(QStringLiteral("name")).toString().trimmed();
        if (pm.name.isEmpty() && pkt.contains(QStringLiteral("label")))
            pm.name = parseStringValue(pkt.value(QStringLiteral("label")).toObject().value(QStringLiteral("text"))).trimmed();
        if (pm.name.isEmpty())
            pm.name = rawId.isEmpty() ? QStringLiteral("CZML Entity") : rawId;

        const QString parentId = pkt.value(QStringLiteral("parent")).toString().trimmed();
        if (!parentId.isEmpty())
            pm.folderPath = packetNames.value(parentId, parentId);

        pm.visible = parseShowValue(pkt.value(QStringLiteral("show")), true);

        const QString descHtml = parseStringValue(pkt.value(QStringLiteral("description")));
        if (!descHtml.isEmpty()) {
            pm.descriptionHtml = descHtml;
            pm.descriptionText = KmlParser::stripHtml(descHtml);
            pm.linkUrl = KmlParser::extractFirstLink(descHtml);
        }

        // Availability interval(s).
        const QJsonValue avail = pkt.value(QStringLiteral("availability"));
        if (avail.isString()) {
            parseIsoInterval(avail.toString(), pm.timeStart, pm.timeEnd);
        } else if (avail.isArray() && !avail.toArray().isEmpty()) {
            QDateTime firstS, firstE, lastS, lastE;
            parseIsoInterval(avail.toArray().first().toString(), firstS, firstE);
            parseIsoInterval(avail.toArray().last().toString(), lastS, lastE);
            pm.timeStart = firstS;
            pm.timeEnd = lastE.isValid() ? lastE : firstE;
        }

        // Position (static or time-sampled).
        if (pkt.contains(QStringLiteral("position"))) {
            parseEntityPosition(pkt.value(QStringLiteral("position")), docStart, pm.hasPoint, pm.point, pm.samples);
            if (!pm.samples.isEmpty()) {
                std::sort(pm.samples.begin(), pm.samples.end(),
                          [](const TimedGeoCoord &a, const TimedGeoCoord &b) { return a.timeMs < b.timeMs; });
                if (!pm.timeStart.isValid())
                    pm.timeStart = QDateTime::fromMSecsSinceEpoch(pm.samples.first().timeMs, QTimeZone::utc());
                if (!pm.timeEnd.isValid())
                    pm.timeEnd = QDateTime::fromMSecsSinceEpoch(pm.samples.last().timeMs, QTimeZone::utc());
            }
        }

        // Point / Billboard / Label styling.
        if (pkt.contains(QStringLiteral("point"))) {
            const QJsonObject pt = pkt.value(QStringLiteral("point")).toObject();
            pm.style.iconColor = parseColorValue(pt.value(QStringLiteral("color")), pm.style.iconColor);
            const double px = parseNumberValue(pt.value(QStringLiteral("pixelSize")), 10.0);
            pm.style.iconScale = std::clamp(px / 9.0, 0.65, 2.5);
            if (!parseShowValue(pt.value(QStringLiteral("show")), true) && !pkt.contains(QStringLiteral("billboard")))
                pm.hasPoint = pm.hasPoint && !pm.samples.isEmpty();
        }
        if (pkt.contains(QStringLiteral("billboard"))) {
            const QJsonObject bb = pkt.value(QStringLiteral("billboard")).toObject();
            pm.style.iconColor = parseColorValue(bb.value(QStringLiteral("color")), pm.style.iconColor);
            pm.style.iconScale = std::clamp(parseNumberValue(bb.value(QStringLiteral("scale")), 1.0), 0.65, 2.5);
        }
        if (pkt.contains(QStringLiteral("label"))) {
            const QJsonObject lb = pkt.value(QStringLiteral("label")).toObject();
            pm.style.labelColor = parseColorValue(lb.value(QStringLiteral("fillColor")), Qt::white);
        }

        // Path (renders sampled trajectory as a 3D orbital/flight polyline).
        const bool hasPathObj = pkt.contains(QStringLiteral("path"));
        if (hasPathObj) {
            const QJsonObject pathObj = pkt.value(QStringLiteral("path")).toObject();
            const bool showPath = parseShowValue(pathObj.value(QStringLiteral("show")), true);
            pm.style.lineColor = parseMaterialColor(pathObj.value(QStringLiteral("material")).toObject(), pm.style.iconColor);
            pm.style.lineWidth = parseNumberValue(pathObj.value(QStringLiteral("width")), 2.0);
            if (showPath && pm.samples.size() >= 2) {
                QVector<GeoCoord> track;
                track.reserve(pm.samples.size());
                for (const auto &s : pm.samples)
                    track.append(s.coord);
                pm.lines.append(track);
            }
        } else if (pm.samples.size() >= 2) {
            // Even without an explicit "path" block, show the sampled trajectory subtly.
            QVector<GeoCoord> track;
            track.reserve(pm.samples.size());
            for (const auto &s : pm.samples)
                track.append(s.coord);
            pm.lines.append(track);
            pm.style.lineColor = pm.style.iconColor;
        }

        // Polyline geometry.
        if (pkt.contains(QStringLiteral("polyline"))) {
            const QJsonObject pl = pkt.value(QStringLiteral("polyline")).toObject();
            if (parseShowValue(pl.value(QStringLiteral("show")), true)) {
                const auto coords = parsePositionList(pl.value(QStringLiteral("positions")).toObject());
                if (coords.size() >= 2) {
                    pm.lines.append(coords);
                    pm.style.lineColor = parseMaterialColor(pl.value(QStringLiteral("material")).toObject(), pm.style.lineColor);
                    pm.style.lineWidth = parseNumberValue(pl.value(QStringLiteral("width")), 2.0);
                }
            }
        }

        // Polygon geometry.
        if (pkt.contains(QStringLiteral("polygon"))) {
            const QJsonObject pg = pkt.value(QStringLiteral("polygon")).toObject();
            if (parseShowValue(pg.value(QStringLiteral("show")), true)) {
                const auto outer = parsePositionList(pg.value(QStringLiteral("positions")).toObject());
                if (outer.size() >= 3) {
                    KmlPolygon poly;
                    poly.outer = outer;
                    poly.extrude = pg.contains(QStringLiteral("extrudedHeight"));
                    pm.polygons.append(poly);
                    pm.style.polyFill = parseShowValue(pg.value(QStringLiteral("fill")), true);
                    pm.style.polyOutline = parseShowValue(pg.value(QStringLiteral("outline")), true);
                    pm.style.polyColor = parseMaterialColor(pg.value(QStringLiteral("material")).toObject(), pm.style.polyColor);
                    pm.style.lineColor = parseColorValue(pg.value(QStringLiteral("outlineColor")), pm.style.polyColor);
                }
            }
        }

        // Ellipse geometry (common for CZML sensor footprints & coverage rings).
        if (pkt.contains(QStringLiteral("ellipse")) && pm.hasPoint) {
            const QJsonObject el = pkt.value(QStringLiteral("ellipse")).toObject();
            if (parseShowValue(el.value(QStringLiteral("show")), true)) {
                const double semiMajor = parseNumberValue(el.value(QStringLiteral("semiMajorAxis")), 0.0);
                const double semiMinor = parseNumberValue(el.value(QStringLiteral("semiMinorAxis")), semiMajor);
                const double rot = parseNumberValue(el.value(QStringLiteral("rotation")), 0.0);
                const auto ring = buildEllipseRing(pm.point, semiMajor, semiMinor, rot);
                if (ring.size() >= 4) {
                    KmlPolygon poly;
                    poly.outer = ring;
                    pm.polygons.append(poly);
                    pm.style.polyFill = parseShowValue(el.value(QStringLiteral("fill")), true);
                    pm.style.polyOutline = parseShowValue(el.value(QStringLiteral("outline")), true);
                    pm.style.polyColor = parseMaterialColor(el.value(QStringLiteral("material")).toObject(), pm.style.polyColor);
                    pm.style.lineColor = parseColorValue(el.value(QStringLiteral("outlineColor")), pm.style.polyColor);
                }
            }
        }

        // Expose custom CZML "properties" bag inside ExtendedData for the Inspector panel.
        if (pkt.contains(QStringLiteral("properties")) && pkt.value(QStringLiteral("properties")).isObject()) {
            const QJsonObject props = pkt.value(QStringLiteral("properties")).toObject();
            for (auto it = props.begin(); it != props.end(); ++it) {
                QString valStr;
                if (it.value().isString())
                    valStr = it.value().toString();
                else if (it.value().isDouble())
                    valStr = QString::number(it.value().toDouble());
                else if (it.value().isBool())
                    valStr = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
                else if (it.value().isObject())
                    valStr = parseStringValue(it.value());
                if (!valStr.isEmpty())
                    pm.extendedData.insert(it.key(), valStr);
            }
        }

        if (!pm.hasPoint && pm.lines.isEmpty() && pm.polygons.isEmpty())
            continue;

        if (!pm.folderPath.isEmpty()) {
            int fIdx = folderIndexByPath.value(pm.folderPath, -1);
            if (fIdx < 0) {
                fIdx = doc.folders.size();
                folderIndexByPath.insert(pm.folderPath, fIdx);
                doc.folders.append({pm.folderPath, pm.folderPath, true, 0});
            }
            doc.folders[fIdx].placemarkCount++;
        }

        doc.placemarks.append(pm);
    }

    if (doc.name.isEmpty())
        doc.name = sourceName.isEmpty() ? QStringLiteral("CZML Feed") : sourceName;
    return doc;
}

KmlDocument CzmlParser::parseFile(const QString &filePath, const QString &feedId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        KmlDocument doc;
        doc.error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return doc;
    }
    return parseBytes(file.readAll(), QFileInfo(filePath).fileName(), feedId);
}
