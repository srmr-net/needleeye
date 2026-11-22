#include "globeview.h"

#include <QHash>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLShaderProgram>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;
constexpr double kMaxMercLat = 85.05112878;

double wrapLon(double lon) {
    while (lon > 180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    return lon;
}

double shortestLonDelta(double fromLon, double toLon) {
    return wrapLon(toLon - fromLon);
}

double tileToMercY(int z, int y) {
    const double n = static_cast<double>(1 << z);
    return M_PI * (1.0 - 2.0 * static_cast<double>(y) / n);
}

double tileToLonRad(int z, int x) {
    const double n = static_cast<double>(1 << z);
    return (-180.0 + 360.0 * static_cast<double>(x) / n) * kDegToRad;
}

struct KmlVertex {
    float x, y, z;
    float r, g, b, a;
};

struct MarkerVertex {
    float x, y, z;
    float r, g, b, a;
    float size;
    float selected;
};

struct RenderSnapshot {
    int baseLayer = 0;
    bool terrainEnabled = true;
    bool graticuleEnabled = true;
    bool atmosphereEnabled = true;
    QColor accent = QColor("#FFD60A");
    QColor background = QColor("#0e0e10");
    QMatrix4x4 view;
    QMatrix4x4 proj;
    QVector3D camPos;
    double camAltitude = 1.95;
    double camLat = 22.0;
    double camLon = 10.0;
    float devicePixelRatio = 1.0f;

    quint64 kmlRevision = 0;
    QString selectedId;
    QVector<KmlPlacemark> placemarks;
    TileManager *tileManager = nullptr;
};

struct VisibleTile {
    int z = 0;
    int x = 0;
    int y = 0;
};

// Recursively selects Web Mercator tiles around the camera view frustum.
void collectVisibleTiles(int z, int x, int y, int maxZoom, const QVector3D &camPos,
                         const QMatrix4x4 &vp, QVector<VisibleTile> &out) {
    const double lon0 = tileToLonRad(z, x);
    const double lon1 = tileToLonRad(z, x + 1);
    const double my0 = tileToMercY(z, y);     // north
    const double my1 = tileToMercY(z, y + 1); // south

    const double latN = std::atan(std::sinh(my0));
    const double latS = std::atan(std::sinh(my1));
    const double latC = 0.5 * (latN + latS);
    const double lonC = 0.5 * (lon0 + lon1);

    const QVector3D center(
        static_cast<float>(std::cos(latC) * std::sin(lonC)),
        static_cast<float>(std::sin(latC)),
        static_cast<float>(std::cos(latC) * std::cos(lonC)));

    const double spanRad = std::max(lon1 - lon0, latN - latS);
    const float boundRadius = static_cast<float>(std::min(1.5, 0.78 * spanRad));

    // Horizon / backface culling: sphere point P is visible from camPos if
    // dot(P, camPos) >= 1.0 (tangent plane). Allow bounding radius margin.
    const float camLen = camPos.length();
    if (z >= 2) {
        const float dotHorizon = QVector3D::dotProduct(center, camPos);
        if (dotHorizon < 1.0f - boundRadius * camLen - 0.12f)
            return;

        // Rough NDC frustum cull with generous margin so tiles never pop at edges.
        const QVector4D clip = vp * QVector4D(center, 1.0f);
        if (clip.w() > 0.0f) {
            const float ndcX = clip.x() / clip.w();
            const float ndcY = clip.y() / clip.w();
            const float margin = 1.25f + boundRadius * 2.4f / std::max(0.05f, (center - camPos).length());
            if (std::abs(ndcX) > margin || std::abs(ndcY) > margin)
                return;
        }
    }

    const float dist = std::max(0.0008f, (center - camPos).length() - boundRadius * 0.5f);
    const float angularMetric = static_cast<float>(spanRad) / dist;

    if (z < maxZoom && (z < 2 || angularMetric > 0.34f) && out.size() < 180) {
        collectVisibleTiles(z + 1, x * 2, y * 2, maxZoom, camPos, vp, out);
        collectVisibleTiles(z + 1, x * 2 + 1, y * 2, maxZoom, camPos, vp, out);
        collectVisibleTiles(z + 1, x * 2, y * 2 + 1, maxZoom, camPos, vp, out);
        collectVisibleTiles(z + 1, x * 2 + 1, y * 2 + 1, maxZoom, camPos, vp, out);
    } else {
        out.append({z, x, y});
    }
}

class GlobeRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLExtraFunctions {
public:
    GlobeRenderer() = default;
    ~GlobeRenderer() override { cleanup(); }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fmt.setSamples(4);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        auto *view = static_cast<GlobeView *>(item);
        m_snap.accent = view->accentQColor();
        m_snap.background = view->backgroundQColor();
        m_snap.view = view->viewMatrix();
        const float aspect = view->height() > 0.0 ? static_cast<float>(view->width() / view->height()) : 1.0f;
        m_snap.proj = view->projectionMatrix(aspect);
        m_snap.camPos = view->cameraWorldPos();
        m_snap.camAltitude = view->cameraAltitudeRadii();
        m_snap.camLat = view->cameraLat();
        m_snap.camLon = view->cameraLon();
        m_snap.devicePixelRatio = view->window() ? static_cast<float>(view->window()->effectiveDevicePixelRatio()) : 1.0f;

        if (auto *b = view->controller()) {
            m_snap.tileManager = b->tileManager();
            m_snap.baseLayer = b->baseLayer();
            m_snap.terrainEnabled = b->terrainEnabled();
            m_snap.graticuleEnabled = b->graticuleEnabled();
            m_snap.atmosphereEnabled = b->atmosphereEnabled();

            if (b->kmlRevision() != m_uploadedKmlRevision || b->selectedPlacemarkId() != m_uploadedSelectedId) {
                m_snap.kmlRevision = b->kmlRevision();
                m_snap.selectedId = b->selectedPlacemarkId();
                m_snap.placemarks = b->visiblePlacemarkList();
                m_kmlDirty = true;
            }
        }
    }

    void render() override {
        if (!m_initialized)
            initGl();

        if (m_kmlDirty) {
            rebuildKmlBuffers();
            m_kmlDirty = false;
        }

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glClearColor(m_snap.background.redF() * 0.55f,
                     m_snap.background.greenF() * 0.55f,
                     m_snap.background.blueF() * 0.65f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const QMatrix4x4 vp = m_snap.proj * m_snap.view;

        // 1. Draw adaptive slippy-map globe tiles (with optional 3D Terrarium DEM).
        renderTiles(vp);

        // 2. Draw polar caps so latitudes > 85.05 deg never leave holes.
        renderPolarCaps(vp);

        // 3. Optional subtle lat/lon graticule.
        if (m_snap.graticuleEnabled)
            renderGraticule(vp);

        // 4. KML overlays (polygons, lines, placemarks).
        renderKmlOverlays(vp);

        // 5. Atmospheric Fresnel halo around limb.
        if (m_snap.atmosphereEnabled)
            renderAtmosphere(vp);

        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    struct GpuTile {
        GLuint tex = 0;
        quint64 lastUsedFrame = 0;
    };

    void initGl() {
        initializeOpenGLFunctions();
        buildShaders();
        buildTilePatchMesh();
        buildPolarCapsMesh();
        buildGraticuleMesh();
        buildAtmosphereMesh();

        glGenVertexArrays(1, &m_kmlPolyVao);
        glGenBuffers(1, &m_kmlPolyVbo);
        glGenVertexArrays(1, &m_kmlLineVao);
        glGenBuffers(1, &m_kmlLineVbo);
        glGenVertexArrays(1, &m_kmlMarkerVao);
        glGenBuffers(1, &m_kmlMarkerVbo);

        // 1x1 dark ocean fallback texture when tiles haven't loaded yet.
        glGenTextures(1, &m_fallbackTex);
        glBindTexture(GL_TEXTURE_2D, m_fallbackTex);
        const unsigned char darkPx[4] = {18, 22, 30, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, darkPx);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // 1x1 sea-level Terrarium DEM fallback (R=128, G=0, B=0 => 0m).
        glGenTextures(1, &m_flatDemTex);
        glBindTexture(GL_TEXTURE_2D, m_flatDemTex);
        const unsigned char flatPx[4] = {128, 0, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatPx);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_initialized = true;
    }

    void cleanup() {
        if (!m_initialized)
            return;
        for (auto it = m_tileTextures.begin(); it != m_tileTextures.end(); ++it)
            glDeleteTextures(1, &it.value().tex);
        m_tileTextures.clear();
        if (m_fallbackTex) glDeleteTextures(1, &m_fallbackTex);
        if (m_flatDemTex) glDeleteTextures(1, &m_flatDemTex);
        if (m_patchVao) glDeleteVertexArrays(1, &m_patchVao);
        if (m_patchVbo) glDeleteBuffers(1, &m_patchVbo);
        if (m_patchIbo) glDeleteBuffers(1, &m_patchIbo);
        if (m_capsVao) glDeleteVertexArrays(1, &m_capsVao);
        if (m_capsVbo) glDeleteBuffers(1, &m_capsVbo);
        if (m_gratVao) glDeleteVertexArrays(1, &m_gratVao);
        if (m_gratVbo) glDeleteBuffers(1, &m_gratVbo);
        if (m_atmoVao) glDeleteVertexArrays(1, &m_atmoVao);
        if (m_atmoVbo) glDeleteBuffers(1, &m_atmoVbo);
        if (m_atmoIbo) glDeleteBuffers(1, &m_atmoIbo);
        if (m_kmlPolyVao) glDeleteVertexArrays(1, &m_kmlPolyVao);
        if (m_kmlPolyVbo) glDeleteBuffers(1, &m_kmlPolyVbo);
        if (m_kmlLineVao) glDeleteVertexArrays(1, &m_kmlLineVao);
        if (m_kmlLineVbo) glDeleteBuffers(1, &m_kmlLineVbo);
        if (m_kmlMarkerVao) glDeleteVertexArrays(1, &m_kmlMarkerVao);
        if (m_kmlMarkerVbo) glDeleteBuffers(1, &m_kmlMarkerVbo);
    }

    void buildShaders() {
        // Tile shader: projects Mercator patch onto sphere and displaces by AWS Terrarium DEM.
        m_tileProg.addShaderFromSourceCode(QOpenGLShader::Vertex, R"glsl(
            #version 330 core
            layout(location = 0) in vec2 aUv;
            uniform mat4 uVp;
            uniform vec4 uBounds;       // lon0, lon1, mercY0(north), mercY1(south)
            uniform vec4 uColorUvRect;  // u0, v0, uScale, vScale
            uniform vec4 uDemUvRect;    // u0, v0, uScale, vScale
            uniform sampler2D uDemTex;
            uniform float uTerrainScale;

            out vec2 vColorUv;
            out vec3 vNormal;
            out float vElevNorm;

            void main() {
                float lon = mix(uBounds.x, uBounds.y, aUv.x);
                float mercY = mix(uBounds.z, uBounds.w, aUv.y);
                float lat = atan(sinh(mercY));

                vec3 n = vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon));
                vColorUv = uColorUvRect.xy + aUv * uColorUvRect.zw;

                float disp = 0.0;
                if (uTerrainScale > 0.0) {
                    vec2 demUv = uDemUvRect.xy + aUv * uDemUvRect.zw;
                    vec3 rgb = texture(uDemTex, demUv).rgb * 255.0;
                    float meters = (rgb.r * 256.0 + rgb.g + rgb.b / 256.0) - 32768.0;
                    meters = max(0.0, meters);
                    disp = (meters / 6371000.0) * uTerrainScale;
                    vElevNorm = clamp(meters / 6000.0, 0.0, 1.0);
                } else {
                    vElevNorm = 0.0;
                }

                vNormal = n;
                gl_Position = uVp * vec4(n * (1.0 + disp), 1.0);
            }
        )glsl");
        m_tileProg.addShaderFromSourceCode(QOpenGLShader::Fragment, R"glsl(
            #version 330 core
            in vec2 vColorUv;
            in vec3 vNormal;
            in float vElevNorm;
            uniform sampler2D uColorTex;
            uniform vec3 uCamDir;
            uniform vec3 uAccent;
            uniform float uTerrainTint;
            out vec4 fragColor;

            void main() {
                vec3 col = texture(uColorTex, vColorUv).rgb;
                float ndv = clamp(dot(normalize(vNormal), normalize(uCamDir)), 0.0, 1.0);
                float shade = 0.72 + 0.28 * pow(ndv, 0.45);
                col *= shade;
                // Subtle alpine relief highlight when 3D terrain is active.
                col += vec3(0.06, 0.07, 0.08) * vElevNorm * uTerrainTint;
                // Delicate atmospheric limb tint near horizon.
                float rim = pow(1.0 - ndv, 3.2);
                col = mix(col, mix(vec3(0.18, 0.42, 0.68), uAccent, 0.35), rim * 0.28);
                fragColor = vec4(col, 1.0);
            }
        )glsl");
        m_tileProg.link();

        // Flat / colored geometry shader for polar caps, graticule, KML lines & polygons.
        m_colorProg.addShaderFromSourceCode(QOpenGLShader::Vertex, R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec4 aCol;
            uniform mat4 uVp;
            uniform vec4 uOverrideColor;
            uniform float uUseVertexColor;
            out vec4 vCol;
            void main() {
                vCol = mix(uOverrideColor, aCol, uUseVertexColor);
                gl_Position = uVp * vec4(aPos, 1.0);
            }
        )glsl");
        m_colorProg.addShaderFromSourceCode(QOpenGLShader::Fragment, R"glsl(
            #version 330 core
            in vec4 vCol;
            out vec4 fragColor;
            void main() {
                fragColor = vCol;
            }
        )glsl");
        m_colorProg.link();

        // Anti-aliased GPU point-sprite shader for KML Placemark pins.
        m_markerProg.addShaderFromSourceCode(QOpenGLShader::Vertex, R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec4 aCol;
            layout(location = 2) in float aSize;
            layout(location = 3) in float aSelected;
            uniform mat4 uVp;
            uniform float uDpr;
            out vec4 vCol;
            out float vSelected;
            void main() {
                vCol = aCol;
                vSelected = aSelected;
                gl_Position = uVp * vec4(aPos, 1.0);
                gl_PointSize = (aSize + aSelected * 10.0) * uDpr;
            }
        )glsl");
        m_markerProg.addShaderFromSourceCode(QOpenGLShader::Fragment, R"glsl(
            #version 330 core
            in vec4 vCol;
            in float vSelected;
            uniform vec3 uAccent;
            out vec4 fragColor;
            void main() {
                vec2 uv = gl_PointCoord * 2.0 - 1.0;
                float r = length(uv);
                if (r > 1.0) discard;

                if (vSelected > 0.5) {
                    // Crisp target reticle ring + bright core for selected feature.
                    float outerRing = smoothstep(0.98, 0.86, r) - smoothstep(0.76, 0.66, r);
                    float core = smoothstep(0.46, 0.34, r);
                    vec3 c = mix(uAccent, vec3(1.0), core * 0.45);
                    float a = clamp(outerRing + core, 0.0, 1.0);
                    if (a < 0.02) discard;
                    fragColor = vec4(c, a);
                } else {
                    float glow = exp(-r * r * 3.8) * 0.42;
                    float core = smoothstep(0.58, 0.44, r);
                    float border = smoothstep(0.68, 0.54, r) - core;
                    vec3 c = mix(vCol.rgb, vec3(0.05, 0.05, 0.07), border * 0.85);
                    float a = clamp(glow + core + border * 0.9, 0.0, 1.0) * vCol.a;
                    if (a < 0.02) discard;
                    fragColor = vec4(c, a);
                }
            }
        )glsl");
        m_markerProg.link();

        // Atmospheric Fresnel outer halo shader.
        m_atmoProg.addShaderFromSourceCode(QOpenGLShader::Vertex, R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            uniform mat4 uVp;
            out vec3 vWorldPos;
            void main() {
                vWorldPos = aPos;
                gl_Position = uVp * vec4(aPos, 1.0);
            }
        )glsl");
        m_atmoProg.addShaderFromSourceCode(QOpenGLShader::Fragment, R"glsl(
            #version 330 core
            in vec3 vWorldPos;
            uniform vec3 uCamPos;
            uniform vec3 uAccent;
            out vec4 fragColor;
            void main() {
                vec3 n = normalize(vWorldPos);
                vec3 v = normalize(uCamPos - vWorldPos);
                float rim = pow(clamp(1.0 - abs(dot(n, v)), 0.0, 1.0), 3.6);
                vec3 tint = mix(vec3(0.16, 0.45, 0.78), uAccent, 0.38);
                fragColor = vec4(tint, rim * 0.52);
            }
        )glsl");
        m_atmoProg.link();
    }

    void buildTilePatchMesh() {
        constexpr int N = 16;
        std::vector<float> verts;
        verts.reserve((N + 1) * (N + 1) * 2);
        for (int j = 0; j <= N; ++j) {
            for (int i = 0; i <= N; ++i) {
                verts.push_back(static_cast<float>(i) / N);
                verts.push_back(static_cast<float>(j) / N);
            }
        }
        std::vector<unsigned int> idx;
        idx.reserve(N * N * 6);
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                const unsigned int row0 = j * (N + 1) + i;
                const unsigned int row1 = (j + 1) * (N + 1) + i;
                idx.push_back(row0);
                idx.push_back(row1);
                idx.push_back(row0 + 1);
                idx.push_back(row0 + 1);
                idx.push_back(row1);
                idx.push_back(row1 + 1);
            }
        }
        m_patchIndexCount = static_cast<int>(idx.size());

        glGenVertexArrays(1, &m_patchVao);
        glGenBuffers(1, &m_patchVbo);
        glGenBuffers(1, &m_patchIbo);
        glBindVertexArray(m_patchVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_patchVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_patchIbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(unsigned int)), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
    }

    void buildPolarCapsMesh() {
        std::vector<KmlVertex> verts;
        constexpr int segs = 64;
        auto addCap = [&](double sign) {
            const QVector3D pole = GlobeView::latLonToVec3(sign * 90.0, 0.0, 0.9998);
            const double edgeLat = sign * (kMaxMercLat - 0.15);
            for (int i = 0; i < segs; ++i) {
                const double lon0 = -180.0 + 360.0 * i / segs;
                const double lon1 = -180.0 + 360.0 * (i + 1) / segs;
                const QVector3D p0 = GlobeView::latLonToVec3(edgeLat, lon0, 0.9998);
                const QVector3D p1 = GlobeView::latLonToVec3(edgeLat, lon1, 0.9998);
                const float r = 0.08f, g = 0.10f, b = 0.13f;
                if (sign > 0) {
                    verts.push_back({pole.x(), pole.y(), pole.z(), r, g, b, 1.0f});
                    verts.push_back({p0.x(), p0.y(), p0.z(), r, g, b, 1.0f});
                    verts.push_back({p1.x(), p1.y(), p1.z(), r, g, b, 1.0f});
                } else {
                    verts.push_back({pole.x(), pole.y(), pole.z(), r, g, b, 1.0f});
                    verts.push_back({p1.x(), p1.y(), p1.z(), r, g, b, 1.0f});
                    verts.push_back({p0.x(), p0.y(), p0.z(), r, g, b, 1.0f});
                }
            }
        };
        addCap(1.0);
        addCap(-1.0);
        m_capsVertexCount = static_cast<int>(verts.size());

        glGenVertexArrays(1, &m_capsVao);
        glGenBuffers(1, &m_capsVbo);
        glBindVertexArray(m_capsVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_capsVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(KmlVertex)), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), reinterpret_cast<void *>(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    void buildGraticuleMesh() {
        std::vector<KmlVertex> verts;
        const float radius = 1.0014f;
        // Latitude parallels every 30 degrees (equator slightly brighter).
        for (int lat = -60; lat <= 60; lat += 30) {
            const float a = (lat == 0) ? 0.20f : 0.09f;
            for (int lon = -180; lon < 180; lon += 4) {
                const QVector3D p0 = GlobeView::latLonToVec3(lat, lon, radius);
                const QVector3D p1 = GlobeView::latLonToVec3(lat, lon + 4, radius);
                verts.push_back({p0.x(), p0.y(), p0.z(), 0.7f, 0.8f, 0.9f, a});
                verts.push_back({p1.x(), p1.y(), p1.z(), 0.7f, 0.8f, 0.9f, a});
            }
        }
        // Longitude meridians every 30 degrees.
        for (int lon = -180; lon < 180; lon += 30) {
            const float a = (lon == 0) ? 0.18f : 0.09f;
            for (int lat = -84; lat < 84; lat += 4) {
                const QVector3D p0 = GlobeView::latLonToVec3(lat, lon, radius);
                const QVector3D p1 = GlobeView::latLonToVec3(lat + 4, lon, radius);
                verts.push_back({p0.x(), p0.y(), p0.z(), 0.7f, 0.8f, 0.9f, a});
                verts.push_back({p1.x(), p1.y(), p1.z(), 0.7f, 0.8f, 0.9f, a});
            }
        }
        m_gratVertexCount = static_cast<int>(verts.size());

        glGenVertexArrays(1, &m_gratVao);
        glGenBuffers(1, &m_gratVbo);
        glBindVertexArray(m_gratVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_gratVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(KmlVertex)), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), reinterpret_cast<void *>(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    void buildAtmosphereMesh() {
        constexpr int stacks = 32;
        constexpr int slices = 64;
        constexpr float R = 1.045f;
        std::vector<float> verts;
        std::vector<unsigned int> idx;
        for (int i = 0; i <= stacks; ++i) {
            const double phi = M_PI * static_cast<double>(i) / stacks - M_PI * 0.5;
            for (int j = 0; j <= slices; ++j) {
                const double theta = 2.0 * M_PI * static_cast<double>(j) / slices;
                verts.push_back(static_cast<float>(R * std::cos(phi) * std::sin(theta)));
                verts.push_back(static_cast<float>(R * std::sin(phi)));
                verts.push_back(static_cast<float>(R * std::cos(phi) * std::cos(theta)));
            }
        }
        for (int i = 0; i < stacks; ++i) {
            for (int j = 0; j < slices; ++j) {
                const unsigned int a = i * (slices + 1) + j;
                const unsigned int b = (i + 1) * (slices + 1) + j;
                idx.push_back(a); idx.push_back(a + 1); idx.push_back(b);
                idx.push_back(a + 1); idx.push_back(b + 1); idx.push_back(b);
            }
        }
        m_atmoIndexCount = static_cast<int>(idx.size());

        glGenVertexArrays(1, &m_atmoVao);
        glGenBuffers(1, &m_atmoVbo);
        glGenBuffers(1, &m_atmoIbo);
        glBindVertexArray(m_atmoVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_atmoVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_atmoIbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(unsigned int)), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
    }

    GLuint ensureTileTexture(int layer, int z, int x, int y, QVector4D &uvRect) {
        const TileKey exact{layer, z, x, y};
        auto it = m_tileTextures.find(exact);
        if (it != m_tileTextures.end()) {
            it.value().lastUsedFrame = m_frameCounter;
            uvRect = QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
            return it.value().tex;
        }

        // Check if TileManager already has this tile in memory cache.
        if (m_snap.tileManager) {
            const QImage img = m_snap.tileManager->cachedTile(layer, z, x, y);
            if (!img.isNull()) {
                GLuint tex = uploadTexture(img);
                m_tileTextures.insert(exact, {tex, m_frameCounter});
                evictOldTextures();
                uvRect = QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
                return tex;
            }
        }

        // Walk up ancestor tiles so we never flash blank space while loading.
        int az = z - 1;
        int ax = x >> 1;
        int ay = y >> 1;
        int scale = 2;
        while (az >= 0) {
            const TileKey anc{layer, az, ax, ay};
            auto ait = m_tileTextures.find(anc);
            GLuint tex = 0;
            if (ait != m_tileTextures.end()) {
                ait.value().lastUsedFrame = m_frameCounter;
                tex = ait.value().tex;
            } else if (m_snap.tileManager) {
                const QImage img = m_snap.tileManager->cachedTile(layer, az, ax, ay);
                if (!img.isNull()) {
                    tex = uploadTexture(img);
                    m_tileTextures.insert(anc, {tex, m_frameCounter});
                }
            }
            if (tex != 0) {
                const float inv = 1.0f / static_cast<float>(scale);
                const float u0 = static_cast<float>(x - (ax * scale)) * inv;
                const float v0 = static_cast<float>(y - (ay * scale)) * inv;
                uvRect = QVector4D(u0, v0, inv, inv);
                return tex;
            }
            --az;
            ax >>= 1;
            ay >>= 1;
            scale <<= 1;
        }

        uvRect = QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
        return (layer == static_cast<int>(BaseLayer::ElevationDem)) ? m_flatDemTex : m_fallbackTex;
    }

    GLuint uploadTexture(const QImage &img) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    void evictOldTextures() {
        if (m_tileTextures.size() <= 320)
            return;
        QList<TileKey> toRemove;
        for (auto it = m_tileTextures.cbegin(); it != m_tileTextures.cend(); ++it) {
            if (m_frameCounter - it.value().lastUsedFrame > 180)
                toRemove.append(it.key());
        }
        for (const auto &k : toRemove) {
            glDeleteTextures(1, &m_tileTextures[k].tex);
            m_tileTextures.remove(k);
        }
    }

    void renderTiles(const QMatrix4x4 &vp) {
        ++m_frameCounter;
        const int maxZoom = std::min(TileManager::maxZoomForLayer(m_snap.baseLayer),
                                     std::clamp(static_cast<int>(std::ceil(std::log2(3.5 / std::max(0.0005, m_snap.camAltitude)))) + 1, 2, 16));

        QVector<VisibleTile> tiles;
        tiles.reserve(96);
        collectVisibleTiles(1, 0, 0, maxZoom, m_snap.camPos, vp, tiles);
        collectVisibleTiles(1, 1, 0, maxZoom, m_snap.camPos, vp, tiles);
        collectVisibleTiles(1, 0, 1, maxZoom, m_snap.camPos, vp, tiles);
        collectVisibleTiles(1, 1, 1, maxZoom, m_snap.camPos, vp, tiles);

        m_tileProg.bind();
        m_tileProg.setUniformValue("uVp", vp);
        m_tileProg.setUniformValue("uCamDir", m_snap.camPos.normalized());
        m_tileProg.setUniformValue("uAccent", QVector3D(m_snap.accent.redF(), m_snap.accent.greenF(), m_snap.accent.blueF()));
        // Exaggerate elevation gently from orbit and dial down close to ground.
        const float terrainScale = m_snap.terrainEnabled
            ? static_cast<float>(std::clamp(1.6 + m_snap.camAltitude * 2.2, 1.6, 5.5))
            : 0.0f;
        m_tileProg.setUniformValue("uTerrainScale", terrainScale);
        m_tileProg.setUniformValue("uTerrainTint", m_snap.terrainEnabled ? 1.0f : 0.0f);
        m_tileProg.setUniformValue("uColorTex", 0);
        m_tileProg.setUniformValue("uDemTex", 1);

        glBindVertexArray(m_patchVao);
        for (const auto &t : tiles) {
            QVector4D colorUv(0, 0, 1, 1);
            const GLuint colorTex = ensureTileTexture(m_snap.baseLayer, t.z, t.x, t.y, colorUv);

            QVector4D demUv(0, 0, 1, 1);
            GLuint demTex = m_flatDemTex;
            if (m_snap.terrainEnabled) {
                const int demZ = std::min(t.z, 10);
                const int shift = t.z - demZ;
                demTex = ensureTileTexture(static_cast<int>(BaseLayer::ElevationDem), demZ, t.x >> shift, t.y >> shift, demUv);
                if (shift > 0) {
                    const float subScale = 1.0f / static_cast<float>(1 << shift);
                    const float subU = static_cast<float>(t.x - ((t.x >> shift) << shift)) * subScale;
                    const float subV = static_cast<float>(t.y - ((t.y >> shift) << shift)) * subScale;
                    demUv = QVector4D(demUv.x() + subU * demUv.z(),
                                      demUv.y() + subV * demUv.w(),
                                      demUv.z() * subScale,
                                      demUv.w() * subScale);
                }
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, colorTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, demTex);

            const QVector4D bounds(
                static_cast<float>(tileToLonRad(t.z, t.x)),
                static_cast<float>(tileToLonRad(t.z, t.x + 1)),
                static_cast<float>(tileToMercY(t.z, t.y)),
                static_cast<float>(tileToMercY(t.z, t.y + 1)));

            m_tileProg.setUniformValue("uBounds", bounds);
            m_tileProg.setUniformValue("uColorUvRect", colorUv);
            m_tileProg.setUniformValue("uDemUvRect", demUv);
            glDrawElements(GL_TRIANGLES, m_patchIndexCount, GL_UNSIGNED_INT, nullptr);
        }
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        m_tileProg.release();
    }

    void renderPolarCaps(const QMatrix4x4 &vp) {
        m_colorProg.bind();
        m_colorProg.setUniformValue("uVp", vp);
        m_colorProg.setUniformValue("uUseVertexColor", 1.0f);
        glBindVertexArray(m_capsVao);
        glDrawArrays(GL_TRIANGLES, 0, m_capsVertexCount);
        glBindVertexArray(0);
        m_colorProg.release();
    }

    void renderGraticule(const QMatrix4x4 &vp) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        m_colorProg.bind();
        m_colorProg.setUniformValue("uVp", vp);
        m_colorProg.setUniformValue("uUseVertexColor", 1.0f);
        glBindVertexArray(m_gratVao);
        glDrawArrays(GL_LINES, 0, m_gratVertexCount);
        glBindVertexArray(0);
        m_colorProg.release();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void rebuildKmlBuffers() {
        m_uploadedKmlRevision = m_snap.kmlRevision;
        m_uploadedSelectedId = m_snap.selectedId;

        std::vector<KmlVertex> polyVerts;
        std::vector<KmlVertex> lineVerts;
        std::vector<MarkerVertex> markerVerts;

        const double polyRadius = 1.0022;
        const double lineRadius = 1.0030;
        const double pointRadius = 1.0045;

        auto appendSubdividedSegment = [&](const GeoCoord &a, const GeoCoord &b, const QColor &col, double rad) {
            const double dLon = shortestLonDelta(a.lon, b.lon);
            const double dLat = b.lat - a.lat;
            const double span = std::hypot(dLon, dLat);
            const int steps = std::clamp(static_cast<int>(std::ceil(span / 2.5)), 1, 24);
            const float r = col.redF(), g = col.greenF(), bl = col.blueF(), al = std::max(0.45f, static_cast<float>(col.alphaF()));
            for (int s = 0; s < steps; ++s) {
                const double t0 = static_cast<double>(s) / steps;
                const double t1 = static_cast<double>(s + 1) / steps;
                const QVector3D p0 = GlobeView::latLonToVec3(a.lat + dLat * t0, wrapLon(a.lon + dLon * t0), rad);
                const QVector3D p1 = GlobeView::latLonToVec3(a.lat + dLat * t1, wrapLon(a.lon + dLon * t1), rad);
                lineVerts.push_back({p0.x(), p0.y(), p0.z(), r, g, bl, al});
                lineVerts.push_back({p1.x(), p1.y(), p1.z(), r, g, bl, al});
            }
        };

        auto emitSphericalTri = [&](const QVector3D &a, const QVector3D &b, const QVector3D &c,
                                    const QColor &col, int depth, auto &&self) -> void {
            const float maxEdge = std::max({(a - b).length(), (b - c).length(), (c - a).length()});
            if (depth > 0 && maxEdge > 0.085f) {
                const QVector3D ab = (a + b).normalized() * static_cast<float>(polyRadius);
                const QVector3D bc = (b + c).normalized() * static_cast<float>(polyRadius);
                const QVector3D ca = (c + a).normalized() * static_cast<float>(polyRadius);
                self(a, ab, ca, col, depth - 1, self);
                self(ab, b, bc, col, depth - 1, self);
                self(ca, bc, c, col, depth - 1, self);
                self(ab, bc, ca, col, depth - 1, self);
                return;
            }
            const float r = col.redF(), g = col.greenF(), bl = col.blueF();
            const float al = std::clamp(static_cast<float>(col.alphaF()), 0.16f, 0.55f);
            polyVerts.push_back({a.x(), a.y(), a.z(), r, g, bl, al});
            polyVerts.push_back({b.x(), b.y(), b.z(), r, g, bl, al});
            polyVerts.push_back({c.x(), c.y(), c.z(), r, g, bl, al});
        };

        for (const auto &pm : m_snap.placemarks) {
            const bool isSelected = (!m_snap.selectedId.isEmpty() && pm.id == m_snap.selectedId);

            // Polygons
            for (const auto &poly : pm.polygons) {
                if (poly.outer.size() < 3)
                    continue;
                if (pm.style.polyFill) {
                    const QVector<int> tris = KmlParser::triangulateRing(poly.outer);
                    const QColor fillCol = isSelected ? m_snap.accent : pm.style.polyColor;
                    for (int i = 0; i + 2 < tris.size(); i += 3) {
                        const QVector3D a = GlobeView::latLonToVec3(poly.outer[tris[i]].lat, poly.outer[tris[i]].lon, polyRadius);
                        const QVector3D b = GlobeView::latLonToVec3(poly.outer[tris[i + 1]].lat, poly.outer[tris[i + 1]].lon, polyRadius);
                        const QVector3D c = GlobeView::latLonToVec3(poly.outer[tris[i + 2]].lat, poly.outer[tris[i + 2]].lon, polyRadius);
                        emitSphericalTri(a, b, c, fillCol, 3, emitSphericalTri);
                    }
                }
                if (pm.style.polyOutline) {
                    const QColor stroke = isSelected ? m_snap.accent : pm.style.lineColor;
                    for (int i = 0; i < poly.outer.size(); ++i) {
                        appendSubdividedSegment(poly.outer[i], poly.outer[(i + 1) % poly.outer.size()], stroke, lineRadius);
                    }
                }
            }

            // Lines
            for (const auto &line : pm.lines) {
                const QColor stroke = isSelected ? m_snap.accent : pm.style.lineColor;
                for (int i = 0; i + 1 < line.size(); ++i)
                    appendSubdividedSegment(line[i], line[i + 1], stroke, lineRadius);
            }

            // Point marker (also add an anchor marker if a polygon/line has no explicit point).
            const GeoCoord anchor = pm.anchor();
            const QVector3D p = GlobeView::latLonToVec3(anchor.lat, anchor.lon, pointRadius);
            QColor col = pm.style.iconColor;
            if (!col.isValid() || col.alpha() < 20)
                col = m_snap.accent;
            const float pxSize = static_cast<float>(std::clamp(9.0 * std::max(0.65, pm.style.iconScale), 7.0, 22.0));
            markerVerts.push_back({p.x(), p.y(), p.z(),
                                   static_cast<float>(col.redF()),
                                   static_cast<float>(col.greenF()),
                                   static_cast<float>(col.blueF()),
                                   1.0f, pxSize, isSelected ? 1.0f : 0.0f});
        }

        m_kmlPolyCount = static_cast<int>(polyVerts.size());
        glBindVertexArray(m_kmlPolyVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_kmlPolyVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(polyVerts.size() * sizeof(KmlVertex)), polyVerts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), reinterpret_cast<void *>(3 * sizeof(float)));

        m_kmlLineCount = static_cast<int>(lineVerts.size());
        glBindVertexArray(m_kmlLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_kmlLineVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineVerts.size() * sizeof(KmlVertex)), lineVerts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(KmlVertex), reinterpret_cast<void *>(3 * sizeof(float)));

        m_kmlMarkerCount = static_cast<int>(markerVerts.size());
        glBindVertexArray(m_kmlMarkerVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_kmlMarkerVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(markerVerts.size() * sizeof(MarkerVertex)), markerVerts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex), reinterpret_cast<void *>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex), reinterpret_cast<void *>(7 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex), reinterpret_cast<void *>(8 * sizeof(float)));
        glBindVertexArray(0);
    }

    void renderKmlOverlays(const QMatrix4x4 &vp) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        if (m_kmlPolyCount > 0) {
            glDisable(GL_CULL_FACE);
            m_colorProg.bind();
            m_colorProg.setUniformValue("uVp", vp);
            m_colorProg.setUniformValue("uUseVertexColor", 1.0f);
            glBindVertexArray(m_kmlPolyVao);
            glDrawArrays(GL_TRIANGLES, 0, m_kmlPolyCount);
            glBindVertexArray(0);
            m_colorProg.release();
            glEnable(GL_CULL_FACE);
        }

        if (m_kmlLineCount > 0) {
            glLineWidth(2.0f);
            m_colorProg.bind();
            m_colorProg.setUniformValue("uVp", vp);
            m_colorProg.setUniformValue("uUseVertexColor", 1.0f);
            glBindVertexArray(m_kmlLineVao);
            glDrawArrays(GL_LINES, 0, m_kmlLineCount);
            glBindVertexArray(0);
            m_colorProg.release();
        }

        if (m_kmlMarkerCount > 0) {
            glEnable(GL_PROGRAM_POINT_SIZE);
            m_markerProg.bind();
            m_markerProg.setUniformValue("uVp", vp);
            m_markerProg.setUniformValue("uDpr", m_snap.devicePixelRatio);
            m_markerProg.setUniformValue("uAccent", QVector3D(m_snap.accent.redF(), m_snap.accent.greenF(), m_snap.accent.blueF()));
            glBindVertexArray(m_kmlMarkerVao);
            glDrawArrays(GL_POINTS, 0, m_kmlMarkerCount);
            glBindVertexArray(0);
            m_markerProg.release();
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void renderAtmosphere(const QMatrix4x4 &vp) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glCullFace(GL_FRONT);

        m_atmoProg.bind();
        m_atmoProg.setUniformValue("uVp", vp);
        m_atmoProg.setUniformValue("uCamPos", m_snap.camPos);
        m_atmoProg.setUniformValue("uAccent", QVector3D(m_snap.accent.redF(), m_snap.accent.greenF(), m_snap.accent.blueF()));
        glBindVertexArray(m_atmoVao);
        glDrawElements(GL_TRIANGLES, m_atmoIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        m_atmoProg.release();

        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    bool m_initialized = false;
    RenderSnapshot m_snap;
    quint64 m_frameCounter = 0;

    QOpenGLShaderProgram m_tileProg;
    QOpenGLShaderProgram m_colorProg;
    QOpenGLShaderProgram m_markerProg;
    QOpenGLShaderProgram m_atmoProg;

    GLuint m_patchVao = 0, m_patchVbo = 0, m_patchIbo = 0;
    int m_patchIndexCount = 0;
    GLuint m_capsVao = 0, m_capsVbo = 0;
    int m_capsVertexCount = 0;
    GLuint m_gratVao = 0, m_gratVbo = 0;
    int m_gratVertexCount = 0;
    GLuint m_atmoVao = 0, m_atmoVbo = 0, m_atmoIbo = 0;
    int m_atmoIndexCount = 0;

    GLuint m_kmlPolyVao = 0, m_kmlPolyVbo = 0;
    int m_kmlPolyCount = 0;
    GLuint m_kmlLineVao = 0, m_kmlLineVbo = 0;
    int m_kmlLineCount = 0;
    GLuint m_kmlMarkerVao = 0, m_kmlMarkerVbo = 0;
    int m_kmlMarkerCount = 0;

    quint64 m_uploadedKmlRevision = 0;
    QString m_uploadedSelectedId;
    bool m_kmlDirty = true;

    GLuint m_fallbackTex = 0;
    GLuint m_flatDemTex = 0;
    QHash<TileKey, GpuTile> m_tileTextures;
};

} // namespace

