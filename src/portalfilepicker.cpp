// xdg-desktop-portal file chooser pattern adapted from Omacut (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QRandomGenerator>
#include <QUrl>

namespace {

struct FilterRule {
    uint type;
    QString pattern;
};

struct FileFilter {
    QString label;
    QList<FilterRule> rules;
};

QDBusArgument &operator<<(QDBusArgument &arg, const FilterRule &rule) {
    arg.beginStructure();
    arg << rule.type << rule.pattern;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FilterRule &rule) {
    arg.beginStructure();
    arg >> rule.type >> rule.pattern;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const FileFilter &filter) {
    arg.beginStructure();
    arg << filter.label << filter.rules;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FileFilter &filter) {
    arg.beginStructure();
    arg >> filter.label >> filter.rules;
    arg.endStructure();
    return arg;
}

} // namespace

Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(FileFilter)
Q_DECLARE_METATYPE(QList<FileFilter>)

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<FileFilter>();
    qDBusRegisterMetaType<QList<FileFilter>>();
}

void PortalFilePicker::openKmlFile(const QString &title, Callback callback) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (callback)
            callback({}, tr("Cannot open file picker: D-Bus session bus is unavailable."));
        return;
    }

    QDBusInterface portal(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.FileChooser"),
        bus);

    if (!portal.isValid()) {
        if (callback)
            callback({}, tr("Cannot open file picker: xdg-desktop-portal is unavailable."));
        return;
    }

    const QString token = QStringLiteral("needleeye_%1").arg(QRandomGenerator::global()->generate());
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("multiple"), false);

    QList<FileFilter> filters;
    filters.append({tr("KML / KMZ files"), {{0, QStringLiteral("*.kml")}, {0, QStringLiteral("*.kmz")}}});
    filters.append({tr("All files"), {{0, QStringLiteral("*")}}});
    options.insert(QStringLiteral("filters"), QVariant::fromValue(filters));

    // Predict the request path so we connect before the reply races in.
    QString sender = bus.baseService();
    sender.remove(0, 1);
    sender.replace('.', '_');
    const QString expectedPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    if (!m_activeRequestPath.isEmpty()) {
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(onPortalResponse(uint, QVariantMap)));
    }

    m_callback = std::move(callback);
    m_activeRequestPath = expectedPath;
    bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(onPortalResponse(uint, QVariantMap)));

    QDBusReply<QDBusObjectPath> reply = portal.call(QStringLiteral("OpenFile"), QString(), title, options);
    if (!reply.isValid()) {
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(onPortalResponse(uint, QVariantMap)));
        m_activeRequestPath.clear();
        auto cb = std::move(m_callback);
        if (cb)
            cb({}, tr("File picker failed: %1").arg(reply.error().message()));
        return;
    }

    // Rebind if the portal returned a different object path than predicted.
    if (reply.value().path() != m_activeRequestPath) {
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(onPortalResponse(uint, QVariantMap)));
        m_activeRequestPath = reply.value().path();
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                    QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                    this, SLOT(onPortalResponse(uint, QVariantMap)));
    }
}

void PortalFilePicker::onPortalResponse(uint response, const QVariantMap &results) {
    auto bus = QDBusConnection::sessionBus();
    if (!m_activeRequestPath.isEmpty()) {
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_activeRequestPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(onPortalResponse(uint, QVariantMap)));
        m_activeRequestPath.clear();
    }

    auto cb = std::move(m_callback);
    if (!cb || response != 0)
        return;

    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty())
        return;

    const QUrl url(uris.first());
    cb(url.isLocalFile() ? url.toLocalFile() : uris.first(), {});
}
