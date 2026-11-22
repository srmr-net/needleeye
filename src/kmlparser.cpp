#include "kmlparser.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocument>
#include <QXmlStreamReader>
#include <cmath>
#include <zip.h>

GeoCoord KmlPlacemark::anchor() const {
    if (hasPoint)
        return point;
    if (!samples.isEmpty())
        return samples.first().coord;
    if (lookAt.valid)
        return {lookAt.lon, lookAt.lat, 0.0};
    if (!lines.isEmpty() && !lines.first().isEmpty()) {
        const auto &line = lines.first();
        return line.at(line.size() / 2);
    }
    if (!polygons.isEmpty() && !polygons.first().outer.isEmpty()) {
        double sumLon = 0.0, sumLat = 0.0;
        const auto &ring = polygons.first().outer;
        for (const auto &c : ring) {
            sumLon += c.lon;
            sumLat += c.lat;
        }
        return {sumLon / ring.size(), sumLat / ring.size(), 0.0};
    }
    return {};
}

GeoCoord KmlPlacemark::positionAt(qint64 timeMs) const {
    if (samples.isEmpty())
        return anchor();
    if (timeMs <= samples.first().timeMs)
        return samples.first().coord;
    if (timeMs >= samples.last().timeMs)
        return samples.last().coord;

    // Binary search for bracketing samples.
    int lo = 0;
    int hi = samples.size() - 1;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (samples[mid].timeMs <= timeMs)
            lo = mid;
        else
            hi = mid;
    }

    const auto &a = samples[lo];
    const auto &b = samples[hi];
    const double span = static_cast<double>(b.timeMs - a.timeMs);
    if (span <= 0.0)
        return a.coord;
    const double t = (static_cast<double>(timeMs - a.timeMs)) / span;

    // Shortest-path longitude wrap across the antimeridian.
    double dLon = b.coord.lon - a.coord.lon;
    while (dLon > 180.0) dLon -= 360.0;
    while (dLon < -180.0) dLon += 360.0;
    double lon = a.coord.lon + dLon * t;
    while (lon > 180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;

    return {
        lon,
        a.coord.lat + (b.coord.lat - a.coord.lat) * t,
        a.coord.alt + (b.coord.alt - a.coord.alt) * t
    };
}

QColor KmlParser::parseKmlColor(const QString &aabbggrr, const QColor &fallback) {
    QString s = aabbggrr.trimmed();
    if (s.startsWith('#'))
        s.remove(0, 1);
    if (s.size() == 6)
        s.prepend(QStringLiteral("ff"));
    if (s.size() != 8)
        return fallback;

    bool okA = false, okB = false, okG = false, okR = false;
    const int a = s.mid(0, 2).toInt(&okA, 16);
    const int b = s.mid(2, 2).toInt(&okB, 16);
    const int g = s.mid(4, 2).toInt(&okG, 16);
    const int r = s.mid(6, 2).toInt(&okR, 16);
    if (!okA || !okB || !okG || !okR)
        return fallback;
    return QColor(r, g, b, a);
}

QVector<GeoCoord> KmlParser::parseCoordinates(const QString &text) {
    QVector<GeoCoord> result;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    result.reserve(tokens.size());
    for (const QString &tok : tokens) {
        const QStringList parts = tok.split(',');
        if (parts.size() < 2)
            continue;
        bool okLon = false, okLat = false;
        const double lon = parts[0].toDouble(&okLon);
        const double lat = parts[1].toDouble(&okLat);
        if (!okLon || !okLat)
            continue;
        const double alt = parts.size() >= 3 ? parts[2].toDouble() : 0.0;
        result.append({lon, lat, alt});
    }
    return result;
}

QString KmlParser::stripHtml(const QString &html) {
    if (html.isEmpty())
        return {};
    QString text = html;
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("</(p|div|tr|li|h[1-6])>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("<td[^>]*>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" "));
    text.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"),
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));

    QStringList cleanedLines;
    for (const QString &line : text.split('\n')) {
        const QString trimmed = line.simplified();
        if (!trimmed.isEmpty())
            cleanedLines.append(trimmed);
    }
    return cleanedLines.join('\n');
}