GlobeView::GlobeView(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    setMirrorVertically(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemHasContents, true);

    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, &GlobeView::tickAnimation);
}

QQuickFramebufferObject::Renderer *GlobeView::createRenderer() const {
    return new GlobeRenderer();
}

void GlobeView::setController(Backend *backend) {
    if (m_backend == backend)
        return;
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::settingsChanged, this, [this]() {
            requestNeededTilesForCamera();
            update();
        });
        connect(m_backend, &Backend::kmlDataChanged, this, [this]() {
            updateSelectedMarkerProjection();
            update();
        });
        connect(m_backend, &Backend::selectionChanged, this, [this]() {
            updateSelectedMarkerProjection();
            update();
        });
        connect(m_backend, &Backend::flyToRequested, this, &GlobeView::flyTo);
        if (auto *tm = m_backend->tileManager()) {
            connect(tm, &TileManager::tileReady, this, [this]() { update(); });
        }
        requestNeededTilesForCamera();
    }
    emit controllerChanged();
    update();
}

void GlobeView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size() && newGeometry.width() > 0 && newGeometry.height() > 0) {
        requestNeededTilesForCamera();
        updateSelectedMarkerProjection();
    }
}

void GlobeView::setAccentColor(const QString &hex) {
    const QColor c(hex);
    if (!c.isValid() || c == m_accentColor)
        return;
    m_accentColor = c;
    emit accentColorChanged();
    update();
}

