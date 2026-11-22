#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <zip.h>

#include "backend.h"
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
        Backend backend(&tm, &picker, false);

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