QString KmlParser::extractFirstLink(const QString &html) {
    static const QRegularExpression hrefRe(QStringLiteral(R"re(href\s*=\s*["'](https?://[^"']+)["'])re"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto match = hrefRe.match(html);
    return match.hasMatch() ? match.captured(1) : QString();
}

namespace {

KmlStyle parseStyleElement(QXmlStreamReader &xml) {
    KmlStyle style;
    style.id = xml.attributes().value(QStringLiteral("id")).toString();

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("Style"))
            break;
        if (!xml.isStartElement())
            continue;

        const QStringView tag = xml.name();
        if (tag == QLatin1String("IconStyle")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("IconStyle"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("color"))
                    style.iconColor = KmlParser::parseKmlColor(xml.readElementText(), style.iconColor);
                else if (xml.name() == QLatin1String("scale"))
                    style.iconScale = xml.readElementText().trimmed().toDouble();
                else if (xml.name() == QLatin1String("Icon")) {
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isEndElement() && xml.name() == QLatin1String("Icon"))
                            break;
                        if (xml.isStartElement() && xml.name() == QLatin1String("href"))
                            style.iconHref = xml.readElementText().trimmed();
                    }
                }
            }
        } else if (tag == QLatin1String("LineStyle")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("LineStyle"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("color"))
                    style.lineColor = KmlParser::parseKmlColor(xml.readElementText(), style.lineColor);
                else if (xml.name() == QLatin1String("width"))
                    style.lineWidth = xml.readElementText().trimmed().toDouble();
            }
        } else if (tag == QLatin1String("PolyStyle")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("PolyStyle"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("color"))
                    style.polyColor = KmlParser::parseKmlColor(xml.readElementText(), style.polyColor);
                else if (xml.name() == QLatin1String("fill"))
                    style.polyFill = xml.readElementText().trimmed() != QLatin1String("0");
                else if (xml.name() == QLatin1String("outline"))
                    style.polyOutline = xml.readElementText().trimmed() != QLatin1String("0");
            }
        } else if (tag == QLatin1String("LabelStyle")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("LabelStyle"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("color"))
                    style.labelColor = KmlParser::parseKmlColor(xml.readElementText(), style.labelColor);
                else if (xml.name() == QLatin1String("scale"))
                    style.labelScale = xml.readElementText().trimmed().toDouble();
            }
        }
    }
    return style;
}

void parseStyleMapElement(QXmlStreamReader &xml, KmlDocument &doc) {
    const QString id = xml.attributes().value(QStringLiteral("id")).toString();
    QString normalUrl;
    QString fallbackUrl;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("StyleMap"))
            break;
        if (xml.isStartElement() && xml.name() == QLatin1String("Pair")) {
            QString key;
            QString url;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("Pair"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("key"))
                    key = xml.readElementText().trimmed();
                else if (xml.name() == QLatin1String("styleUrl"))
                    url = xml.readElementText().trimmed();
            }
            if (fallbackUrl.isEmpty() && !url.isEmpty())
                fallbackUrl = url;
            if (key == QLatin1String("normal") && !url.isEmpty())
                normalUrl = url;
        }
    }
    if (!id.isEmpty())
        doc.styleMaps.insert(id, !normalUrl.isEmpty() ? normalUrl : fallbackUrl);
}

KmlLookAt parseLookAtElement(QXmlStreamReader &xml, const QString &endTag) {
    KmlLookAt lookAt;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == endTag)
            break;
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("longitude")) {
            lookAt.lon = xml.readElementText().trimmed().toDouble();
            lookAt.valid = true;
        } else if (xml.name() == QLatin1String("latitude")) {
            lookAt.lat = xml.readElementText().trimmed().toDouble();
            lookAt.valid = true;
        } else if (xml.name() == QLatin1String("range") || xml.name() == QLatin1String("altitude")) {
            const double val = xml.readElementText().trimmed().toDouble();
            if (val > 0.0)
                lookAt.range = val;
        } else if (xml.name() == QLatin1String("tilt")) {
            lookAt.tilt = xml.readElementText().trimmed().toDouble();
        } else if (xml.name() == QLatin1String("heading")) {
            lookAt.heading = xml.readElementText().trimmed().toDouble();
        }
    }
    return lookAt;
}