void GlobeView::setBackgroundColor(const QString &hex) {
    const QColor c(hex);
    if (!c.isValid() || c == m_backgroundColor)
        return;
    m_backgroundColor = c;
    emit backgroundColorChanged();
    update();
}

QVector3D GlobeView::latLonToVec3(double latDeg, double lonDeg, double radius) {
    const double lat = latDeg * kDegToRad;
    const double lon = lonDeg * kDegToRad;
    return QVector3D(
        static_cast<float>(radius * std::cos(lat) * std::sin(lon)),
        static_cast<float>(radius * std::sin(lat)),
        static_cast<float>(radius * std::cos(lat) * std::cos(lon)));
}

void GlobeView::vec3ToLatLon(const QVector3D &p, double &latDeg, double &lonDeg) {
    const QVector3D n = p.normalized();
    latDeg = std::asin(std::clamp(static_cast<double>(n.y()), -1.0, 1.0)) * kRadToDeg;
    lonDeg = std::atan2(static_cast<double>(n.x()), static_cast<double>(n.z())) * kRadToDeg;
}

QVector3D GlobeView::cameraWorldPos() const {
    const QVector3D n = latLonToVec3(m_lat, m_lon, 1.0);
    QVector3D worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(n.y()) > 0.998f)
        worldUp = QVector3D(0.0f, 0.0f, 1.0f);
    const QVector3D east = QVector3D::crossProduct(worldUp, n).normalized();
    const QVector3D north = QVector3D::crossProduct(n, east).normalized();

    const float hRad = static_cast<float>(m_heading * kDegToRad);
    const QVector3D northRot = (north * std::cos(hRad) + east * std::sin(hRad)).normalized();

    const float pRad = static_cast<float>(m_pitch * kDegToRad);
    const QVector3D camDir = (n * std::cos(pRad) - northRot * std::sin(pRad)).normalized();
    return n + camDir * static_cast<float>(m_altitude);
}

