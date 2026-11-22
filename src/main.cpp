// needleeye — 3D Earth globe and live KML/KMZ situational viewer for Omarchy.

#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QUrl>

#include "backend.h"
#include "globeview.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // QQuickFramebufferObject requires the OpenGL scenegraph backend in Qt 6.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName("needleeye");
    app.setApplicationVersion("0.1.0");
    // Allow high-resolution NASA Blue Marble / single-image world textures (up to 512 MB).
    QImageReader::setAllocationLimit(512);

    // Matches the window to needleeye.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("needleeye");
    app.setWindowIcon(QIcon::fromTheme("needleeye"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    // Open any local .kml/.kmz files passed on the command line.
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].startsWith('-'))
            continue;
        if (QFileInfo::exists(args[i]))
            backend.loadLocalFile(args[i]);
        else if (args[i].startsWith(QStringLiteral("http://")) || args[i].startsWith(QStringLiteral("https://")))
            backend.addFeedUrl(args[i]);
    }

    qmlRegisterType<GlobeView>("Needleeye", 1, 0, "GlobeView");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
