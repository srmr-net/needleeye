#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <zip.h>

#include "backend.h"
#include "cli.h"
#include "czmlparser.h"
#include "dbusservice.h"
#include "kmlparser.h"
#include "theme.h"
#include "tilemanager.h"
#include "translator.h"

namespace {

class FakeFilePicker : public FilePicker {
public:
    QString nextPath;
    void openKmlFile(const QString &, Callback callback) override {
        if (callback)
            callback(nextPath, {});
    }
    void openImageFile(const QString &, Callback callback) override {
        if (callback)
            callback(nextPath, {});
    }
};

QByteArray sampleHazardKml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        "  <Document>\n"
        "    <name>Sample Hazard Feed</name>\n"
        "    <Style id=\"eqStyle\">\n"
        "      <IconStyle><color>ff0000ff</color><scale>1.4</scale></IconStyle>\n"
        "      <LineStyle><color>ffffaa00</color><width>3</width></LineStyle>\n"
        "      <PolyStyle><color>660055ff</color><fill>1</fill><outline>1</outline></PolyStyle>\n"
        "    </Style>\n"
        "    <StyleMap id=\"eqMap\">\n"
        "      <Pair><key>normal</key><styleUrl>#eqStyle</styleUrl></Pair>\n"
        "    </StyleMap>\n"
        "    <Folder>\n"
        "      <name>M 5.0+</name>\n"
        "      <Placemark id=\"us70001\">\n"
        "        <name>M 5.8 - 42 km E of Honshu, Japan</name>\n"
        "        <styleUrl>#eqMap</styleUrl>\n"
        "        <TimeStamp><when>2026-11-20T08:30:00Z</when></TimeStamp>\n"
        "        <LookAt><longitude>141.5</longitude><latitude>38.2</latitude><range>450000</range></LookAt>\n"
        "        <description><![CDATA[<p>Depth: <b>35 km</b> <a href=\"https://earthquake.usgs.gov/earthquakes/eventpage/us70001\">Details</a></p>]]></description>\n"
        "        <Point><coordinates>141.5,38.2,-35000</coordinates></Point>\n"
        "      </Placemark>\n"
        "    </Folder>\n"
        "    <Folder>\n"
        "      <name>Tropical Cyclones</name>\n"
        "      <Placemark id=\"tc1\">\n"
        "        <name>Cyclone Cone</name>\n"
        "        <styleUrl>#eqStyle</styleUrl>\n"
        "        <MultiGeometry>\n"
        "          <LineString><coordinates>130.0,15.0,0 132.0,18.0,0 135.0,22.0,0</coordinates></LineString>\n"
        "          <Polygon>\n"
        "            <outerBoundaryIs><LinearRing><coordinates>\n"
        "              130.0,14.0,0 136.0,14.0,0 136.0,23.0,0 130.0,23.0,0 130.0,14.0,0\n"
        "            </coordinates></LinearRing></outerBoundaryIs>\n"
        "          </Polygon>\n"
        "        </MultiGeometry>\n"
        "      </Placemark>\n"
        "      <NetworkLink>\n"
        "        <name>Live Update</name>\n"
        "        <Link><href>https://example.org/live.kml</href><refreshInterval>300</refreshInterval></Link>\n"
        "      </NetworkLink>\n"
        "    </Folder>\n"
        "  </Document>\n"
        "</kml>\n");
}

QByteArray sampleKmzDoc() {
    return QByteArray(
        "<kml><Document><name>KMZ Test</name>"
        "<Placemark><name>Pin A</name><Point><coordinates>12.5,55.6,0</coordinates></Point></Placemark>"
        "</Document></kml>");
}