QMatrix4x4 GlobeView::viewMatrix() const {
    const QVector3D n = latLonToVec3(m_lat, m_lon, 1.0);
    QVector3D worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(n.y()) > 0.998f)
        worldUp = QVector3D(0.0f, 0.0f, 1.0f);
    const QVector3D east = QVector3D::crossProduct(worldUp, n).normalized();
    const QVector3D north = QVector3D::crossProduct(n, east).normalized();

    const float hRad = static_cast<float>(m_heading * kDegToRad);
    const QVector3D northRot = (north * std::cos(hRad) + east * std::sin(hRad)).normalized();

    const float pRad = static_cast<float>(m_pitch * kDegToRad);
    const QVector3D camDir = (n * std::cos(pRad) - northRot * std::sin(pRad)).normalized();
    const QVector3D eye = n + camDir * static_cast<float>(m_altitude);
    const QVector3D up = (northRot * std::cos(pRad) + n * std::sin(pRad)).normalized();

    QMatrix4x4 m;
    m.lookAt(eye, n, up);
    return m;
}

QMatrix4x4 GlobeView::projectionMatrix(float aspect) const {
    QMatrix4x4 m;
    const float nearPlane = static_cast<float>(std::clamp(m_altitude * 0.025, 0.00005, 0.25));
    const float farPlane = static_cast<float>(m_altitude + 4.5);
    m.perspective(42.0f, std::max(0.1f, aspect), nearPlane, farPlane);
    return m;
}

