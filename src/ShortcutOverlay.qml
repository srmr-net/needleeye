import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: overlay
    signal closeRequested()

    Rectangle {
        anchors.fill: parent
        color: "#aa000000"
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: overlay.closeRequested()
            onWheel: function(wheel) { wheel.accepted = true }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: contentCol.implicitHeight + 36
        radius: theme.radius
        color: Qt.rgba(0.06, 0.06, 0.08, 0.96)
        border.width: 1
        border.color: theme.accent

        Material.theme: Material.Dark
        Material.accent: theme.accent
        Material.roundedScale: theme.radius

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onWheel: function(wheel) { wheel.accepted = true }
        }

        ColumnLayout {
            id: contentCol
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Keyboard Shortcuts")
                    color: theme.foreground
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Text {
                    text: "Esc / ?"
                    color: "#77ffffff"
                    font.family: "monospace"
                    font.pixelSize: 11
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#22ffffff" }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 18
                rowSpacing: 8

                Repeater {
                    model: [
                        { key: "/", desc: qsTr("Jump to KML feature or place") },
                        { key: "L", desc: qsTr("Toggle feeds & layers sidebar") },
                        { key: "H", desc: qsTr("Hide or restore HUD (globe-only mode)") },
                        { key: "O", desc: qsTr("Open local KML / KMZ / CZML file") },
                        { key: "F5 / Ctrl+R", desc: qsTr("Refresh active KML feeds") },
                        { key: "Space", desc: qsTr("Stop flight, or play / pause timeline") },
                        { key: "[ / ]", desc: qsTr("Step timeline backward / forward") },
                        { key: "1 – 9", desc: qsTr("Switch enabled base maps") },
                        { key: ", / Ctrl+,", desc: qsTr("Open settings & base map manager") },
                        { key: "T / G / D", desc: qsTr("Toggle 3D terrain / grid / day-night") },
                        { key: "Arrows", desc: qsTr("Pan (Alt: fine · Shift: tilt/rotate)") },
                        { key: "+ / - / PgUp", desc: qsTr("Zoom in / out (or scroll wheel)") },
                        { key: "N / U / R", desc: qsTr("Reset north / tilt / both (0: home)") },
                        { key: "Mid / Shift", desc: qsTr("Drag to tilt & rotate (Right: configurable)") },
                        { key: "?", desc: qsTr("Show or hide this shortcut sheet") },
                        { key: "Q", desc: qsTr("Quit Needleeye") }
                    ]

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 74
                            Layout.preferredHeight: 22
                            radius: Math.min(4, theme.radius)
                            color: "#18ffffff"
                            border.width: 1
                            border.color: "#28ffffff"

                            Text {
                                anchors.centerIn: parent
                                text: modelData.key
                                color: theme.accent
                                font.family: "monospace"
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.desc
                            color: "#ddffffff"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
