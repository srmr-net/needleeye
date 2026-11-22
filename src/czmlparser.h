#pragma once

#include <QByteArray>
#include <QString>

#include "kmlparser.h"

class CzmlParser {
public:
    static bool looksLikeCzml(const QByteArray &data, const QString &sourceName = {});
    static KmlDocument parseBytes(const QByteArray &data, const QString &sourceName = {},
                                  const QString &feedId = {});
    static KmlDocument parseFile(const QString &filePath, const QString &feedId = {});

    // Converts WGS84 ECEF Cartesian coordinates (meters) to geodetic (lon deg, lat deg, alt m).
    static GeoCoord ecefToGeodetic(double x, double y, double z);
};