void GlobeView::flyTo(double lat, double lon, double altitudeRadii) {
    m_flying = true;
    m_velLat = 0.0;
    m_velLon = 0.0;
    m_targetLat = std::clamp(lat, -84.0, 84.0);
    m_targetLon = wrapLon(lon);
    m_targetAlt = std::clamp(altitudeRadii, 0.001, 3.2);
    m_targetPitch = m_pitch;
    m_targetHeading = m_heading;
    if (!m_animTimer.isActive())
        m_animTimer.start();
}

void GlobeView::zoomBy(double factor) {
    m_flying = false;
    m_altitude = std::clamp(m_altitude * factor, 0.0008, 3.2);
    requestNeededTilesForCamera();
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();
}

void GlobeView::panBy(double dLatDeg, double dLonDeg) {
    m_flying = false;
    const double scale = std::clamp(m_altitude * 0.6, 0.004, 1.0);
    m_lat = std::clamp(m_lat + dLatDeg * scale, -84.0, 84.0);
    m_lon = wrapLon(m_lon + dLonDeg * scale);
    requestNeededTilesForCamera();
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();
}

void GlobeView::tiltBy(double dPitchDeg) {
    m_flying = false;
    m_pitch = std::clamp(m_pitch + dPitchDeg, 0.0, 62.0);
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();
}

