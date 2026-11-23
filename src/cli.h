#pragma once

#include <QStringList>

class Cli {
public:
    // Returns true when arguments invoke a headless CLI subcommand instead of launching the GUI.
    static bool isCliInvocation(const QStringList &args);

    // Executes the CLI subcommand and returns process exit code (0 = ok, 1 = error).
    static int run(const QStringList &args);

    // Helpers exposed for unit testing.
    static QString inspectFileJson(const QString &filePath, int limit = 50);
    static QString installSkillJson(const QString &targetDir = {}, const QString &skillText = {});
};