KmlPolygon parsePolygonElement(QXmlStreamReader &xml) {
    KmlPolygon poly;
    bool inOuter = false;
    bool inInner = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("Polygon"))
            break;
        if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("outerBoundaryIs"))
                inOuter = false;
            else if (xml.name() == QLatin1String("innerBoundaryIs"))
                inInner = false;
            continue;
        }
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("outerBoundaryIs")) {
            inOuter = true;
        } else if (xml.name() == QLatin1String("innerBoundaryIs")) {
            inInner = true;
        } else if (xml.name() == QLatin1String("extrude")) {
            poly.extrude = xml.readElementText().trimmed() == QLatin1String("1");
        } else if (xml.name() == QLatin1String("coordinates")) {
            const auto coords = KmlParser::parseCoordinates(xml.readElementText());
            if (inOuter && !coords.isEmpty())
                poly.outer = coords;
            else if (inInner && !coords.isEmpty())
                poly.inners.append(coords);
        }
    }
    return poly;
}

void parseGeometryElement(QXmlStreamReader &xml, KmlPlacemark &pm) {
    const QString tag = xml.name().toString();
    if (tag == QLatin1String("Point")) {
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("Point"))
                break;
            if (xml.isStartElement() && xml.name() == QLatin1String("coordinates")) {
                const auto coords = KmlParser::parseCoordinates(xml.readElementText());
                if (!coords.isEmpty()) {
                    pm.hasPoint = true;
                    pm.point = coords.first();
                }
            }
        }
    } else if (tag == QLatin1String("LineString")) {
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("LineString"))
                break;
            if (xml.isStartElement() && xml.name() == QLatin1String("coordinates")) {
                const auto coords = KmlParser::parseCoordinates(xml.readElementText());
                if (coords.size() >= 2)
                    pm.lines.append(coords);
            }
        }
    } else if (tag == QLatin1String("LinearRing")) {
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("LinearRing"))
                break;
            if (xml.isStartElement() && xml.name() == QLatin1String("coordinates")) {
                const auto coords = KmlParser::parseCoordinates(xml.readElementText());
                if (coords.size() >= 2)
                    pm.lines.append(coords);
            }
        }
    } else if (tag == QLatin1String("Polygon")) {
        KmlPolygon poly = parsePolygonElement(xml);
        if (poly.outer.size() >= 3)
            pm.polygons.append(poly);
    } else if (tag == QLatin1String("Track")) {
        QVector<GeoCoord> trackCoords;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("Track"))
                break;
            if (!xml.isStartElement())
                continue;
            if (xml.name() == QLatin1String("when")) {
                const QDateTime dt = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
                if (dt.isValid()) {
                    if (!pm.timeStart.isValid() || dt < pm.timeStart)
                        pm.timeStart = dt;
                    if (!pm.timeEnd.isValid() || dt > pm.timeEnd)
                        pm.timeEnd = dt;
                }
            } else if (xml.name() == QLatin1String("coord")) {
                const QStringList parts = xml.readElementText().trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    const double lon = parts[0].toDouble();
                    const double lat = parts[1].toDouble();
                    const double alt = parts.size() >= 3 ? parts[2].toDouble() : 0.0;
                    trackCoords.append({lon, lat, alt});
                }
            }
        }
        if (trackCoords.size() >= 2)
            pm.lines.append(trackCoords);
        else if (trackCoords.size() == 1 && !pm.hasPoint) {
            pm.hasPoint = true;
            pm.point = trackCoords.first();
        }
    } else if (tag == QLatin1String("MultiGeometry")) {
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("MultiGeometry"))
                break;
            if (xml.isStartElement())
                parseGeometryElement(xml, pm);
        }
    }
}

