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
            onClicked: overlay.closeRequested()
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
                        { key: "O", desc: qsTr("Open local KML / KMZ file") },
                        { key: "R", desc: qsTr("Refresh active KML feeds") },
                        { key: "Space", desc: qsTr("Play / pause timeline or refresh") },
                        { key: "[ / ]", desc: qsTr("Step timeline backward / forward") },
                        { key: "1 – 4", desc: qsTr("Switch Dark / Satellite / Sentinel / OSM") },
                        { key: "T / G", desc: qsTr("Toggle 3D terrain / graticule grid") },
                        { key: "Arrows", desc: qsTr("Pan globe (Shift + Arrows: tilt / rotate)") },
                        { key: "+ / -", desc: qsTr("Zoom in / out (or scroll wheel)") },
                        { key: "N / 0", desc: qsTr("Reset north / home orbit") },
                        { key: "Right-drag", desc: qsTr("Tilt horizon & rotate heading") },
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
