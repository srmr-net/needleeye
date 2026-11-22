import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: panel
    radius: theme.radius
    color: Qt.rgba(0.06, 0.06, 0.08, 0.90)
    border.width: 1
    border.color: "#28ffffff"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    signal flyToClicked()

    readonly property var pm: backend.selectedPlacemark

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: pm.color || theme.accent
            }

            Text {
                Layout.fillWidth: true
                text: pm.feedName || qsTr("FEATURE")
                color: "#88ffffff"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.2
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: Math.min(4, theme.radius)
                color: "#18ffffff"
                Text { anchors.centerIn: parent; text: "×"; color: theme.foreground; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.clearSelection() }
            }
        }

        Text {
            Layout.fillWidth: true
            text: pm.name || ""
            color: theme.foreground
            font.pixelSize: 16
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Text {
            visible: (pm.folder || "") !== ""
            Layout.fillWidth: true
            text: pm.folder || ""
            color: theme.accent
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        // Coordinate & timestamp telemetry box.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: metaCol.implicitHeight + 14
            radius: Math.min(4, theme.radius)
            color: "#12ffffff"
            border.width: 1
            border.color: "#1cffffff"

            ColumnLayout {
                id: metaCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 8
                spacing: 4

                Text {
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: "#ddffffff"
                    text: pm.lat !== undefined
                        ? (Number(pm.lat).toFixed(4) + "°, " + Number(pm.lon).toFixed(4) + "°")
                        : ""
                }

                Text {
                    visible: pm.alt !== undefined && Math.abs(Number(pm.alt)) > 0.1
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: "#99ffffff"
                    text: qsTr("Altitude: %1 m").arg(Math.round(Number(pm.alt || 0)))
                }

                Text {
                    visible: (pm.time || "") !== ""
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: "#99ffffff"
                    text: pm.time || ""
                }
            }
        }

        // Action buttons.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                radius: Math.min(4, theme.radius)
                color: theme.accent

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Fly To")
                    color: theme.accentForeground
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.flyToClicked()
                }
            }

            Rectangle {
                visible: (pm.linkUrl || "") !== ""
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                radius: Math.min(4, theme.radius)
                color: "#1cffffff"
                border.width: 1
                border.color: "#2cffffff"

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Open Report")
                    color: theme.foreground
                    font.pixelSize: 12
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.openExternalLink(pm.linkUrl)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#1cffffff" }

        // Scrollable description & KML ExtendedData.
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: detailCol.implicitHeight

            ColumnLayout {
                id: detailCol
                width: parent.width
                spacing: 8

                Text {
                    visible: (pm.description || "") !== ""
                    Layout.fillWidth: true
                    text: pm.description || ""
                    color: "#ccffffff"
                    font.pixelSize: 12
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: pm.extendedData || []
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: modelData.key
                            color: "#77ffffff"
                            font.pixelSize: 11
                            Layout.preferredWidth: 100
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: "#ddffffff"
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
