import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: overlay
    visible: active

    property bool active: false
    signal closeRequested()

    function focusInput() {
        searchField.text = ""
        backend.updateSearch("")
        resultsList.currentIndex = 0
        searchField.forceActiveFocus()
    }

    Rectangle {
        anchors.fill: parent
        color: "#88000000"
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: overlay.closeRequested()
            onWheel: function(wheel) { wheel.accepted = true }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 84
        width: Math.min(540, parent.width - 48)
        height: Math.min(440, searchCol.implicitHeight + 24)
        radius: theme.radius
        color: Qt.rgba(0.07, 0.07, 0.09, 0.96)
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
            id: searchCol
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Jump to KML feature, city, or place…")
                font.pixelSize: 14
                Material.roundedScale: theme.radius
                onTextChanged: {
                    backend.updateSearch(text)
                    resultsList.currentIndex = 0
                }
                Keys.onDownPressed: {
                    if (resultsList.count > 0)
                        resultsList.currentIndex = Math.min(resultsList.count - 1, resultsList.currentIndex + 1)
                }
                Keys.onUpPressed: {
                    if (resultsList.count > 0)
                        resultsList.currentIndex = Math.max(0, resultsList.currentIndex - 1)
                }
                Keys.onReturnPressed: {
                    if (resultsList.count > 0) {
                        backend.activateSearchResult(Math.max(0, resultsList.currentIndex))
                        overlay.closeRequested()
                    }
                }
                Keys.onEscapePressed: overlay.closeRequested()
            }

            ListView {
                id: resultsList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, contentHeight)
                clip: true
                spacing: 4
                model: backend.searchResults

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 40
                    radius: Math.min(4, theme.radius)
                    color: ListView.isCurrentItem ? "#24ffffff" : (itemMa.containsMouse ? "#14ffffff" : "transparent")
                    border.width: ListView.isCurrentItem ? 1 : 0
                    border.color: theme.accent

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4
                            color: modelData.kind === "kml" ? (modelData.color || theme.accent) : "#60a5fa"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: theme.foreground
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.subtitle || ""
                                color: "#88ffffff"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            font.family: "monospace"
                            font.pixelSize: 10
                            color: "#66ffffff"
                            text: modelData.kind === "kml" ? "KML" : (modelData.kind === "geocode" ? "OSM" : "CITY")
                        }
                    }

                    MouseArea {
                        id: itemMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.activateSearchResult(index)
                            overlay.closeRequested()
                        }
                    }
                }
            }

            Text {
                visible: searchField.text.trim() !== "" && backend.searchResults.length === 0
                text: backend.searching ? qsTr("Searching…") : qsTr("No matching features or places")
                color: "#77ffffff"
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                Layout.bottomMargin: 8
            }
        }
    }
}
