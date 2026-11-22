#pragma once

#include <QObject>
#include <QString>
#include <functional>

class FilePicker : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &filePath, const QString &error)>;
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual void openKmlFile(const QString &title, Callback callback) = 0;
};

// Opens files via org.freedesktop.portal.FileChooser over D-Bus so the dialog
// integrates natively with Hyprland/Omarchy without linking Qt Widgets.
class PortalFilePicker : public FilePicker {
    Q_OBJECT
public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    void openKmlFile(const QString &title, Callback callback) override;

private slots:
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    Callback m_callback;
    QString m_activeRequestPath;
};
