// needleeye — 3D Earth globe and live KML/KMZ/CZML situational viewer for Omarchy.

#include <QCoreApplication>
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
#include "cli.h"
#include "dbusservice.h"
#include "globeview.h"
#include "idleinhibitor.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs.append(QString::fromLocal8Bit(argv[i]));

    // Headless CLI commands (D-Bus remote control, local file inspect, skill install)
    // require no display server or OpenGL context.
    if (Cli::isCliInvocation(rawArgs)) {
        QCoreApplication cliApp(argc, argv);
        QCoreApplication::setApplicationName("needleeye");
        QCoreApplication::setApplicationVersion("0.1.0");
        return Cli::run(rawArgs);
    }

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

    // Set identity before QGuiApplication initializes QPA so xdg-desktop-portal
    // receives the final app ID once instead of rejecting a second registration.
    QCoreApplication::setApplicationName("needleeye");
    QCoreApplication::setApplicationVersion("0.1.0");
    QGuiApplication::setDesktopFileName("needleeye");

    QGuiApplication app(argc, argv);
    // Allow high-resolution NASA Blue Marble / single-image world textures (up to 512 MB).
    QImageReader::setAllocationLimit(512);
    app.setWindowIcon(QIcon::fromTheme("needleeye"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    // Open any local .kml/.kmz/.czml files or feed URLs passed on the command line
    // (supports both `needleeye file.kml` and `needleeye open file.kml`).
    const QStringList args = app.arguments();
    int startIdx = (args.size() >= 2 && args[1] == QLatin1String("open")) ? 2 : 1;
    for (int i = startIdx; i < args.size(); ++i) {
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

    auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *globeView = rootWindow ? rootWindow->findChild<GlobeView *>(QStringLiteral("globeView")) : nullptr;

    // Prevent Hyprland / Omarchy screensaver during active 3D SpaceMouse input
    // (windowed or fullscreen) and while the window is fullscreen.
    IdleInhibitor idleInhibitor(rootWindow);
    if (auto *spaceNav = backend.spaceNav()) {
        QObject::connect(spaceNav, &SpaceNavController::motionReceived,
                         &idleInhibitor, &IdleInhibitor::notifyActivity);
        QObject::connect(spaceNav, &SpaceNavController::buttonPressed,
                         &idleInhibitor, &IdleInhibitor::notifyActivity);
    }

    DbusService dbusService(&backend, globeView, rootWindow);
    dbusService.registerOnSessionBus();

    return app.exec();
}
