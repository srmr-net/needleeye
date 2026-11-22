import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    radius: theme.radius
    color: Qt.rgba(0.06, 0.06, 0.08, 0.88)
    border.width: 1
    border.color: "#24ffffff"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: Math.min(4, theme.radius)
            color: backend.timelinePlaying ? theme.accent : "#1cffffff"

            Text {
                anchors.centerIn: parent
                text: backend.timelinePlaying ? "❚❚" : "▶"
                color: backend.timelinePlaying ? theme.accentForeground : theme.foreground
                font.pixelSize: 11
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.primaryAction()
            }
        }

        Text {
            font.family: "monospace"
            font.pixelSize: 11
            color: backend.timelineActive ? theme.accent : "#88ffffff"
            text: backend.timelineActive ? backend.currentTimeLabel : qsTr("ALL TIME (%1)").arg(backend.timeRangeLabel)
        }

        Slider {
            id: scrubber
            Layout.fillWidth: true
            from: 0.0
            to: 1.0
            value: backend.timelineProgress
            onMoved: backend.timelineProgress = value
        }

        Rectangle {
            Layout.preferredHeight: 24
            Layout.preferredWidth: allTxt.implicitWidth + 14
            radius: Math.min(4, theme.radius)
            color: !backend.timelineActive ? theme.accent : "#16ffffff"

            Text {
                id: allTxt
                anchors.centerIn: parent
                text: qsTr("All")
                color: !backend.timelineActive ? theme.accentForeground : theme.foreground
                font.pixelSize: 11
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.timelineActive = !backend.timelineActive
            }
        }
    }
}
