import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: nav
    implicitWidth: navCol.implicitWidth
    implicitHeight: navCol.implicitHeight

    property real heading: 0.0
    property real pitch: 0.0

    signal zoomInStep()
    signal zoomOutStep()
    signal rotateDelta(real degrees)
    signal resetNorthClicked()
    signal resetNorthAndTiltClicked()
    signal toggle3DClicked()
    signal resetHomeClicked()

    // Swallow wheel and hover events so pointer gestures over the nav cluster
    // never leak through to the underlying globe.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    // Continuous zoom repeat while holding + or -.
    Timer {
        id: zoomRepeatTimer
        interval: 60
        repeat: true
        property bool zoomIn: true
        onTriggered: zoomIn ? nav.zoomInStep() : nav.zoomOutStep()
    }

    Column {
        id: navCol
        spacing: 8

    // Interactive rotating compass dial (true circle keeps fixed radius).
    Rectangle {
        id: compass
        width: 42
        height: 42
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 21
        color: Qt.rgba(0.06, 0.06, 0.08, 0.88)
        border.width: 1
        border.color: compassMa.containsMouse || compassMa.dragging
            ? theme.accent
            : (Math.abs(nav.heading) > 0.5 ? "#55ffffff" : "#24ffffff")

        // Subtle outer cardinal ticks.
        Item {
            anchors.fill: parent
            rotation: -nav.heading

            // North needle (accent / red highlight).
            Rectangle {
                width: 3
                height: 11
                radius: 1
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 5
                color: Math.abs(nav.heading) > 0.5 ? theme.accent : "#ef4444"
            }

            // South counterweight needle.
            Rectangle {
                width: 2
                height: 9
                radius: 1
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                color: "#55ffffff"
            }

            // Center pivot.
            Rectangle {
                width: 5
                height: 5
                radius: 2.5
                anchors.centerIn: parent
                color: theme.foreground
            }
        }

        // Fixed readable 'N' badge at top when near north, or subtle degrees readout while dragging.
        Text {
            visible: compassMa.dragging
            anchors.centerIn: parent
            text: Math.round((nav.heading + 360) % 360) + "°"
            color: theme.accent
            font.family: "monospace"
            font.pixelSize: 9
            font.weight: Font.Bold
        }

        MouseArea {
            id: compassMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property bool dragging: false
            property bool moved: false
            property real pressX: 0
            property real lastX: 0

            onPressed: function(mouse) {
                dragging = true
                moved = false
                pressX = mouse.x
                lastX = mouse.x
            }
            onPositionChanged: function(mouse) {
                if (!dragging)
                    return
                if (Math.abs(mouse.x - pressX) > 3)
                    moved = true
                var dx = mouse.x - lastX
                lastX = mouse.x
                if (Math.abs(dx) > 0.1)
                    nav.rotateDelta(dx * 0.6)
            }
            onReleased: {
                if (dragging && !moved)
                    nav.resetNorthClicked()
                dragging = false
            }
            onDoubleClicked: nav.resetNorthAndTiltClicked()
            onWheel: function(wheel) { wheel.accepted = true }
        }

        ToolTip.visible: compassMa.containsMouse && !compassMa.dragging
        ToolTip.delay: 500
        ToolTip.text: qsTr("Click: North (N) · Double-click: Reset view (R) · Drag: Rotate")
    }

    // Compact vertical control strip: 3D/2D tilt, Zoom +, Zoom -, Home.
    Rectangle {
        width: 38
        height: buttonsCol.implicitHeight
        anchors.horizontalCenter: parent.horizontalCenter
        radius: theme.radius
        color: Qt.rgba(0.06, 0.06, 0.08, 0.88)
        border.width: 1
        border.color: "#24ffffff"

        Column {
            id: buttonsCol
            width: parent.width

            // 3D / 2D oblique tilt toggle.
            Rectangle {
                width: parent.width
                height: 32
                radius: Math.min(4, theme.radius)
                color: nav.pitch > 5.0 ? theme.accent : (tiltMa.containsMouse ? "#22ffffff" : "transparent")

                Text {
                    anchors.centerIn: parent
                    text: nav.pitch > 5.0 ? "2D" : "3D"
                    color: nav.pitch > 5.0 ? theme.accentForeground : theme.foreground
                    font.family: "monospace"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: tiltMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: nav.toggle3DClicked()
                    onWheel: function(wheel) { wheel.accepted = true }
                }

                ToolTip.visible: tiltMa.containsMouse
                ToolTip.delay: 500
                ToolTip.text: nav.pitch > 5.0 ? qsTr("Top-down view (U)") : qsTr("Tilt to 3D horizon (Shift+Down)")
            }

            Rectangle { width: parent.width - 10; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: "#1cffffff" }

            // Zoom In (+).
            Rectangle {
                width: parent.width
                height: 32
                color: zoomInMa.containsMouse ? "#22ffffff" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: theme.foreground
                    font.family: "monospace"
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: zoomInMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: {
                        nav.zoomInStep()
                        zoomRepeatTimer.zoomIn = true
                        zoomRepeatTimer.start()
                    }
                    onReleased: zoomRepeatTimer.stop()
                    onCanceled: zoomRepeatTimer.stop()
                    onWheel: function(wheel) { wheel.accepted = true }
                }

                ToolTip.visible: zoomInMa.containsMouse
                ToolTip.delay: 500
                ToolTip.text: qsTr("Zoom in (+ / Page Up)")
            }

            Rectangle { width: parent.width - 10; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: "#1cffffff" }

            // Zoom Out (-).
            Rectangle {
                width: parent.width
                height: 32
                color: zoomOutMa.containsMouse ? "#22ffffff" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "−"
                    color: theme.foreground
                    font.family: "monospace"
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: zoomOutMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: {
                        nav.zoomOutStep()
                        zoomRepeatTimer.zoomIn = false
                        zoomRepeatTimer.start()
                    }
                    onReleased: zoomRepeatTimer.stop()
                    onCanceled: zoomRepeatTimer.stop()
                    onWheel: function(wheel) { wheel.accepted = true }
                }

                ToolTip.visible: zoomOutMa.containsMouse
                ToolTip.delay: 500
                ToolTip.text: qsTr("Zoom out (- / Page Down)")
            }

            Rectangle { width: parent.width - 10; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: "#1cffffff" }

            // Home orbit reset (0).
            Rectangle {
                width: parent.width
                height: 30
                radius: Math.min(4, theme.radius)
                color: homeMa.containsMouse ? "#22ffffff" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "◎"
                    color: "#bbffffff"
                    font.pixelSize: 13
                }

                MouseArea {
                    id: homeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: nav.resetHomeClicked()
                    onWheel: function(wheel) { wheel.accepted = true }
                }

                ToolTip.visible: homeMa.containsMouse
                ToolTip.delay: 500
                ToolTip.text: qsTr("Home orbit (0)")
            }
        }
    }
    }
}