KmlPlacemark parsePlacemarkElement(QXmlStreamReader &xml, const QString &feedId,
                                   const QString &folderPath, bool inheritedVisible, int index) {
    KmlPlacemark pm;
    pm.id = xml.attributes().value(QStringLiteral("id")).toString();
    if (pm.id.isEmpty())
        pm.id = QStringLiteral("%1_pm_%2").arg(feedId.isEmpty() ? QStringLiteral("kml") : feedId).arg(index);
    else if (!feedId.isEmpty())
        pm.id = feedId + QStringLiteral(":") + pm.id;
    pm.feedId = feedId;
    pm.folderPath = folderPath;
    pm.visible = inheritedVisible;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("Placemark"))
            break;
        if (!xml.isStartElement())
            continue;

        const QStringView tag = xml.name();
        if (tag == QLatin1String("name")) {
            pm.name = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("description")) {
            pm.descriptionHtml = xml.readElementText();
            pm.descriptionText = KmlParser::stripHtml(pm.descriptionHtml);
            pm.linkUrl = KmlParser::extractFirstLink(pm.descriptionHtml);
        } else if (tag == QLatin1String("visibility")) {
            pm.visible = inheritedVisible && (xml.readElementText().trimmed() != QLatin1String("0"));
        } else if (tag == QLatin1String("styleUrl")) {
            pm.styleUrl = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("Style")) {
            pm.style = parseStyleElement(xml);
            pm.hasInlineStyle = true;
        } else if (tag == QLatin1String("LookAt") || tag == QLatin1String("Camera")) {
            pm.lookAt = parseLookAtElement(xml, tag.toString());
        } else if (tag == QLatin1String("TimeStamp")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("TimeStamp"))
                    break;
                if (xml.isStartElement() && xml.name() == QLatin1String("when")) {
                    const QDateTime dt = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
                    if (dt.isValid()) {
                        pm.timeStart = dt;
                        pm.timeEnd = dt;
                    }
                }
            }
        } else if (tag == QLatin1String("TimeSpan")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("TimeSpan"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("begin"))
                    pm.timeStart = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
                else if (xml.name() == QLatin1String("end"))
                    pm.timeEnd = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
            }
        } else if (tag == QLatin1String("ExtendedData")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("ExtendedData"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("Data")) {
                    const QString key = xml.attributes().value(QStringLiteral("name")).toString();
                    QString value;
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isEndElement() && xml.name() == QLatin1String("Data"))
                            break;
                        if (xml.isStartElement() && xml.name() == QLatin1String("value"))
                            value = xml.readElementText().trimmed();
                    }
                    if (!key.isEmpty() && !value.isEmpty())
                        pm.extendedData.insert(key, value);
                } else if (xml.name() == QLatin1String("SimpleData")) {
                    const QString key = xml.attributes().value(QStringLiteral("name")).toString();
                    const QString value = xml.readElementText().trimmed();
                    if (!key.isEmpty() && !value.isEmpty())
                        pm.extendedData.insert(key, value);
                }
            }
        } else if (tag == QLatin1String("Point") || tag == QLatin1String("LineString") ||
                   tag == QLatin1String("LinearRing") || tag == QLatin1String("Polygon") ||
                   tag == QLatin1String("MultiGeometry") || tag == QLatin1String("Track")) {
            parseGeometryElement(xml, pm);
        }
    }

    if (pm.name.isEmpty())
        pm.name = QStringLiteral("Unnamed feature");
    return pm;
}

KmlNetworkLink parseNetworkLinkElement(QXmlStreamReader &xml, const QString &folderPath, bool inheritedVisible) {
    KmlNetworkLink link;
    link.folderPath = folderPath;
    link.visible = inheritedVisible;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("NetworkLink"))
            break;
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("name"))
            link.name = xml.readElementText().trimmed();
        else if (xml.name() == QLatin1String("visibility"))
            link.visible = inheritedVisible && (xml.readElementText().trimmed() != QLatin1String("0"));
        else if (xml.name() == QLatin1String("Link") || xml.name() == QLatin1String("Url")) {
            const QString endTag = xml.name().toString();
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == endTag)
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("href"))
                    link.href = xml.readElementText().trimmed();
                else if (xml.name() == QLatin1String("refreshInterval"))
                    link.refreshIntervalSec = static_cast<int>(std::round(xml.readElementText().trimmed().toDouble()));
            }
        }
    }
    return link;
}

