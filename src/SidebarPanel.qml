import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: panel
    radius: theme.radius
    color: Qt.rgba(0.06, 0.06, 0.08, 0.88)
    border.width: 1
    border.color: "#24ffffff"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property alias urlInputFocused: urlField.activeFocus
    signal manageBaseMapsRequested()

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // Base map selector.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: qsTr("BASE MAP")
                color: "#80ffffff"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.4
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.preferredHeight: 20
                Layout.preferredWidth: manageBtnTxt.implicitWidth + 12
                radius: Math.min(4, theme.radius)
                color: "#14ffffff"
                Text {
                    id: manageBtnTxt
                    anchors.centerIn: parent
                    text: qsTr("Manage (,)")
                    color: "#99ffffff"
                    font.pixelSize: 10
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.manageBaseMapsRequested()
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 6
            columnSpacing: 6

            Repeater {
                model: backend.enabledBaseMaps

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: Math.min(4, theme.radius)
                    color: modelData.active ? theme.accent : "#16ffffff"
                    border.width: 1
                    border.color: modelData.active ? theme.accent : "#20ffffff"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: (modelData.slot <= 9 ? (modelData.slot + " · ") : "") + modelData.name
                        color: modelData.active ? theme.accentForeground : theme.foreground
                        font.pixelSize: 11
                        font.weight: modelData.active ? Font.DemiBold : Font.Normal
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.selectBaseMap(modelData.id)
                    }
                }
            }
        }

        // Globe display toggles.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                radius: Math.min(4, theme.radius)
                color: backend.terrainEnabled ? "#26ffffff" : "#10ffffff"
                border.width: 1
                border.color: backend.terrainEnabled ? theme.accent : "#20ffffff"

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Terrain (T)")
                    color: backend.terrainEnabled ? theme.accent : "#99ffffff"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.terrainEnabled = !backend.terrainEnabled
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                radius: Math.min(4, theme.radius)
                color: backend.graticuleEnabled ? "#26ffffff" : "#10ffffff"
                border.width: 1
                border.color: backend.graticuleEnabled ? theme.accent : "#20ffffff"

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Grid (G)")
                    color: backend.graticuleEnabled ? theme.accent : "#99ffffff"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.graticuleEnabled = !backend.graticuleEnabled
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                radius: Math.min(4, theme.radius)
                color: backend.dayNightEnabled ? "#26ffffff" : "#10ffffff"
                border.width: 1
                border.color: backend.dayNightEnabled ? theme.accent : "#20ffffff"

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Sun (D)")
                    color: backend.dayNightEnabled ? theme.accent : "#99ffffff"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.dayNightEnabled = !backend.dayNightEnabled
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#1cffffff" }

        // KML/KMZ Feeds header + actions.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: qsTr("KML / CZML FEEDS")
                color: "#80ffffff"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.4
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.preferredHeight: 22
                Layout.preferredWidth: openBtnTxt.implicitWidth + 12
                radius: Math.min(4, theme.radius)
                color: "#1affffff"
                Text { id: openBtnTxt; anchors.centerIn: parent; text: qsTr("Open (O)"); color: theme.foreground; font.pixelSize: 10 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.openLocalFileDialog() }
            }

            Rectangle {
                Layout.preferredHeight: 22
                Layout.preferredWidth: refreshBtnTxt.implicitWidth + 12
                radius: Math.min(4, theme.radius)
                color: "#1affffff"
                Text { id: refreshBtnTxt; anchors.centerIn: parent; text: qsTr("Sync (F5)"); color: theme.accent; font.pixelSize: 10 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.refreshAllFeeds() }
            }
        }

        // Feeds list.
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 140
            clip: true
            spacing: 6
            model: backend.feeds

            delegate: Rectangle {
                width: ListView.view.width
                height: feedCol.implicitHeight + 14
                radius: Math.min(4, theme.radius)
                color: modelData.enabled ? "#1cffffff" : "#0cffffff"
                border.width: 1
                border.color: modelData.error !== "" ? "#aaef4444" : (modelData.enabled ? "#33ffffff" : "#14ffffff")

                ColumnLayout {
                    id: feedCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 2
                            color: modelData.enabled ? theme.accent : "transparent"
                            border.width: 1
                            border.color: modelData.enabled ? theme.accent : "#66ffffff"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: modelData.enabled ? theme.foreground : "#88ffffff"
                            font.pixelSize: 12
                            font.weight: modelData.enabled ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            font.family: "monospace"
                            font.pixelSize: 10
                            color: modelData.loading ? theme.accent : "#88ffffff"
                            text: modelData.loading ? "…" : (modelData.count > 0 ? modelData.count : "")
                        }
                    }

                    Text {
                        visible: modelData.error !== "" || modelData.updated !== ""
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        font.pixelSize: 10
                        color: modelData.error !== "" ? "#fca5a5" : "#66ffffff"
                        elide: Text.ElideRight
                        text: modelData.error !== ""
                            ? modelData.error
                            : qsTr("Updated %1").arg(modelData.updated)
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.toggleFeed(modelData.id)
                }
            }
        }

        // Add custom KML URL input.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("Add KML / KMZ / CZML URL…")
                font.pixelSize: 12
                Material.roundedScale: theme.radius
                onAccepted: {
                    if (text.trim() !== "") {
                        backend.addFeedUrl(text)
                        text = ""
                        focus = false
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: Math.min(4, theme.radius)
                color: theme.accent

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: theme.accentForeground
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (urlField.text.trim() !== "") {
                            backend.addFeedUrl(urlField.text)
                            urlField.text = ""
                            urlField.focus = false
                        }
                    }
                }
            }
        }

        // KML Folder tree (appears when active feeds contain folders).
        Rectangle {
            visible: backend.folders.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#1cffffff"
        }

        Text {
            visible: backend.folders.length > 0
            text: qsTr("FOLDERS")
            color: "#80ffffff"
            font.pixelSize: 10
            font.weight: Font.Bold
            font.letterSpacing: 1.4
        }

        ListView {
            visible: backend.folders.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, contentHeight)
            clip: true
            spacing: 4
            model: backend.folders

            delegate: Rectangle {
                width: ListView.view.width
                height: 26
                radius: Math.min(4, theme.radius)
                color: modelData.visible ? "#12ffffff" : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: modelData.visible ? theme.accent : "#33ffffff"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: modelData.visible ? "#ddffffff" : "#66ffffff"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    Text {
                        text: modelData.count
                        color: "#66ffffff"
                        font.family: "monospace"
                        font.pixelSize: 10
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.toggleFolder(modelData.feedId, modelData.path)
                }
            }
        }
    }
}
