import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Needleeye 1.0

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 500
    visible: true
    title: "Needleeye"
    color: theme.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool sidebarVisible: true
    property bool searchVisible: false
    property bool settingsVisible: false
    property bool helpVisible: false
    property bool textInputFocused: searchOverlay.active || sidebarPanel.urlInputFocused || settingsOverlay.inputFocused
    property bool globeShortcutsEnabled: !win.textInputFocused && !win.settingsVisible && !win.searchVisible && !win.helpVisible

    function openSettings(tabIndex) {
        win.helpVisible = false
        win.searchVisible = false
        if (tabIndex !== undefined)
            settingsOverlay.currentTab = tabIndex
        win.settingsVisible = true
        settingsOverlay.focusOverlay()
    }

    function formatCoord(value, posHem, negHem) {
        var _lang = translator.language
        var absVal = Math.abs(value).toLocaleString(Qt.locale(), 'f', 3)
        return absVal + "° " + (value >= 0 ? posHem : negHem)
    }

    function formatAltitude(km) {
        var _lang = translator.language
        if (km >= 1000)
            return Math.round(km).toLocaleString(Qt.locale(), 'f', 0) + " km"
        if (km >= 10)
            return km.toLocaleString(Qt.locale(), 'f', 1) + " km"
        return Math.round(km * 1000).toLocaleString(Qt.locale(), 'f', 0) + " m"
    }

    // Global keyboard shortcuts (globe navigation & map shortcuts pause while overlays are open).
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.textInputFocused && !win.settingsVisible; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: ","; context: Qt.ApplicationShortcut; enabled: !win.textInputFocused; onActivated: win.settingsVisible ? (win.settingsVisible = false) : win.openSettings() }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; onActivated: win.settingsVisible ? (win.settingsVisible = false) : win.openSettings() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.textInputFocused; onActivated: Qt.quit() }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: { win.searchVisible = true; searchOverlay.focusInput() } }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: win.sidebarVisible = !win.sidebarVisible }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.openLocalFileDialog() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.refreshAllFeeds() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.primaryAction() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.stepTimeline(-0.04) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.stepTimeline(0.04) }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(1) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(2) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(3) }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(4) }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(5) }
    Shortcut { sequence: "7"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(6) }
    Shortcut { sequence: "8"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(7) }
    Shortcut { sequence: "9"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.selectBaseMapBySlot(8) }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.terrainEnabled = !backend.terrainEnabled }
    Shortcut { sequence: "G"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: backend.graticuleEnabled = !backend.graticuleEnabled }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.resetNorth() }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.resetHome() }
    Shortcut { sequence: "+"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.zoomBy(0.74) }
    Shortcut { sequence: "="; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.zoomBy(0.74) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.zoomBy(1.35) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.panBy(12, 0) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.panBy(-12, 0) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.panBy(0, -16) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.panBy(0, 16) }
    Shortcut { sequence: "Shift+Up"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.tiltBy(8) }
    Shortcut { sequence: "Shift+Down"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.tiltBy(-8) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.rotateHeadingBy(-15) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: win.globeShortcutsEnabled; onActivated: globe.rotateHeadingBy(15) }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.searchVisible) {
                win.searchVisible = false
            } else if (win.settingsVisible) {
                win.settingsVisible = false
            } else if (win.helpVisible) {
                win.helpVisible = false
            } else if (backend.hasSelection) {
                backend.clearSelection()
            } else if (backend.errorMessage !== "") {
                backend.dismissError()
            }
        }
    }

    // Right-to-left languages mirror the window; Translator sets the layout direction.
    Item {
        id: root
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // Full-bleed OpenGL 3D Globe viewport.
        GlobeView {
            id: globe
            anchors.fill: parent
            enabled: !win.settingsVisible && !win.searchVisible && !win.helpVisible
            controller: backend
            accentColor: theme.accent
            backgroundColor: theme.background
        }

        // Crisp floating callout tag anchored to the selected KML placemark on the globe.
        Rectangle {
            visible: globe.selectedMarkerVisible && backend.hasSelection
            x: Math.max(12, Math.min(root.width - width - 12, globe.selectedMarkerX + 14))
            y: Math.max(56, Math.min(root.height - height - 12, globe.selectedMarkerY - height / 2))
            width: Math.min(260, calloutLabel.implicitWidth + 20)
            height: 28
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(0.06, 0.06, 0.08, 0.90)
            border.width: 1
            border.color: theme.accent

            Text {
                id: calloutLabel
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: Text.AlignVCenter
                text: backend.selectedPlacemark.name || ""
                color: theme.foreground
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }

        // Top floating telemetry & quick-action bar.
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 12
            height: 40
            radius: theme.radius
            color: Qt.rgba(0.06, 0.06, 0.08, 0.86)
            border.width: 1
            border.color: "#24ffffff"

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onWheel: function(wheel) { wheel.accepted = true }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 12

                // Brand mark + sidebar toggle.
                Rectangle {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: backend.loadingCount > 0 ? theme.accent : "#4ade80"

                    SequentialAnimation on opacity {
                        running: backend.loadingCount > 0
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 450 }
                        NumberAnimation { to: 1.0; duration: 450 }
                    }
                }

                Text {
                    text: "NEEDLEEYE"
                    color: theme.foreground
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 1.8
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 16; color: "#28ffffff" }

                // Live cursor or camera coordinates in tabular monospace.
                Text {
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: "#ccffffff"
                    text: globe.cursorOnGlobe
                        ? (win.formatCoord(globe.cursorLat, "N", "S") + "  " +
                           win.formatCoord(globe.cursorLon, "E", "W") +
                           (globe.cursorElevationMeters > 1
                               ? ("  ·  " + Math.round(globe.cursorElevationMeters) + " m")
                               : ""))
                        : (win.formatCoord(globe.cameraLat, "N", "S") + "  " +
                           win.formatCoord(globe.cameraLon, "E", "W"))
                }

                Text {
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: "#88ffffff"
                    text: qsTr("ALT %1").arg(win.formatAltitude(globe.cameraAltitudeKm))
                }

                Item { Layout.fillWidth: true }

                // Feature count badge.
                Text {
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: theme.accent
                    text: {
                        var _l = translator.language
                        return qsTr("%n features", "", backend.visiblePlacemarks)
                    }
                }

                // Active layer pill.
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: layerPillText.implicitWidth + 16
                    radius: Math.min(4, theme.radius)
                    color: "#1affffff"
                    border.width: 1
                    border.color: "#22ffffff"

                    Text {
                        id: layerPillText
                        anchors.centerIn: parent
                        text: backend.baseLayerName + (backend.terrainEnabled ? " · 3D" : "")
                        color: theme.foreground
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.cycleBaseMap()
                    }
                }

                // Search button (/).
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: searchBtnRow.implicitWidth + 14
                    radius: Math.min(4, theme.radius)
                    color: searchBtnMa.containsMouse ? "#28ffffff" : "#14ffffff"
                    border.width: 1
                    border.color: "#22ffffff"

                    Row {
                        id: searchBtnRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: qsTr("Jump"); color: theme.foreground; font.pixelSize: 11 }
                        Text { text: "/"; color: theme.accent; font.family: "monospace"; font.pixelSize: 11; font.weight: Font.Bold }
                    }

                    MouseArea {
                        id: searchBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { win.searchVisible = true; searchOverlay.focusInput() }
                    }
                }

                // Layers sidebar toggle (L).
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: layersBtnRow.implicitWidth + 14
                    radius: Math.min(4, theme.radius)
                    color: win.sidebarVisible ? theme.accent : (layersBtnMa.containsMouse ? "#28ffffff" : "#14ffffff")

                    Row {
                        id: layersBtnRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: qsTr("Layers")
                            color: win.sidebarVisible ? theme.accentForeground : theme.foreground
                            font.pixelSize: 11
                            font.weight: win.sidebarVisible ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            text: "L"
                            color: win.sidebarVisible ? theme.accentForeground : theme.accent
                            font.family: "monospace"
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }

                    MouseArea {
                        id: layersBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.sidebarVisible = !win.sidebarVisible
                    }
                }

                // Settings button (,).
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: settingsBtnRow.implicitWidth + 14
                    radius: Math.min(4, theme.radius)
                    color: win.settingsVisible ? theme.accent : (settingsBtnMa.containsMouse ? "#28ffffff" : "#14ffffff")

                    Row {
                        id: settingsBtnRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: qsTr("Settings")
                            color: win.settingsVisible ? theme.accentForeground : theme.foreground
                            font.pixelSize: 11
                            font.weight: win.settingsVisible ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            text: ","
                            color: win.settingsVisible ? theme.accentForeground : theme.accent
                            font.family: "monospace"
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }

                    MouseArea {
                        id: settingsBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.settingsVisible ? (win.settingsVisible = false) : win.openSettings()
                    }
                }

                // Shortcut help button (?).
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: 24
                    radius: Math.min(4, theme.radius)
                    color: win.helpVisible ? theme.accent : (helpBtnMa.containsMouse ? "#28ffffff" : "#14ffffff")

                    Text {
                        anchors.centerIn: parent
                        text: "?"
                        color: win.helpVisible ? theme.accentForeground : theme.foreground
                        font.family: "monospace"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: helpBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.helpVisible = !win.helpVisible
                    }
                }
            }
        }

        // Error banner (Omarchy convention: never hide errors silently).
        Rectangle {
            id: errorBanner
            visible: backend.errorMessage !== ""
            anchors.top: topBar.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 48, Math.max(360, errorText.implicitWidth + 56))
            height: 36
            radius: theme.radius
            color: "#e62d1215"
            border.width: 1
            border.color: "#ff5c5c"
            z: 20

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Text {
                    id: errorText
                    Layout.fillWidth: true
                    text: backend.errorMessage
                    color: "#ffd6d6"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    radius: Math.min(4, theme.radius)
                    color: "#22ffffff"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: "#ffd6d6"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.dismissError()
                    }
                }
            }
        }

        // Left Feeds & Layers Sidebar.
        SidebarPanel {
            id: sidebarPanel
            visible: win.sidebarVisible
            anchors.top: topBar.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.bottom: timelineBar.visible ? timelineBar.top : parent.bottom
            anchors.bottomMargin: 12
            width: 310
            z: 10
            onManageBaseMapsRequested: win.openSettings(0)
        }

        // Right Selected Placemark Inspector.
        InspectorPanel {
            id: inspectorPanel
            visible: backend.hasSelection
            anchors.top: topBar.bottom
            anchors.topMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: timelineBar.visible ? timelineBar.top : parent.bottom
            anchors.bottomMargin: 12
            width: 320
            z: 10
            onFlyToClicked: {
                var sel = backend.selectedPlacemark
                if (sel && sel.lat !== undefined)
                    globe.flyTo(sel.lat, sel.lon, 0.16)
            }
        }

        // Bottom Timeline Scrubber (when KML feeds carry timestamps).
        TimelineBar {
            id: timelineBar
            visible: backend.hasTimeline
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            height: 46
            z: 10
        }

        // Subtle base map attribution watermark.
        Text {
            visible: backend.baseLayerAttribution !== "" && !timelineBar.visible
            anchors.right: inspectorPanel.visible ? inspectorPanel.left : parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            text: backend.baseLayerAttribution
            color: "#66ffffff"
            font.pixelSize: 10
            z: 5
        }

        // Command / Jump Search Overlay (/).
        SearchOverlay {
            id: searchOverlay
            active: win.searchVisible
            anchors.fill: parent
            z: 30
            onCloseRequested: win.searchVisible = false
        }

        // Settings & Base Map Manager Overlay (,).
        SettingsOverlay {
            id: settingsOverlay
            active: win.settingsVisible
            anchors.fill: parent
            z: 35
            onCloseRequested: win.settingsVisible = false
        }

        // Keyboard Shortcut Reference Overlay (?).
        ShortcutOverlay {
            id: shortcutOverlay
            visible: win.helpVisible
            anchors.fill: parent
            z: 40
            onCloseRequested: win.helpVisible = false
        }
    }
}