KmlGroundOverlay parseGroundOverlayElement(QXmlStreamReader &xml, const QString &folderPath, bool inheritedVisible) {
    KmlGroundOverlay ov;
    ov.folderPath = folderPath;
    ov.visible = inheritedVisible;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("GroundOverlay"))
            break;
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("name"))
            ov.name = xml.readElementText().trimmed();
        else if (xml.name() == QLatin1String("visibility"))
            ov.visible = inheritedVisible && (xml.readElementText().trimmed() != QLatin1String("0"));
        else if (xml.name() == QLatin1String("color"))
            ov.color = KmlParser::parseKmlColor(xml.readElementText(), Qt::white);
        else if (xml.name() == QLatin1String("Icon")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("Icon"))
                    break;
                if (xml.isStartElement() && xml.name() == QLatin1String("href"))
                    ov.iconHref = xml.readElementText().trimmed();
            }
        } else if (xml.name() == QLatin1String("LatLonBox")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("LatLonBox"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("north")) ov.north = xml.readElementText().toDouble();
                else if (xml.name() == QLatin1String("south")) ov.south = xml.readElementText().toDouble();
                else if (xml.name() == QLatin1String("east")) ov.east = xml.readElementText().toDouble();
                else if (xml.name() == QLatin1String("west")) ov.west = xml.readElementText().toDouble();
                else if (xml.name() == QLatin1String("rotation")) ov.rotation = xml.readElementText().toDouble();
            }
        }
    }
    return ov;
}

void parseContainer(QXmlStreamReader &xml, const QString &endTag, KmlDocument &doc,
                    const QString &feedId, const QString &parentPath, bool parentVisible) {
    QString containerName;
    bool containerVisible = parentVisible;
    QString currentPath = parentPath;
    int folderIndex = -1;

    auto ensureFolder = [&]() {
        if (endTag != QLatin1String("Folder") || folderIndex >= 0)
            return;
        const QString label = containerName.isEmpty() ? QStringLiteral("Folder") : containerName;
        currentPath = parentPath.isEmpty() ? label : parentPath + QStringLiteral(" / ") + label;
        folderIndex = doc.folders.size();
        doc.folders.append({currentPath, label, containerVisible, 0});
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == endTag)
            break;
        if (!xml.isStartElement())
            continue;

        const QStringView tag = xml.name();
        if (tag == QLatin1String("name")) {
            containerName = xml.readElementText().trimmed();
            if (endTag == QLatin1String("Document") && doc.name.isEmpty())
                doc.name = containerName;
            if (endTag == QLatin1String("Folder"))
                ensureFolder();
        } else if (tag == QLatin1String("description")) {
            if (endTag == QLatin1String("Document") && doc.description.isEmpty())
                doc.description = KmlParser::stripHtml(xml.readElementText());
            else
                xml.skipCurrentElement();
        } else if (tag == QLatin1String("visibility")) {
            containerVisible = parentVisible && (xml.readElementText().trimmed() != QLatin1String("0"));
            if (folderIndex >= 0)
                doc.folders[folderIndex].visible = containerVisible;
        } else if (tag == QLatin1String("Style")) {
            KmlStyle style = parseStyleElement(xml);
            if (!style.id.isEmpty())
                doc.styles.insert(style.id, style);
        } else if (tag == QLatin1String("StyleMap")) {
            parseStyleMapElement(xml, doc);
        } else if (tag == QLatin1String("Document") || tag == QLatin1String("Folder")) {
            ensureFolder();
            parseContainer(xml, tag.toString(), doc, feedId, currentPath, containerVisible);
        } else if (tag == QLatin1String("Placemark")) {
            ensureFolder();
            KmlPlacemark pm = parsePlacemarkElement(xml, feedId, currentPath, containerVisible, doc.placemarks.size());
            if (pm.hasPoint || !pm.lines.isEmpty() || !pm.polygons.isEmpty()) {
                doc.placemarks.append(pm);
                if (folderIndex >= 0)
                    doc.folders[folderIndex].placemarkCount++;
            }
        } else if (tag == QLatin1String("NetworkLink")) {
            ensureFolder();
            KmlNetworkLink nl = parseNetworkLinkElement(xml, currentPath, containerVisible);
            if (!nl.href.isEmpty())
                doc.networkLinks.append(nl);
        } else if (tag == QLatin1String("GroundOverlay")) {
            ensureFolder();
            KmlGroundOverlay ov = parseGroundOverlayElement(xml, currentPath, containerVisible);
            if (!ov.iconHref.isEmpty())
                doc.groundOverlays.append(ov);
        } else {
            xml.skipCurrentElement();
        }
    }
}