void GlobeView::rotateHeadingBy(double dHeadingDeg) {
    m_flying = false;
    m_heading = wrapLon(m_heading + dHeadingDeg);
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();
}

void GlobeView::resetNorth() {
    m_flying = true;
    m_targetLat = m_lat;
    m_targetLon = m_lon;
    m_targetAlt = m_altitude;
    m_targetPitch = 0.0;
    m_targetHeading = 0.0;
    if (!m_animTimer.isActive())
        m_animTimer.start();
}

void GlobeView::resetHome() {
    m_flying = true;
    m_targetLat = 22.0;
    m_targetLon = 10.0;
    m_targetAlt = 1.95;
    m_targetPitch = 0.0;
    m_targetHeading = 0.0;
    if (!m_animTimer.isActive())
        m_animTimer.start();
}

void GlobeView::tickAnimation() {
    bool active = false;
    if (m_flying) {
        constexpr double k = 0.14;
        const double dLat = m_targetLat - m_lat;
        const double dLon = shortestLonDelta(m_lon, m_targetLon);
        const double dAlt = m_targetAlt - m_altitude;
        const double dPitch = m_targetPitch - m_pitch;
        const double dHead = shortestLonDelta(m_heading, m_targetHeading);

        m_lat += dLat * k;
        m_lon = wrapLon(m_lon + dLon * k);
        m_altitude += dAlt * k;
        m_pitch += dPitch * k;
        m_heading = wrapLon(m_heading + dHead * k);

        if (std::abs(dLat) < 0.01 && std::abs(dLon) < 0.01 && std::abs(dAlt) < 0.0005 &&
            std::abs(dPitch) < 0.05 && std::abs(dHead) < 0.05) {
            m_lat = m_targetLat;
            m_lon = m_targetLon;
            m_altitude = m_targetAlt;
            m_pitch = m_targetPitch;
            m_heading = m_targetHeading;
            m_flying = false;
        } else {
            active = true;
        }
    } else if (!m_dragging && (std::abs(m_velLat) > 0.002 || std::abs(m_velLon) > 0.002)) {
        m_lat = std::clamp(m_lat + m_velLat, -84.0, 84.0);
        m_lon = wrapLon(m_lon + m_velLon);
        m_velLat *= 0.88;
        m_velLon *= 0.88;
        active = true;
    }

    requestNeededTilesForCamera();
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();

    if (!active)
        m_animTimer.stop();
}

