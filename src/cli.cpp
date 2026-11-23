#include "cli.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <cmath>
#include <limits>

#include "czmlparser.h"
#include "dbusservice.h"
#include "kmlparser.h"

namespace {

const QSet<QString> &cliCommands() {
    static const QSet<QString> cmds = {
        QStringLiteral("status"),
        QStringLiteral("fly"),
        QStringLiteral("reset"),
        QStringLiteral("map"),
        QStringLiteral("set"),
        QStringLiteral("feed"),
        QStringLiteral("features"),
        QStringLiteral("select"),
        QStringLiteral("timeline"),
        QStringLiteral("screenshot"),
        QStringLiteral("quit"),
        QStringLiteral("inspect"),
        QStringLiteral("skill"),
        QStringLiteral("help"),
        QStringLiteral("-h"),
        QStringLiteral("--help"),
        QStringLiteral("-v"),
        QStringLiteral("--version")
    };
    return cmds;
}

void writeOut(const QString &text) {
    QTextStream out(stdout);
    out << text;
    if (!text.endsWith('\n'))
        out << '\n';
}

void writeErr(const QString &text) {
    QTextStream err(stderr);
    err << text;
    if (!text.endsWith('\n'))
        err << '\n';
}

QString jsonError(const QString &msg) {
    QJsonObject o;
    o.insert(QStringLiteral("ok"), false);
    o.insert(QStringLiteral("error"), msg);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool parseBoolFlag(const QString &val, bool &out) {
    const QString s = val.trimmed().toLower();
    if (s == QLatin1String("on") || s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes")) {
        out = true;
        return true;
    }
    if (s == QLatin1String("off") || s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no")) {
        out = false;
        return true;
    }
    return false;
}

void printHelp() {
    writeOut(QStringLiteral(
        "needleeye 0.1.0 — 3D Earth globe and live KML/KMZ/CZML viewer for Omarchy\n"
        "\n"
        "Usage:\n"
        "  needleeye [open] [file|url...]          Launch GUI (optionally opening KML/KMZ/CZML)\n"
        "  needleeye <command> [options] [--json]  Control running window via D-Bus or run offline\n"
        "\n"
        "Live Window Commands (Session D-Bus: org.needleeye.App):\n"
        "  status                                  Show camera, base map, display flags, and counts\n"
        "  fly <lat> <lon> [--alt <km>] [--pitch <deg>] [--heading <deg>] [--instant]\n"
        "  reset [north|home]                      Reset north/pitch or return to home orbit\n"
        "  map list                                List configured base maps\n"
        "  map set <id|name|slot>                  Activate base map by id, name, or slot (1-9)\n"
        "  set [--terrain on|off] [--grid on|off] [--day-night on|off]\n"
        "      [--atmosphere on|off] [--hud on|off] [--sidebar on|off]\n"
        "  feed list                               List KML/KMZ/CZML feeds and opened files\n"
        "  feed add <file|url> [--name <label>]    Load a local file or remote feed URL\n"
        "  feed enable|disable|remove <id>         Toggle or remove a feed\n"
        "  feed refresh                            Refresh all active feeds\n"
        "  features [--query <text>] [--limit <n>] List or search visible features\n"
        "  select <id|name|none> [--no-fly]        Select a feature (or clear with 'none')\n"
        "  timeline [--active on|off] [--progress <0..1>] [--play|--pause]\n"
        "  screenshot <output.png>                 Capture live 3D globe window to an image\n"
        "  quit                                    Quit the running Needleeye window\n"
        "\n"
        "Offline Commands:\n"
        "  inspect <file> [--limit <n>]            Parse and summarize a local KML/KMZ/CZML file\n"
        "  skill show                              Print the bundled agent SKILL.md\n"
        "  skill install [--dir <path>]            Install the bundled agent SKILL.md\n"));
}

int emitResponse(const QString &jsonStr, bool asJson) {
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    const QJsonObject obj = doc.object();
    const bool ok = obj.value(QStringLiteral("ok")).toBool(false);

    if (asJson) {
        if (ok) {
            writeOut(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed());
            return 0;
        }
        writeErr(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        return 1;
    }

    if (!ok) {
        writeErr(QStringLiteral("error: ") + obj.value(QStringLiteral("error")).toString(QStringLiteral("Unknown error")));
        return 1;
    }

    // Concise human-readable output when --json is not set.
    if (obj.contains(QStringLiteral("camera"))) {
        const auto cam = obj.value(QStringLiteral("camera")).toObject();
        writeOut(QStringLiteral("camera:  %1, %2  alt=%3 km  pitch=%4°  heading=%5°")
                     .arg(cam.value(QStringLiteral("lat")).toDouble(), 0, 'f', 4)
                     .arg(cam.value(QStringLiteral("lon")).toDouble(), 0, 'f', 4)
                     .arg(cam.value(QStringLiteral("altKm")).toDouble(), 0, 'f', 1)
                     .arg(cam.value(QStringLiteral("pitch")).toDouble(), 0, 'f', 1)
                     .arg(cam.value(QStringLiteral("heading")).toDouble(), 0, 'f', 1));
        writeOut(QStringLiteral("map:     %1 (%2)")
                     .arg(obj.value(QStringLiteral("baseMapName")).toString(),
                          obj.value(QStringLiteral("baseMapId")).toString()));
        writeOut(QStringLiteral("display: terrain=%1 grid=%2 dayNight=%3 hud=%4 features=%5/%6")
                     .arg(obj.value(QStringLiteral("terrain")).toBool() ? "on" : "off")
                     .arg(obj.value(QStringLiteral("grid")).toBool() ? "on" : "off")
                     .arg(obj.value(QStringLiteral("dayNight")).toBool() ? "on" : "off")
                     .arg(obj.value(QStringLiteral("hudHidden")).toBool() ? "hidden" : "visible")
                     .arg(obj.value(QStringLiteral("visibleFeatures")).toInt())
                     .arg(obj.value(QStringLiteral("totalFeatures")).toInt()));
        return 0;
    }

    if (obj.contains(QStringLiteral("baseMaps"))) {
        for (const auto &v : obj.value(QStringLiteral("baseMaps")).toArray()) {
            const auto m = v.toObject();
            const QString mark = m.value(QStringLiteral("active")).toBool() ? QStringLiteral("*") : QStringLiteral(" ");
            const int slot = m.value(QStringLiteral("slot")).toInt();
            writeOut(QStringLiteral("%1 [%2] %3  (%4, %5)")
                         .arg(mark)
                         .arg(slot > 0 ? QString::number(slot) : QStringLiteral("-"))
                         .arg(m.value(QStringLiteral("id")).toString(), -16)
                         .arg(m.value(QStringLiteral("name")).toString(),
                              m.value(QStringLiteral("projection")).toString()));
        }
        return 0;
    }

    if (obj.contains(QStringLiteral("feeds"))) {
        for (const auto &v : obj.value(QStringLiteral("feeds")).toArray()) {
            const auto f = v.toObject();
            writeOut(QStringLiteral("%1 %2  (%3 features)  %4")
                         .arg(f.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("[x]") : QStringLiteral("[ ]"))
                         .arg(f.value(QStringLiteral("id")).toString(), -22)
                         .arg(f.value(QStringLiteral("count")).toInt())
                         .arg(f.value(QStringLiteral("name")).toString()));
        }
        return 0;
    }

    if (obj.contains(QStringLiteral("features"))) {
        for (const auto &v : obj.value(QStringLiteral("features")).toArray()) {
            const auto f = v.toObject();
            writeOut(QStringLiteral("%1  (%2, %3)  %4")
                         .arg(f.value(QStringLiteral("id")).toString(), -24)
                         .arg(f.value(QStringLiteral("lat")).toDouble(), 0, 'f', 3)
                         .arg(f.value(QStringLiteral("lon")).toDouble(), 0, 'f', 3)
                         .arg(f.value(QStringLiteral("name")).toString()));
        }
        return 0;
    }

    if (obj.contains(QStringLiteral("paths"))) {
        for (const auto &p : obj.value(QStringLiteral("paths")).toArray())
            writeOut(QStringLiteral("Installed skill: ") + p.toString());
        return 0;
    }

    if (obj.contains(QStringLiteral("path"))) {
        writeOut(QStringLiteral("Saved: %1 (%2x%3)")
                     .arg(obj.value(QStringLiteral("path")).toString())
                     .arg(obj.value(QStringLiteral("width")).toInt())
                     .arg(obj.value(QStringLiteral("height")).toInt()));
        return 0;
    }

    writeOut(QStringLiteral("ok"));
    return 0;
}

QString callDbus(const QString &method, const QVariantList &args = {}) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return jsonError(QStringLiteral("D-Bus session bus is unavailable."));

    QDBusInterface iface(QString::fromLatin1(DbusService::ServiceName),
                         QString::fromLatin1(DbusService::ObjectPath),
                         QString::fromLatin1(DbusService::InterfaceName),
                         bus);
    if (!iface.isValid())
        return jsonError(QStringLiteral("Needleeye is not running on the session D-Bus (launch 'needleeye' first)."));

    QDBusMessage reply = iface.callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return jsonError(QStringLiteral("D-Bus call %1 failed: %2").arg(method, reply.errorMessage()));
    if (reply.arguments().isEmpty())
        return jsonError(QStringLiteral("D-Bus call %1 returned an empty response.").arg(method));
    return reply.arguments().first().toString();
}

} // namespace

bool Cli::isCliInvocation(const QStringList &args) {
    if (args.size() < 2)
        return false;
    return cliCommands().contains(args.at(1));
}

QString Cli::inspectFileJson(const QString &filePath, int limit) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return jsonError(QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString()));

    const QByteArray bytes = file.readAll();
    const QString fileName = QFileInfo(filePath).fileName();
    const bool isCzml = CzmlParser::looksLikeCzml(bytes, fileName);
    const KmlDocument doc = isCzml
        ? CzmlParser::parseBytes(bytes, fileName)
        : KmlParser::parseBytes(bytes, fileName);

    if (!doc.error.isEmpty())
        return jsonError(doc.error);

    QJsonObject out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("format"), isCzml ? QStringLiteral("czml") : QStringLiteral("kml"));
    out.insert(QStringLiteral("name"), doc.name);
    out.insert(QStringLiteral("folderCount"), doc.folders.size());
    out.insert(QStringLiteral("featureCount"), doc.placemarks.size());
    out.insert(QStringLiteral("networkLinkCount"), doc.networkLinks.size());

    QJsonArray folders;
    for (const auto &f : doc.folders) {
        QJsonObject fo;
        fo.insert(QStringLiteral("path"), f.path);
        fo.insert(QStringLiteral("count"), f.placemarkCount);
        folders.append(fo);
    }
    out.insert(QStringLiteral("folders"), folders);

    QJsonArray features;
    const int cap = limit > 0 ? std::min(limit, 500) : 50;
    for (int i = 0; i < doc.placemarks.size() && i < cap; ++i) {
        const auto &pm = doc.placemarks[i];
        const GeoCoord a = pm.anchor();
        QJsonObject po;
        po.insert(QStringLiteral("id"), pm.id);
        po.insert(QStringLiteral("name"), pm.name);
        po.insert(QStringLiteral("folder"), pm.folderPath);
        po.insert(QStringLiteral("lat"), a.lat);
        po.insert(QStringLiteral("lon"), a.lon);
        po.insert(QStringLiteral("alt"), a.alt);
        if (pm.timeStart.isValid())
            po.insert(QStringLiteral("timeStart"), pm.timeStart.toUTC().toString(Qt::ISODate));
        if (pm.timeEnd.isValid())
            po.insert(QStringLiteral("timeEnd"), pm.timeEnd.toUTC().toString(Qt::ISODate));
        po.insert(QStringLiteral("sampleCount"), pm.samples.size());
        po.insert(QStringLiteral("lineCount"), pm.lines.size());
        po.insert(QStringLiteral("polygonCount"), pm.polygons.size());
        features.append(po);
    }
    out.insert(QStringLiteral("features"), features);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString Cli::installSkillJson(const QString &targetDir, const QString &skillTextOverride) {
    QByteArray content = skillTextOverride.toUtf8();
    if (content.isEmpty()) {
        QFile res(QStringLiteral(":/skill/SKILL.md"));
        if (!res.open(QIODevice::ReadOnly))
            return jsonError(QStringLiteral("Bundled SKILL.md resource is missing."));
        content = res.readAll();
    }

    QStringList destinations;
    if (!targetDir.trimmed().isEmpty()) {
        destinations << QDir(targetDir).filePath(QStringLiteral("SKILL.md"));
    } else {
        const QString home = QDir::homePath();
        const QString geminiSkills = home + QStringLiteral("/.gemini/config/skills");
        const QString claudeSkills = home + QStringLiteral("/.claude/skills");
        const QString agentsSkills = home + QStringLiteral("/.agents/skills");
        if (QFileInfo::exists(geminiSkills))
            destinations << (geminiSkills + QStringLiteral("/needleeye/SKILL.md"));
        if (QFileInfo::exists(claudeSkills))
            destinations << (claudeSkills + QStringLiteral("/needleeye/SKILL.md"));
        if (destinations.isEmpty() || QFileInfo::exists(agentsSkills))
            destinations << (agentsSkills + QStringLiteral("/needleeye/SKILL.md"));
        destinations.removeDuplicates();
    }

    QJsonArray installed;
    for (const QString &dest : destinations) {
        QDir().mkpath(QFileInfo(dest).absolutePath());
        const QString tmpPath = dest + QStringLiteral(".tmp");
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate) || tmp.write(content) != content.size()) {
            tmp.close();
            QFile::remove(tmpPath);
            return jsonError(QStringLiteral("Failed to write skill file: %1").arg(dest));
        }
        tmp.close();
        QFile::remove(dest);
        if (!QFile::rename(tmpPath, dest)) {
            QFile::remove(tmpPath);
            return jsonError(QStringLiteral("Failed to finalize skill file: %1").arg(dest));
        }
        installed.append(dest);
    }

    QJsonObject out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("paths"), installed);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