QByteArray sampleTimelineKml() {
    return QByteArray(
        "<kml><Document><name>Quakes</name>"
        "<Folder><name>Old</name>"
        "<Placemark id=\"p1\"><name>Quake Alpha</name><TimeStamp><when>2026-11-10T00:00:00Z</when></TimeStamp><Point><coordinates>10,20,0</coordinates></Point></Placemark>"
        "</Folder>"
        "<Folder><name>New</name>"
        "<Placemark id=\"p2\"><name>Quake Beta</name><TimeStamp><when>2026-11-20T00:00:00Z</when></TimeStamp><Point><coordinates>30,40,0</coordinates></Point></Placemark>"
        "</Folder>"
        "</Document></kml>");
}

} // namespace

class NeedleeyeTests : public QObject {
    Q_OBJECT

private slots:
    void kmlParsesAbgrColors() {
        const QColor c = KmlParser::parseKmlColor(QStringLiteral("80ff8800"));
        QCOMPARE(c.alpha(), 0x80);
        QCOMPARE(c.blue(), 0xff);
        QCOMPARE(c.green(), 0x88);
        QCOMPARE(c.red(), 0x00);
    }

    void kmlParsesUsgsAndGdacsPrimitives() {
        const KmlDocument doc = KmlParser::parseBytes(sampleHazardKml(), QStringLiteral("sample.kml"), QStringLiteral("test"));
        QVERIFY(doc.error.isEmpty());
        QCOMPARE(doc.name, QStringLiteral("Sample Hazard Feed"));
        QCOMPARE(doc.folders.size(), 2);
        QCOMPARE(doc.placemarks.size(), 2);
        QCOMPARE(doc.networkLinks.size(), 1);
        QCOMPARE(doc.networkLinks.first().href, QStringLiteral("https://example.org/live.kml"));
        QCOMPARE(doc.networkLinks.first().refreshIntervalSec, 300);

        const auto &eq = doc.placemarks[0];
        QCOMPARE(eq.folderPath, QStringLiteral("M 5.0+"));
        QVERIFY(eq.hasPoint);
        QCOMPARE(eq.point.lon, 141.5);
        QCOMPARE(eq.point.lat, 38.2);
        QCOMPARE(eq.style.iconColor, QColor(255, 0, 0, 255));
        QCOMPARE(eq.linkUrl, QStringLiteral("https://earthquake.usgs.gov/earthquakes/eventpage/us70001"));
        QVERIFY(eq.timeStart.isValid());
        QVERIFY(eq.lookAt.valid);

        const auto &tc = doc.placemarks[1];
        QCOMPARE(tc.lines.size(), 1);
        QCOMPARE(tc.polygons.size(), 1);
        QCOMPARE(tc.polygons.first().outer.size(), 5);
    }