KmlStyle resolveStyleUrl(const QString &rawUrl, const KmlDocument &doc) {
    QString key = rawUrl.trimmed();
    const int hash = key.indexOf('#');
    if (hash >= 0)
        key = key.mid(hash + 1);

    if (doc.styleMaps.contains(key)) {
        QString mapped = doc.styleMaps.value(key);
        const int mappedHash = mapped.indexOf('#');
        if (mappedHash >= 0)
            mapped = mapped.mid(mappedHash + 1);
        if (doc.styles.contains(mapped))
            return doc.styles.value(mapped);
    }
    if (doc.styles.contains(key))
        return doc.styles.value(key);
    return {};
}

} // namespace

KmlDocument KmlParser::parseBytes(const QByteArray &data, const QString &sourceName, const QString &feedId) {
    // Detect ZIP magic header ("PK\x03\x04") so KMZ payloads from URLs work
    // even when the URL does not end in ".kmz".
    if (data.size() >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04)
        return parseKmzBytes(data, sourceName, feedId);

    KmlDocument doc;

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QStringView tag = xml.name();
        if (tag == QLatin1String("Document") || tag == QLatin1String("Folder")) {
            parseContainer(xml, tag.toString(), doc, feedId, {}, true);
        } else if (tag == QLatin1String("Placemark")) {
            KmlPlacemark pm = parsePlacemarkElement(xml, feedId, {}, true, doc.placemarks.size());
            if (pm.hasPoint || !pm.lines.isEmpty() || !pm.polygons.isEmpty())
                doc.placemarks.append(pm);
        } else if (tag == QLatin1String("NetworkLink")) {
            KmlNetworkLink nl = parseNetworkLinkElement(xml, {}, true);
            if (!nl.href.isEmpty())
                doc.networkLinks.append(nl);
        } else if (tag == QLatin1String("Style")) {
            KmlStyle style = parseStyleElement(xml);
            if (!style.id.isEmpty())
                doc.styles.insert(style.id, style);
        } else if (tag == QLatin1String("StyleMap")) {
            parseStyleMapElement(xml, doc);
        }
    }

    if (xml.hasError() && doc.placemarks.isEmpty() && doc.networkLinks.isEmpty()) {
        doc.error = QStringLiteral("KML parse error at line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return doc;
    }

    // Resolve shared styles onto placemarks so the renderer has flat values.
    for (auto &pm : doc.placemarks) {
        if (!pm.hasInlineStyle && !pm.styleUrl.isEmpty())
            pm.style = resolveStyleUrl(pm.styleUrl, doc);
    }

    if (doc.name.isEmpty())
        doc.name = sourceName.isEmpty() ? QStringLiteral("KML Feed") : sourceName;
    return doc;
}

KmlDocument KmlParser::parseKmzBytes(const QByteArray &zipData, const QString &sourceName, const QString &feedId) {
    KmlDocument doc;
    doc.name = sourceName;

    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_source_t *src = zip_source_buffer_create(zipData.constData(), static_cast<zip_uint64_t>(zipData.size()), 0, &zerr);
    if (!src) {
        doc.error = QStringLiteral("KMZ buffer error: %1").arg(zip_error_strerror(&zerr));
        zip_error_fini(&zerr);
        return doc;
    }

    zip_t *archive = zip_open_from_source(src, ZIP_RDONLY, &zerr);
    if (!archive) {
        doc.error = QStringLiteral("KMZ open error: %1").arg(zip_error_strerror(&zerr));
        zip_source_free(src);
        zip_error_fini(&zerr);
        return doc;
    }

    QByteArray kmlBytes;
    QString kmlEntryName;
    QHash<QString, QImage> images;

    const zip_int64_t count = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < count; ++i) {
        struct zip_stat st;
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &st) != 0)
            continue;
        if (!(st.valid & ZIP_STAT_NAME) || !(st.valid & ZIP_STAT_SIZE) || st.size == 0)
            continue;

        const QString entryName = QString::fromUtf8(st.name);
        const bool isKml = entryName.endsWith(QStringLiteral(".kml"), Qt::CaseInsensitive);
        const bool isImg = entryName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
                           entryName.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) ||
                           entryName.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);

        if (!isKml && !isImg)
            continue;
        if (st.size > 64 * 1024 * 1024)
            continue;

        zip_file_t *zf = zip_fopen_index(archive, static_cast<zip_uint64_t>(i), 0);
        if (!zf)
            continue;

        QByteArray buf(static_cast<int>(st.size), Qt::Uninitialized);
        const zip_int64_t read = zip_fread(zf, buf.data(), st.size);
        zip_fclose(zf);
        if (read <= 0)
            continue;
        buf.truncate(static_cast<int>(read));

        if (isKml) {
            // Prefer doc.kml at root, otherwise the first KML entry found.
            if (kmlBytes.isEmpty() || entryName.compare(QStringLiteral("doc.kml"), Qt::CaseInsensitive) == 0) {
                kmlBytes = buf;
                kmlEntryName = entryName;
            }
        } else if (isImg) {
            QImage img;
            if (img.loadFromData(buf))
                images.insert(entryName, img);
        }
    }

    zip_close(archive);
    zip_error_fini(&zerr);

    if (kmlBytes.isEmpty()) {
        doc.error = QStringLiteral("KMZ archive contains no .kml document.");
        return doc;
    }

    doc = parseBytes(kmlBytes, sourceName.isEmpty() ? kmlEntryName : sourceName, feedId);
    doc.embeddedImages = images;
    return doc;
}