bool GlobeView::screenToGlobe(const QPointF &pos, double &latDeg, double &lonDeg) const {
    if (width() <= 0.0 || height() <= 0.0)
        return false;
    const float ndcX = static_cast<float>(2.0 * pos.x() / width() - 1.0);
    const float ndcY = static_cast<float>(1.0 - 2.0 * pos.y() / height());

    const QMatrix4x4 invVp = (projectionMatrix(static_cast<float>(width() / height())) * viewMatrix()).inverted();
    const QVector4D nearP = invVp * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    const QVector4D farP = invVp * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearP.w()) < 1e-6f || std::abs(farP.w()) < 1e-6f)
        return false;

    const QVector3D orig = nearP.toVector3DAffine();
    const QVector3D dir = (farP.toVector3DAffine() - orig).normalized();

    // Intersect ray with unit sphere |orig + t * dir|^2 = 1.
    const float b = QVector3D::dotProduct(orig, dir);
    const float c = QVector3D::dotProduct(orig, orig) - 1.0f;
    const float disc = b * b - c;
    if (disc < 0.0f)
        return false;
    const float t = -b - std::sqrt(disc);
    if (t < 0.0f)
        return false;

    const QVector3D hit = orig + dir * t;
    vec3ToLatLon(hit, latDeg, lonDeg);
    return true;
}

void GlobeView::updateCursorTelemetry(const QPointF &pos) {
    double lat = 0.0, lon = 0.0;
    const bool hit = screenToGlobe(pos, lat, lon);
    if (hit) {
        m_cursorOnGlobe = true;
        m_cursorLat = lat;
        m_cursorLon = lon;
        m_cursorElevMeters = (m_backend && m_backend->tileManager())
            ? m_backend->tileManager()->sampleElevationMeters(lat, lon)
            : 0.0;
    } else {
        m_cursorOnGlobe = false;
    }
    emit cursorChanged();
}

void GlobeView::updateSelectedMarkerProjection() {
    if (!m_backend || !m_backend->hasSelection() || width() <= 0.0 || height() <= 0.0) {
        if (m_selectedMarkerVisible) {
            m_selectedMarkerVisible = false;
            emit selectedMarkerChanged();
        }
        return;
    }

    const QVariantMap sel = m_backend->selectedPlacemarkVariant();
    const QVector3D world = latLonToVec3(sel.value(QStringLiteral("lat")).toDouble(),
                                         sel.value(QStringLiteral("lon")).toDouble(), 1.0045);
    const QVector3D cam = cameraWorldPos();
    if (QVector3D::dotProduct(world, cam) < 1.0f) {
        if (m_selectedMarkerVisible) {
            m_selectedMarkerVisible = false;
            emit selectedMarkerChanged();
        }
        return;
    }

    const QMatrix4x4 vp = projectionMatrix(static_cast<float>(width() / height())) * viewMatrix();
    const QVector4D clip = vp * QVector4D(world, 1.0f);
    if (clip.w() <= 0.0f) {
        m_selectedMarkerVisible = false;
        emit selectedMarkerChanged();
        return;
    }

    const double sx = (clip.x() / clip.w() * 0.5 + 0.5) * width();
    const double sy = (1.0 - (clip.y() / clip.w() * 0.5 + 0.5)) * height();
    m_selectedMarkerVisible = (sx >= 0 && sx <= width() && sy >= 0 && sy <= height());
    m_selectedMarkerX = sx;
    m_selectedMarkerY = sy;
    emit selectedMarkerChanged();
}