int Cli::run(const QStringList &rawArgs) {
    bool asJson = false;
    QStringList args;
    for (int i = 1; i < rawArgs.size(); ++i) {
        if (rawArgs[i] == QLatin1String("--json"))
            asJson = true;
        else
            args.append(rawArgs[i]);
    }
    if (args.isEmpty()) {
        printHelp();
        return 0;
    }

    const QString cmd = args[0];
    if (cmd == QLatin1String("-h") || cmd == QLatin1String("--help") || cmd == QLatin1String("help")) {
        printHelp();
        return 0;
    }
    if (cmd == QLatin1String("-v") || cmd == QLatin1String("--version")) {
        writeOut(QStringLiteral("needleeye 0.1.0"));
        return 0;
    }

    // Offline commands.
    if (cmd == QLatin1String("inspect")) {
        if (args.size() < 2)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye inspect <file> [--limit <n>] [--json]")), asJson);
        int limit = 50;
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--limit") && i + 1 < args.size())
                limit = args[++i].toInt();
        }
        return emitResponse(inspectFileJson(args[1], limit), asJson);
    }

    if (cmd == QLatin1String("skill")) {
        const QString sub = args.size() >= 2 ? args[1] : QStringLiteral("show");
        if (sub == QLatin1String("show")) {
            QFile res(QStringLiteral(":/skill/SKILL.md"));
            if (!res.open(QIODevice::ReadOnly))
                return emitResponse(jsonError(QStringLiteral("Bundled SKILL.md resource is missing.")), asJson);
            writeOut(QString::fromUtf8(res.readAll()));
            return 0;
        }
        if (sub == QLatin1String("install")) {
            QString dir;
            for (int i = 2; i < args.size(); ++i) {
                if (args[i] == QLatin1String("--dir") && i + 1 < args.size())
                    dir = args[++i];
            }
            return emitResponse(installSkillJson(dir), asJson);
        }
        return emitResponse(jsonError(QStringLiteral("Usage: needleeye skill [show|install [--dir <path>]]")), asJson);
    }

    // Live D-Bus window commands.
    if (cmd == QLatin1String("status"))
        return emitResponse(callDbus(QStringLiteral("GetState")), asJson);

    if (cmd == QLatin1String("fly")) {
        if (args.size() < 3)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye fly <lat> <lon> [--alt <km>] [--pitch <deg>] [--heading <deg>] [--instant]")), asJson);
        bool okLat = false, okLon = false;
        const double lat = args[1].toDouble(&okLat);
        const double lon = args[2].toDouble(&okLon);
        if (!okLat || !okLon)
            return emitResponse(jsonError(QStringLiteral("Latitude and longitude must be numbers.")), asJson);

        double altKm = -1.0;
        double pitch = std::numeric_limits<double>::quiet_NaN();
        double heading = std::numeric_limits<double>::quiet_NaN();
        bool instant = false;
        for (int i = 3; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--alt") && i + 1 < args.size())
                altKm = args[++i].toDouble();
            else if (args[i] == QLatin1String("--pitch") && i + 1 < args.size())
                pitch = args[++i].toDouble();
            else if (args[i] == QLatin1String("--heading") && i + 1 < args.size())
                heading = args[++i].toDouble();
            else if (args[i] == QLatin1String("--instant"))
                instant = true;
        }
        if (!std::isnan(pitch) || !std::isnan(heading) || instant)
            return emitResponse(callDbus(QStringLiteral("SetView"), {lat, lon, altKm, pitch, heading, instant}), asJson);
        return emitResponse(callDbus(QStringLiteral("FlyTo"), {lat, lon, altKm}), asJson);
    }

    if (cmd == QLatin1String("reset")) {
        const QString target = args.size() >= 2 ? args[1].toLower() : QStringLiteral("home");
        if (target == QLatin1String("north"))
            return emitResponse(callDbus(QStringLiteral("ResetNorth")), asJson);
        if (target == QLatin1String("home"))
            return emitResponse(callDbus(QStringLiteral("ResetHome")), asJson);
        return emitResponse(jsonError(QStringLiteral("Usage: needleeye reset [north|home]")), asJson);
    }

    if (cmd == QLatin1String("map")) {
        if (args.size() < 2)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye map [list|set <id|name|slot>]")), asJson);
        if (args[1] == QLatin1String("list"))
            return emitResponse(callDbus(QStringLiteral("ListBaseMaps")), asJson);
        if (args[1] == QLatin1String("set") && args.size() >= 3)
            return emitResponse(callDbus(QStringLiteral("SetBaseMap"), {args[2]}), asJson);
        return emitResponse(jsonError(QStringLiteral("Usage: needleeye map [list|set <id|name|slot>]")), asJson);
    }

    if (cmd == QLatin1String("set")) {
        if (args.size() < 3)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye set [--terrain on|off] [--grid on|off] [--day-night on|off] [--atmosphere on|off] [--hud on|off] [--sidebar on|off]")), asJson);
        QString lastReply;
        for (int i = 1; i + 1 < args.size(); i += 2) {
            const QString flag = args[i];
            bool val = false;
            if (!parseBoolFlag(args[i + 1], val))
                return emitResponse(jsonError(QStringLiteral("Expected on/off for %1").arg(flag)), asJson);
            if (flag == QLatin1String("--terrain"))
                lastReply = callDbus(QStringLiteral("SetTerrain"), {val});
            else if (flag == QLatin1String("--grid"))
                lastReply = callDbus(QStringLiteral("SetGrid"), {val});
            else if (flag == QLatin1String("--day-night"))
                lastReply = callDbus(QStringLiteral("SetDayNight"), {val});
            else if (flag == QLatin1String("--atmosphere"))
                lastReply = callDbus(QStringLiteral("SetAtmosphere"), {val});
            else if (flag == QLatin1String("--hud"))
                lastReply = callDbus(QStringLiteral("SetHudHidden"), {!val});
            else if (flag == QLatin1String("--sidebar"))
                lastReply = callDbus(QStringLiteral("SetSidebarVisible"), {val});
            else
                return emitResponse(jsonError(QStringLiteral("Unknown option: %1").arg(flag)), asJson);

            if (!QJsonDocument::fromJson(lastReply.toUtf8()).object().value(QStringLiteral("ok")).toBool(false))
                return emitResponse(lastReply, asJson);
        }
        return emitResponse(callDbus(QStringLiteral("GetState")), asJson);
    }

    if (cmd == QLatin1String("feed")) {
        if (args.size() < 2)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye feed [list|add <file|url>|enable <id>|disable <id>|remove <id>|refresh]")), asJson);
        const QString sub = args[1];
        if (sub == QLatin1String("list"))
            return emitResponse(callDbus(QStringLiteral("ListFeeds")), asJson);
        if (sub == QLatin1String("refresh"))
            return emitResponse(callDbus(QStringLiteral("RefreshFeeds")), asJson);
        if (sub == QLatin1String("add") && args.size() >= 3) {
            QString name;
            for (int i = 3; i < args.size(); ++i) {
                if (args[i] == QLatin1String("--name") && i + 1 < args.size())
                    name = args[++i];
            }
            QString src = args[2];
            if (!src.startsWith(QStringLiteral("http://")) && !src.startsWith(QStringLiteral("https://")) && QFileInfo::exists(src))
                src = QFileInfo(src).absoluteFilePath();
            return emitResponse(callDbus(QStringLiteral("LoadSource"), {src, name}), asJson);
        }
        if (sub == QLatin1String("enable") && args.size() >= 3)
            return emitResponse(callDbus(QStringLiteral("SetFeedEnabled"), {args[2], true}), asJson);
        if (sub == QLatin1String("disable") && args.size() >= 3)
            return emitResponse(callDbus(QStringLiteral("SetFeedEnabled"), {args[2], false}), asJson);
        if (sub == QLatin1String("remove") && args.size() >= 3)
            return emitResponse(callDbus(QStringLiteral("RemoveFeed"), {args[2]}), asJson);
        return emitResponse(jsonError(QStringLiteral("Invalid feed subcommand.")), asJson);
    }

    if (cmd == QLatin1String("features")) {
        QString query;
        int limit = 50;
        for (int i = 1; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--query") && i + 1 < args.size())
                query = args[++i];
            else if (args[i] == QLatin1String("--limit") && i + 1 < args.size())
                limit = args[++i].toInt();
        }
        return emitResponse(callDbus(QStringLiteral("ListFeatures"), {query, limit}), asJson);
    }

    if (cmd == QLatin1String("select")) {
        if (args.size() < 2)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye select <id|name|none> [--no-fly]")), asJson);
        const bool flyTo = !args.contains(QStringLiteral("--no-fly"));
        return emitResponse(callDbus(QStringLiteral("SelectFeature"), {args[1], flyTo}), asJson);
    }

    if (cmd == QLatin1String("timeline")) {
        bool active = true;
        double progress = -1.0;
        bool playing = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--active") && i + 1 < args.size())
                parseBoolFlag(args[++i], active);
            else if (args[i] == QLatin1String("--progress") && i + 1 < args.size())
                progress = args[++i].toDouble();
            else if (args[i] == QLatin1String("--play"))
                playing = true;
            else if (args[i] == QLatin1String("--pause"))
                playing = false;
        }
        return emitResponse(callDbus(QStringLiteral("SetTimeline"), {active, progress, playing}), asJson);
    }

    if (cmd == QLatin1String("screenshot")) {
        if (args.size() < 2)
            return emitResponse(jsonError(QStringLiteral("Usage: needleeye screenshot <output.png>")), asJson);
        const QString absPath = QFileInfo(args[1]).absoluteFilePath();
        return emitResponse(callDbus(QStringLiteral("CaptureScreenshot"), {absPath}), asJson);
    }

    if (cmd == QLatin1String("quit"))
        return emitResponse(callDbus(QStringLiteral("Quit")), asJson);

    return emitResponse(jsonError(QStringLiteral("Unknown command: %1").arg(cmd)), asJson);
}