    void kmlExtractsKmzArchive() {
        QTemporaryDir dir;
        const QString kmzPath = dir.filePath(QStringLiteral("test.kmz"));
        int err = 0;
        zip_t *za = zip_open(kmzPath.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        QVERIFY(za != nullptr);
        const QByteArray kml = sampleKmzDoc();
        zip_source_t *src = zip_source_buffer(za, kml.constData(), static_cast<zip_uint64_t>(kml.size()), 0);
        QVERIFY(zip_file_add(za, "doc.kml", src, ZIP_FL_ENC_UTF_8) >= 0);
        QCOMPARE(zip_close(za), 0);

        const KmlDocument doc = KmlParser::parseFile(kmzPath, QStringLiteral("kmz"));
        QVERIFY(doc.error.isEmpty());
        QCOMPARE(doc.name, QStringLiteral("KMZ Test"));
        QCOMPARE(doc.placemarks.size(), 1);
        QCOMPARE(doc.placemarks.first().name, QStringLiteral("Pin A"));
    }

    void polygonTriangulationHandlesConcaveRing() {
        // Concave L-shape polygon.
        QVector<GeoCoord> ring = {
            {0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {1, 1, 0}, {1, 4, 0}, {0, 4, 0}, {0, 0, 0}
        };
        const QVector<int> tris = KmlParser::triangulateRing(ring);
        QCOMPARE(tris.size(), 12); // 6 vertices -> 4 triangles -> 12 indices
    }

    void backendIngestsFeedsFiltersFoldersAndTimeline() {
        QTemporaryDir cacheDir;
        TileManager tm(cacheDir.path());
        FakeFilePicker picker;
        Backend backend(&tm, &picker, false, cacheDir.filePath(QStringLiteral("settings.json")));

        QVERIFY(backend.ingestKmlBytesForFeed(QStringLiteral("local-test"), sampleTimelineKml(), QStringLiteral("Quakes")));
        QCOMPARE(backend.visiblePlacemarks(), 2);
        QVERIFY(backend.hasTimeline());

        backend.toggleFolder(QStringLiteral("local-test"), QStringLiteral("Old"));
        QCOMPARE(backend.visiblePlacemarks(), 1);
        backend.toggleFolder(QStringLiteral("local-test"), QStringLiteral("Old"));
        QCOMPARE(backend.visiblePlacemarks(), 2);

        backend.setTimelineProgress(0.05);
        QCOMPARE(backend.visiblePlacemarks(), 1);

        backend.setTimelineActive(false);
        backend.updateSearch(QStringLiteral("Beta"));
        QVERIFY(!backend.searchResults().isEmpty());
        QCOMPARE(backend.searchResults().first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Quake Beta"));
    }

    void tileUrlExpandsXyzTmsAndSubdomain() {
        QCOMPARE(TileManager::expandUrlTemplate(QStringLiteral("https://{s}.tiles.test/{z}/{x}/{y}.png"), 3, 2, 1),
                 QStringLiteral("https://a.tiles.test/3/2/1.png"));
        // For z=3 (8 rows), y=1 -> tmsY = 7 - 1 = 6; (2+1)%3 == 0 ('a'), (2+2)%3 == 1 ('b')
        QCOMPARE(TileManager::expandUrlTemplate(QStringLiteral("https://{s}.tms.test/{z}/{x}/{-y}.png"), 3, 2, 2),
                 QStringLiteral("https://b.tms.test/3/2/5.png"));
    }

    void baseMapManagementValidatesPersistsAndClearsCache() {
        QTemporaryDir dir;
        const QString settingsFile = dir.filePath(QStringLiteral("settings.json"));
        TileManager tm(dir.filePath(QStringLiteral("tiles")));
        FakeFilePicker picker;
        Backend backend(&tm, &picker, false, settingsFile);

        // Validation checks.
        QVERIFY(!Backend::validateBaseMapInput(QString(), QStringLiteral("https://x/{z}/{x}/{y}.png"), 18).isEmpty());
        QVERIFY(!Backend::validateBaseMapInput(QStringLiteral("Bad"), QStringLiteral("ftp://x/{z}/{x}/{y}.png"), 18).isEmpty());
        QVERIFY(!Backend::validateBaseMapInput(QStringLiteral("Missing"), QStringLiteral("https://x/{z}/{x}.png"), 18).isEmpty());
        QVERIFY(Backend::validateBaseMapInput(QStringLiteral("Good TMS"), QStringLiteral("https://x/{z}/{x}/{-y}.png"), 16).isEmpty());

        const int initialCount = backend.baseMapsVariant().size();
        const QString addErr = backend.addCustomBaseMap(
            QStringLiteral("My Radar"),
            QStringLiteral("https://radar.example/{z}/{x}/{y}.png"),
            15,
            QStringLiteral("Radar Co"));
        QVERIFY(addErr.isEmpty());
        QCOMPARE(backend.baseMapsVariant().size(), initialCount + 1);
        QCOMPARE(backend.baseLayerName(), QStringLiteral("My Radar"));
        const QString customId = backend.baseLayerId();

        // Update custom base map and move it up.
        QVERIFY(backend.updateBaseMap(customId, QStringLiteral("My Radar HD"),
                                      QStringLiteral("https://radar2.example/{z}/{x}/{y}.png"), 17,
                                      QStringLiteral("Radar Co")).isEmpty());
        QVERIFY(backend.moveBaseMap(customId, -1));
        backend.setTerrainExaggeration(2.2);

        // Reload in a fresh Backend instance to verify atomic JSON persistence.
        Backend reloaded(&tm, &picker, false, settingsFile);
        QCOMPARE(reloaded.baseLayerName(), QStringLiteral("My Radar HD"));
        QCOMPARE(reloaded.baseMapsVariant().size(), initialCount + 1);
        QVERIFY(std::abs(reloaded.terrainExaggeration() - 2.2) < 0.01);

        // Prevent disabling the last remaining enabled base map.
        for (const auto &v : reloaded.baseMapsVariant()) {
            const QString id = v.toMap().value(QStringLiteral("id")).toString();
            if (id != customId)
                reloaded.setBaseMapEnabled(id, false);
        }
        QVERIFY(!reloaded.setBaseMapEnabled(customId, false));

        // Remove custom map and verify fallback to a valid enabled map.
        QVERIFY(reloaded.removeBaseMap(customId));
        QCOMPARE(reloaded.baseMapsVariant().size(), initialCount);
        QVERIFY(!reloaded.enabledBaseMapsVariant().isEmpty());

        // Verify disk cache stats & clearTileCache.
        QDir().mkpath(dir.filePath(QStringLiteral("tiles/dark-matter/1/0")));
        write(dir.filePath(QStringLiteral("tiles/dark-matter/1/0/0.tile")), QByteArray(128, 'x'));
        reloaded.refreshCacheStats();
        QVERIFY(reloaded.cacheSummary().contains(QStringLiteral("128 B")));
        reloaded.clearTileCache();
        QCOMPARE(reloaded.cacheSummary(), QStringLiteral("Cache empty"));
    }

    void singleImageMapsValidatePersistAndLoadLocalFile() {
        QTemporaryDir dir;
        const QString settingsFile = dir.filePath(QStringLiteral("settings.json"));
        const QString imgPath = dir.filePath(QStringLiteral("world.png"));
        QImage synthetic(64, 32, QImage::Format_RGBA8888);
        synthetic.fill(QColor(20, 40, 80, 255));
        QVERIFY(synthetic.save(imgPath));

        // Validation for equirectangular & mercator single images.
        QVERIFY(Backend::validateBaseMapInput(QStringLiteral("Earth Night"),
                                              QStringLiteral("https://cdn.jsdelivr.net/npm/three-globe/example/img/earth-night.jpg"),
                                              10, QStringLiteral("equirectangular")).isEmpty());
        QVERIFY(Backend::validateBaseMapInput(QStringLiteral("Local Mercator"),
                                              imgPath, 10, QStringLiteral("mercator")).isEmpty());
        QVERIFY(!Backend::validateBaseMapInput(QStringLiteral("Missing File"),
                                               dir.filePath(QStringLiteral("does-not-exist.jpg")),
                                               10, QStringLiteral("equirectangular")).isEmpty());

        TileManager tm(dir.filePath(QStringLiteral("tiles")));
        FakeFilePicker picker;
        Backend backend(&tm, &picker, false, settingsFile);

        const QString err = backend.addCustomBaseMap(QStringLiteral("Local Blue Marble"),
                                                     imgPath, 10, QStringLiteral("NASA"),
                                                     QStringLiteral("mercator"));
        QVERIFY(err.isEmpty());
        const int uid = backend.baseLayer();
        QCOMPARE(tm.projectionForLayer(uid), MapProjection::SingleMercator);

        const QImage loaded = tm.requestSingleImage(uid);
        QVERIFY(!loaded.isNull());
        QCOMPARE(loaded.width(), 64);
        QCOMPARE(loaded.height(), 32);

        // Verify projection survives settings reload.
        Backend reloaded(&tm, &picker, false, settingsFile);
        QCOMPARE(reloaded.baseLayerName(), QStringLiteral("Local Blue Marble"));
        QCOMPARE(tm.projectionForLayer(reloaded.baseLayer()), MapProjection::SingleMercator);
    }

    void sunDirectionMatchesSolsticesAndEquinox() {
        double lat = 0.0, lon = 0.0;

        // June solstice at 12:00 UTC: Sun near Tropic of Cancer (+23.4 deg) & Prime Meridian (~0 deg).
        const QDateTime juneSolstice(QDate(2026, 6, 21), QTime(12, 0, 0), QTimeZone::utc());
        const QVector3D juneDir = Backend::computeSunDirection(juneSolstice, &lat, &lon);
        QVERIFY(std::abs(juneDir.length() - 1.0f) < 1e-4f);
        QVERIFY2(lat > 22.5 && lat < 24.0, qPrintable(QString("Unexpected June lat: %1").arg(lat)));
        QVERIFY2(std::abs(lon) < 3.0, qPrintable(QString("Unexpected June noon lon: %1").arg(lon)));

        // December solstice at 00:00 UTC: Sun near Tropic of Capricorn (-23.4 deg) & antimeridian (~±180 deg).
        const QDateTime decSolstice(QDate(2026, 12, 21), QTime(0, 0, 0), QTimeZone::utc());
        Backend::computeSunDirection(decSolstice, &lat, &lon);
        QVERIFY2(lat < -22.5 && lat > -24.0, qPrintable(QString("Unexpected Dec lat: %1").arg(lat)));
        QVERIFY2(std::abs(lon) > 175.0, qPrintable(QString("Unexpected Dec midnight lon: %1").arg(lon)));

        // March equinox at 12:00 UTC: Sun near equator (~0 deg).
        const QDateTime marchEquinox(QDate(2026, 3, 20), QTime(12, 0, 0), QTimeZone::utc());
        Backend::computeSunDirection(marchEquinox, &lat, &lon);
        QVERIFY2(std::abs(lat) < 1.5, qPrintable(QString("Unexpected Equinox lat: %1").arg(lat)));

        // Verify dayNightEnabled persists cleanly.
        QTemporaryDir dir;
        const QString settingsFile = dir.filePath(QStringLiteral("settings.json"));
        TileManager tm(dir.filePath(QStringLiteral("tiles")));
        FakeFilePicker picker;
        Backend b1(&tm, &picker, false, settingsFile);
        QCOMPARE(b1.dayNightEnabled(), false);
        b1.setDayNightEnabled(true);
        Backend b2(&tm, &picker, false, settingsFile);
        QCOMPARE(b2.dayNightEnabled(), true);
    }

    void czmlParsesDocumentEcefSamplesPolygonsAndAnimatesTimeline() {
        // Verify WGS84 ECEF -> geodetic conversion on equator + 400 km LEO altitude.
        const GeoCoord eqPt = CzmlParser::ecefToGeodetic(6378137.0 + 400000.0, 0.0, 0.0);
        QVERIFY(std::abs(eqPt.lon) < 1e-4);
        QVERIFY(std::abs(eqPt.lat) < 1e-4);
        QVERIFY(std::abs(eqPt.alt - 400000.0) < 5.0);

        const QByteArray sampleCzml =
            "[\n"
            "  {\"id\":\"document\",\"name\":\"Orbital Demo\",\"version\":\"1.0\","
            "   \"clock\":{\"interval\":\"2026-11-22T00:00:00Z/2026-11-22T01:00:00Z\"}},\n"
            "  {\"id\":\"grp\",\"name\":\"Satellites\"},\n"
            "  {\"id\":\"sat-1\",\"name\":\"ISS\",\"parent\":\"grp\","
            "   \"availability\":\"2026-11-22T00:00:00Z/2026-11-22T01:00:00Z\","
            "   \"description\":\"<p>Low Earth Orbit <a href=\\\"https://example.org/iss\\\">Live</a></p>\","
            "   \"position\":{\"epoch\":\"2026-11-22T00:00:00Z\","
            "     \"cartographicDegrees\":[0, 0.0, 10.0, 420000, 3600, 60.0, 40.0, 420000]},"
            "   \"point\":{\"color\":{\"rgba\":[0,255,200,255]},\"pixelSize\":12},"
            "   \"path\":{\"material\":{\"solidColor\":{\"color\":{\"rgbaf\":[0.2,0.8,1.0,1.0]}}},\"width\":2},"
            "   \"properties\":{\"norad\":\"25544\"}},\n"
            "  {\"id\":\"zone-1\",\"name\":\"Coverage Footprints\","
            "   \"position\":{\"cartographicDegrees\":[-105.0, 40.0, 0]},"
            "   \"ellipse\":{\"semiMajorAxis\":200000,\"semiMinorAxis\":120000,"
            "     \"material\":{\"solidColor\":{\"color\":{\"rgba\":[255,100,0,120]}}}}}\n"
            "]\n";

        QVERIFY(CzmlParser::looksLikeCzml(sampleCzml, QStringLiteral("orbit.czml")));
        const KmlDocument doc = CzmlParser::parseBytes(sampleCzml, QStringLiteral("orbit.czml"), QStringLiteral("cz"));
        QVERIFY(doc.error.isEmpty());
        QCOMPARE(doc.name, QStringLiteral("Orbital Demo"));
        QCOMPARE(doc.placemarks.size(), 2);
        QCOMPARE(doc.folders.size(), 1);
        QCOMPARE(doc.folders.first().name, QStringLiteral("Satellites"));

        const auto &iss = doc.placemarks[0];
        QCOMPARE(iss.name, QStringLiteral("ISS"));
        QCOMPARE(iss.folderPath, QStringLiteral("Satellites"));
        QCOMPARE(iss.samples.size(), 2);
        QCOMPARE(iss.lines.size(), 1);
        QCOMPARE(iss.linkUrl, QStringLiteral("https://example.org/iss"));
        QCOMPARE(iss.extendedData.value(QStringLiteral("norad")), QStringLiteral("25544"));
        QCOMPARE(iss.style.iconColor, QColor(0, 255, 200, 255));

        // Midpoint interpolation should land at lon=30, lat=25, alt=420000.
        const qint64 midMs = QDateTime(QDate(2026, 11, 22), QTime(0, 30, 0), QTimeZone::utc()).toMSecsSinceEpoch();
        const GeoCoord mid = iss.positionAt(midMs);
        QVERIFY(std::abs(mid.lon - 30.0) < 0.01);
        QVERIFY(std::abs(mid.lat - 25.0) < 0.01);
        QVERIFY(std::abs(mid.alt - 420000.0) < 1.0);

        const auto &zone = doc.placemarks[1];
        QCOMPARE(zone.polygons.size(), 1);
        QVERIFY(zone.polygons.first().outer.size() >= 48);

        // Verify Backend auto-detects CZML and moves the satellite as timeline progresses.
        QTemporaryDir dir;
        TileManager tm(dir.filePath(QStringLiteral("tiles")));
        FakeFilePicker picker;
        Backend backend(&tm, &picker, false, dir.filePath(QStringLiteral("settings.json")));
        QVERIFY(backend.ingestKmlBytesForFeed(QStringLiteral("czml-demo"), sampleCzml, QStringLiteral("orbit.czml")));
        QVERIFY(backend.hasTimeline());
        backend.setTimelineProgress(0.5);
        QCOMPARE(backend.visiblePlacemarkList().first().anchor().lon, 30.0);
    }

    void dbusServiceAndCliAutomateStateFeaturesAndSkillInstall() {
        QTemporaryDir dir;
        TileManager tm(dir.filePath(QStringLiteral("tiles")));
        FakeFilePicker picker;
        Backend backend(&tm, &picker, false, dir.filePath(QStringLiteral("settings.json")));
        DbusService svc(&backend);

        auto parseObj = [](const QString &json) {
            return QJsonDocument::fromJson(json.toUtf8()).object();
        };

        // Initial state & display toggles via DbusService.
        const QJsonObject st1 = parseObj(svc.GetState());
        QVERIFY(st1.value(QStringLiteral("ok")).toBool());
        QCOMPARE(st1.value(QStringLiteral("hudHidden")).toBool(), false);

        QVERIFY(parseObj(svc.SetHudHidden(true)).value(QStringLiteral("ok")).toBool());
        QVERIFY(parseObj(svc.SetDayNight(true)).value(QStringLiteral("ok")).toBool());
        QVERIFY(parseObj(svc.SetBaseMap(QStringLiteral("earth-night"))).value(QStringLiteral("ok")).toBool());
        QVERIFY(!parseObj(svc.SetBaseMap(QStringLiteral("nonexistent-map"))).value(QStringLiteral("ok")).toBool());

        const QJsonObject st2 = parseObj(svc.GetState());
        QCOMPARE(st2.value(QStringLiteral("hudHidden")).toBool(), true);
        QCOMPARE(st2.value(QStringLiteral("dayNight")).toBool(), true);
        QCOMPARE(st2.value(QStringLiteral("baseMapId")).toString(), QStringLiteral("earth-night"));

        // Load a local KML file through LoadSource, then query & select features.
        const QString kmlPath = dir.filePath(QStringLiteral("events.kml"));
        write(kmlPath, sampleTimelineKml());
        const QJsonObject loadRes = parseObj(svc.LoadSource(kmlPath, QStringLiteral("Test Quakes")));
        QVERIFY(loadRes.value(QStringLiteral("ok")).toBool());
        QCOMPARE(loadRes.value(QStringLiteral("visibleFeatures")).toInt(), 2);

        const QJsonObject featRes = parseObj(svc.ListFeatures(QStringLiteral("Beta"), 10));
        QVERIFY(featRes.value(QStringLiteral("ok")).toBool());
        QCOMPARE(featRes.value(QStringLiteral("returned")).toInt(), 1);

        const QJsonObject selRes = parseObj(svc.SelectFeature(QStringLiteral("Quake Beta"), false));
        QVERIFY(selRes.value(QStringLiteral("ok")).toBool());
        QCOMPARE(selRes.value(QStringLiteral("selected")).toBool(), true);

        const QJsonObject tlRes = parseObj(svc.SetTimeline(true, 0.05, false));
        QVERIFY(tlRes.value(QStringLiteral("ok")).toBool());
        QCOMPARE(backend.visiblePlacemarks(), 1);

        // Verify offline CLI file inspection & skill installation.
        QVERIFY(Cli::isCliInvocation({QStringLiteral("needleeye"), QStringLiteral("status")}));
        QVERIFY(Cli::isCliInvocation({QStringLiteral("needleeye"), QStringLiteral("fly"), QStringLiteral("10"), QStringLiteral("20")}));
        QVERIFY(!Cli::isCliInvocation({QStringLiteral("needleeye"), QStringLiteral("open"), kmlPath}));
        QVERIFY(!Cli::isCliInvocation({QStringLiteral("needleeye"), kmlPath}));

        const QJsonObject inspectRes = parseObj(Cli::inspectFileJson(kmlPath, 10));
        QVERIFY(inspectRes.value(QStringLiteral("ok")).toBool());
        QCOMPARE(inspectRes.value(QStringLiteral("format")).toString(), QStringLiteral("kml"));
        QCOMPARE(inspectRes.value(QStringLiteral("featureCount")).toInt(), 2);

        const QString skillOutDir = dir.filePath(QStringLiteral("skills/needleeye"));
        const QJsonObject skillRes = parseObj(Cli::installSkillJson(skillOutDir, QStringLiteral("# Needleeye Skill\n")));
        QVERIFY(skillRes.value(QStringLiteral("ok")).toBool());
        QVERIFY(QFileInfo::exists(skillOutDir + QStringLiteral("/SKILL.md")));
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccentAndBackground() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\nbackground = \"#101218\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#101218"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackAndPicksPlurals() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(NeedleeyeTests)
#include "needleeye_tests.moc"