QString GlobeView::pickPlacemarkAt(const QPointF &pos) const {
    if (!m_backend || width() <= 0.0 || height() <= 0.0)
        return {};

    const QMatrix4x4 vp = projectionMatrix(static_cast<float>(width() / height())) * viewMatrix();
    const QVector3D cam = cameraWorldPos();

    QString bestId;
    double bestDistSq = 18.0 * 18.0; // 18px click radius

    for (const auto &pm : m_backend->visiblePlacemarkList()) {
        const GeoCoord a = pm.anchor();
        const QVector3D world = latLonToVec3(a.lat, a.lon, 1.0045);
        // Cull markers on the far hemisphere behind the horizon.
        if (QVector3D::dotProduct(world, cam) < 0.995f)
            continue;
        const QVector4D clip = vp * QVector4D(world, 1.0f);
        if (clip.w() <= 0.0f)
            continue;
        const double sx = (clip.x() / clip.w() * 0.5 + 0.5) * width();
        const double sy = (1.0 - (clip.y() / clip.w() * 0.5 + 0.5)) * height();
        const double dx = sx - pos.x();
        const double dy = sy - pos.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDistSq) {
            bestDistSq = d2;
            bestId = pm.id;
        }
    }
    return bestId;
}

void GlobeView::requestNeededTilesForCamera() {
    if (!m_backend || !m_backend->tileManager())
        return;
    auto *tm = m_backend->tileManager();
    const float aspect = height() > 0.0 ? static_cast<float>(width() / height()) : 1.5f;
    const QMatrix4x4 vp = projectionMatrix(aspect) * viewMatrix();
    const QVector3D camPos = cameraWorldPos();

    const int layer = m_backend->baseLayer();
    const int maxZoom = std::min(TileManager::maxZoomForLayer(layer),
                                 std::clamp(static_cast<int>(std::ceil(std::log2(3.5 / std::max(0.0005, m_altitude)))) + 1, 2, 16));

    // Always ensure zoom 1 root tiles are available as instant fallback.
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            tm->requestTile(layer, 1, x, y);

    QVector<VisibleTile> tiles;
    tiles.reserve(96);
    collectVisibleTiles(1, 0, 0, maxZoom, camPos, vp, tiles);
    collectVisibleTiles(1, 1, 0, maxZoom, camPos, vp, tiles);
    collectVisibleTiles(1, 0, 1, maxZoom, camPos, vp, tiles);
    collectVisibleTiles(1, 1, 1, maxZoom, camPos, vp, tiles);

    for (const auto &t : tiles) {
        tm->requestTile(layer, t.z, t.x, t.y);
        if (m_backend->terrainEnabled()) {
            const int demZ = std::min(t.z, 10);
            const int shift = t.z - demZ;
            tm->requestTile(static_cast<int>(BaseLayer::ElevationDem), demZ, t.x >> shift, t.y >> shift);
        }
    }
}

void GlobeView::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus();
    m_flying = false;
    m_velLat = 0.0;
    m_velLon = 0.0;
    m_pressPos = event->position();
    m_lastPos = m_pressPos;
    m_dragMoved = false;
    m_tilting = (event->button() == Qt::RightButton ||
                 event->button() == Qt::MiddleButton ||
                 (event->modifiers() & Qt::ShiftModifier));
    m_dragging = true;
    event->accept();
}

void GlobeView::mouseMoveEvent(QMouseEvent *event) {
    const QPointF cur = event->position();
    const QPointF delta = cur - m_lastPos;
    m_lastPos = cur;

    if ((cur - m_pressPos).manhattanLength() > 4.0)
        m_dragMoved = true;

    if (m_dragging) {
        if (m_tilting) {
            m_heading = wrapLon(m_heading + delta.x() * 0.25);
            m_pitch = std::clamp(m_pitch - delta.y() * 0.25, 0.0, 62.0);
        } else {
            const double degPerPx = std::clamp(m_altitude * 0.14, 0.0003, 0.35);
            const double cosLat = std::max(0.2, std::cos(m_lat * kDegToRad));
            const double hRad = m_heading * kDegToRad;
            const double dx = -delta.x() * degPerPx / cosLat;
            const double dy = delta.y() * degPerPx;
            const double dLon = dx * std::cos(hRad) + dy * std::sin(hRad) / cosLat;
            const double dLat = -dx * std::sin(hRad) * cosLat + dy * std::cos(hRad);

            m_lat = std::clamp(m_lat + dLat, -84.0, 84.0);
            m_lon = wrapLon(m_lon + dLon);
            m_velLat = dLat * 0.45;
            m_velLon = dLon * 0.45;
        }
        requestNeededTilesForCamera();
        updateSelectedMarkerProjection();
        emit cameraChanged();
        update();
    }
    updateCursorTelemetry(cur);
    event->accept();
}

void GlobeView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging && !m_dragMoved && event->button() == Qt::LeftButton && m_backend) {
        const QString picked = pickPlacemarkAt(event->position());
        if (!picked.isEmpty())
            m_backend->selectPlacemark(picked, false);
        else
            m_backend->clearSelection();
    } else if (m_dragging && !m_tilting && (std::abs(m_velLat) > 0.01 || std::abs(m_velLon) > 0.01)) {
        if (!m_animTimer.isActive())
            m_animTimer.start();
    }
    m_dragging = false;
    m_tilting = false;
    event->accept();
}

void GlobeView::hoverMoveEvent(QHoverEvent *event) {
    updateCursorTelemetry(event->position());
    event->accept();
}

void GlobeView::wheelEvent(QWheelEvent *event) {
    m_flying = false;
    const double steps = event->angleDelta().y() / 120.0;
    const double factor = std::pow(0.84, steps);

    // Gently pull the camera toward the cursor point when zooming in.
    double hitLat = 0.0, hitLon = 0.0;
    if (steps > 0 && screenToGlobe(event->position(), hitLat, hitLon)) {
        m_lat = std::clamp(m_lat + (hitLat - m_lat) * 0.14, -84.0, 84.0);
        m_lon = wrapLon(m_lon + shortestLonDelta(m_lon, hitLon) * 0.14);
    }

    m_altitude = std::clamp(m_altitude * factor, 0.0008, 3.2);
    requestNeededTilesForCamera();
    updateSelectedMarkerProjection();
    emit cameraChanged();
    update();
    event->accept();
}

void GlobeView::mouseDoubleClickEvent(QMouseEvent *event) {
    double lat = 0.0, lon = 0.0;
    if (screenToGlobe(event->position(), lat, lon))
        flyTo(lat, lon, std::max(0.003, m_altitude * 0.35));
    event->accept();
}
