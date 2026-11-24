#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QQuickFramebufferObject>
#include <QTimer>
#include <QVector3D>

#include "backend.h"

class GlobeView : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(Backend *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)

    Q_PROPERTY(double cameraLat READ cameraLat NOTIFY cameraChanged)
    Q_PROPERTY(double cameraLon READ cameraLon NOTIFY cameraChanged)
    Q_PROPERTY(double cameraAltitudeKm READ cameraAltitudeKm NOTIFY cameraChanged)
    Q_PROPERTY(double cameraPitch READ cameraPitch NOTIFY cameraChanged)
    Q_PROPERTY(double cameraHeading READ cameraHeading NOTIFY cameraChanged)
    Q_PROPERTY(bool moving READ isMoving NOTIFY cameraChanged)

    Q_PROPERTY(bool cursorOnGlobe READ cursorOnGlobe NOTIFY cursorChanged)
    Q_PROPERTY(double cursorLat READ cursorLat NOTIFY cursorChanged)
    Q_PROPERTY(double cursorLon READ cursorLon NOTIFY cursorChanged)
    Q_PROPERTY(double cursorElevationMeters READ cursorElevationMeters NOTIFY cursorChanged)

    Q_PROPERTY(bool selectedMarkerVisible READ selectedMarkerVisible NOTIFY selectedMarkerChanged)
    Q_PROPERTY(double selectedMarkerX READ selectedMarkerX NOTIFY selectedMarkerChanged)
    Q_PROPERTY(double selectedMarkerY READ selectedMarkerY NOTIFY selectedMarkerChanged)

public:
    explicit GlobeView(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;

    Backend *controller() const { return m_backend; }
    void setController(Backend *backend);

    QString accentColor() const { return m_accentColor.name(); }
    void setAccentColor(const QString &hex);

    QString backgroundColor() const { return m_backgroundColor.name(); }
    void setBackgroundColor(const QString &hex);

    double cameraLat() const { return m_lat; }
    double cameraLon() const { return m_lon; }
    double cameraAltitudeKm() const { return m_altitude * 6371.0; }
    double cameraAltitudeRadii() const { return m_altitude; }
    double cameraPitch() const { return m_pitch; }
    double cameraHeading() const { return m_heading; }
    bool isMoving() const { return m_flying || std::abs(m_velLat) > 0.002 || std::abs(m_velLon) > 0.002; }

    bool cursorOnGlobe() const { return m_cursorOnGlobe; }
    double cursorLat() const { return m_cursorLat; }
    double cursorLon() const { return m_cursorLon; }
    double cursorElevationMeters() const { return m_cursorElevMeters; }

    bool selectedMarkerVisible() const { return m_selectedMarkerVisible; }
    double selectedMarkerX() const { return m_selectedMarkerX; }
    double selectedMarkerY() const { return m_selectedMarkerY; }

    QColor accentQColor() const { return m_accentColor; }
    QColor backgroundQColor() const { return m_backgroundColor; }

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspect) const;
    QVector3D cameraWorldPos() const;

    static QVector3D latLonToVec3(double latDeg, double lonDeg, double radius = 1.0);
    static void vec3ToLatLon(const QVector3D &p, double &latDeg, double &lonDeg);

    Q_INVOKABLE void flyTo(double lat, double lon, double altitudeRadii = 0.45);
    Q_INVOKABLE void setView(double lat, double lon, double altitudeRadii,
                             double pitchDeg, double headingDeg, bool instant = false);
    Q_INVOKABLE void zoomBy(double factor);
    Q_INVOKABLE void panBy(double dLatDeg, double dLonDeg);
    Q_INVOKABLE void tiltBy(double dPitchDeg);
    Q_INVOKABLE void rotateHeadingBy(double dHeadingDeg);
    Q_INVOKABLE void resetNorth();
    Q_INVOKABLE void resetTilt();
    Q_INVOKABLE void resetNorthAndTilt();
    Q_INVOKABLE void toggleTilt3D();
    Q_INVOKABLE void stopMotion();
    Q_INVOKABLE void resetHome();
    void applySpaceNavMotion(const SpaceNavMotion &motion);

signals:
    void controllerChanged();
    void accentColorChanged();
    void backgroundColorChanged();
    void cameraChanged();
    void cursorChanged();
    void selectedMarkerChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void tickAnimation();
    void updateCursorTelemetry(const QPointF &pos);
    void updateSelectedMarkerProjection();
    bool screenToGlobe(const QPointF &pos, double &latDeg, double &lonDeg) const;
    QString pickPlacemarkAt(const QPointF &pos) const;
    void requestNeededTilesForCamera();

    Backend *m_backend = nullptr;
    QColor m_accentColor = QColor("#FFD60A");
    QColor m_backgroundColor = QColor("#0e0e10");

    double m_lat = 22.0;
    double m_lon = 10.0;
    double m_altitude = 1.95; // Earth radii above surface
    double m_pitch = 0.0;     // degrees [0, 62]
    double m_heading = 0.0;   // degrees

    // Smooth inertia & fly-to target state
    double m_velLat = 0.0;
    double m_velLon = 0.0;
    bool m_flying = false;
    double m_targetLat = 22.0;
    double m_targetLon = 10.0;
    double m_targetAlt = 1.95;
    double m_targetPitch = 0.0;
    double m_targetHeading = 0.0;

    bool m_dragging = false;
    bool m_tilting = false;
    bool m_rightZooming = false;
    bool m_dragMoved = false;
    QPointF m_pressPos;
    QPointF m_lastPos;

    bool m_cursorOnGlobe = false;
    double m_cursorLat = 0.0;
    double m_cursorLon = 0.0;
    double m_cursorElevMeters = 0.0;

    bool m_selectedMarkerVisible = false;
    double m_selectedMarkerX = 0.0;
    double m_selectedMarkerY = 0.0;

    QTimer m_animTimer;
};