KmlDocument KmlParser::parseFile(const QString &filePath, const QString &feedId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        KmlDocument doc;
        doc.error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return doc;
    }
    return parseBytes(file.readAll(), QFileInfo(filePath).fileName(), feedId);
}

namespace {

double cross2D(const GeoCoord &a, const GeoCoord &b, const GeoCoord &c) {
    return (b.lon - a.lon) * (c.lat - a.lat) - (b.lat - a.lat) * (c.lon - a.lon);
}

bool pointInTriangle(const GeoCoord &p, const GeoCoord &a, const GeoCoord &b, const GeoCoord &c) {
    const double d1 = cross2D(p, a, b);
    const double d2 = cross2D(p, b, c);
    const double d3 = cross2D(p, c, a);
    const bool hasNeg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    const bool hasPos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
    return !(hasNeg && hasPos);
}

} // namespace

QVector<int> KmlParser::triangulateRing(const QVector<GeoCoord> &ring) {
    QVector<int> indices;
    int n = ring.size();
    if (n >= 2 && std::abs(ring.first().lon - ring.last().lon) < 1e-9 &&
        std::abs(ring.first().lat - ring.last().lat) < 1e-9) {
        --n;
    }
    if (n < 3)
        return indices;

    QVector<int> V(n);
    double area2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto &a = ring[i];
        const auto &b = ring[(i + 1) % n];
        area2 += (a.lon * b.lat - b.lon * a.lat);
    }

    // Ensure counter-clockwise winding for ear clipping.
    if (area2 >= 0.0) {
        for (int i = 0; i < n; ++i) V[i] = i;
    } else {
        for (int i = 0; i < n; ++i) V[i] = n - 1 - i;
    }

    int guard = n * n;
    while (V.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const int m = V.size();
        for (int i = 0; i < m; ++i) {
            const int prev = V[(i + m - 1) % m];
            const int curr = V[i];
            const int next = V[(i + 1) % m];
            if (cross2D(ring[prev], ring[curr], ring[next]) <= 1e-12)
                continue;

            bool containsOther = false;
            for (int j = 0; j < m; ++j) {
                const int test = V[j];
                if (test == prev || test == curr || test == next)
                    continue;
                if (pointInTriangle(ring[test], ring[prev], ring[curr], ring[next])) {
                    containsOther = true;
                    break;
                }
            }
            if (containsOther)
                continue;

            indices.append(prev);
            indices.append(curr);
            indices.append(next);
            V.remove(i);
            clipped = true;
            break;
        }
        if (!clipped)
            break;
    }

    if (V.size() == 3) {
        indices.append(V[0]);
        indices.append(V[1]);
        indices.append(V[2]);
    } else if (indices.isEmpty() && n >= 3) {
        // Fan fallback for degenerate or self-intersecting rings.
        for (int i = 1; i < n - 1; ++i) {
            indices.append(0);
            indices.append(i);
            indices.append(i + 1);
        }
    }
    return indices;
}
