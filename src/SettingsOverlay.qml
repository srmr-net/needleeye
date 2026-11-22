import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: overlay
    visible: active
    focus: active

    property bool active: false
    property int currentTab: 0 // 0 = Base Maps, 1 = Display & Cache
    property string editingMapId: ""
    property string formProjection: "xyz" // "xyz" | "equirectangular" | "mercator"
    property string formError: ""
    property bool inputFocused: nameField.activeFocus || urlField.activeFocus ||
                                maxZoomField.activeFocus || attrField.activeFocus

    signal closeRequested()

    Connections {
        target: backend
        function onBaseMapImagePicked(path) {
            urlField.text = path
            if (nameField.text.trim() === "") {
                var slash = path.lastIndexOf("/")
                var fname = slash >= 0 ? path.substring(slash + 1) : path
                var dot = fname.lastIndexOf(".")
                nameField.text = dot > 0 ? fname.substring(0, dot) : fname
            }
        }
    }

    onActiveChanged: {
        if (active) {
            backend.refreshCacheStats()
            formError = ""
            syncCurrentIndexToActiveMap()
            focusOverlay()
        } else {
            resetForm()
        }
    }

    function focusOverlay() {
        overlay.forceActiveFocus()
    }

    function syncCurrentIndexToActiveMap() {
        var maps = backend.baseMaps
        for (var i = 0; i < maps.length; ++i) {
            if (maps[i].active) {
                mapsList.currentIndex = i
                mapsList.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
        if (maps.length > 0 && mapsList.currentIndex < 0)
            mapsList.currentIndex = 0
    }

    function currentMapItem() {
        var maps = backend.baseMaps
        if (mapsList.currentIndex >= 0 && mapsList.currentIndex < maps.length)
            return maps[mapsList.currentIndex]
        return null
    }

    function moveSelected(delta) {
        var item = currentMapItem()
        if (!item)
            return
        var nextIdx = mapsList.currentIndex + delta
        if (backend.moveBaseMap(item.id, delta)) {
            mapsList.currentIndex = nextIdx
            mapsList.positionViewAtIndex(nextIdx, ListView.Contain)
        }
    }

    Keys.onPressed: function(event) {
        if (!overlay.active || overlay.inputFocused)
            return

        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            overlay.currentTab = overlay.currentTab === 0 ? 1 : 0
            if (overlay.currentTab === 1)
                backend.refreshCacheStats()
            event.accepted = true
            return
        }

        if (overlay.currentTab !== 0)
            return

        var count = backend.baseMaps.length
        if (count <= 0)
            return

        if (event.key === Qt.Key_Up || event.key === Qt.Key_K) {
            if (event.modifiers & Qt.ShiftModifier) {
                moveSelected(-1)
            } else {
                mapsList.currentIndex = Math.max(0, mapsList.currentIndex - 1)
                mapsList.positionViewAtIndex(mapsList.currentIndex, ListView.Contain)
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_J) {
            if (event.modifiers & Qt.ShiftModifier) {
                moveSelected(1)
            } else {
                mapsList.currentIndex = Math.min(count - 1, mapsList.currentIndex + 1)
                mapsList.positionViewAtIndex(mapsList.currentIndex, ListView.Contain)
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            var sel = currentMapItem()
            if (sel) {
                var selIdx = mapsList.currentIndex
                backend.selectBaseMap(sel.id)
                mapsList.currentIndex = Math.min(selIdx, backend.baseMaps.length - 1)
                mapsList.positionViewAtIndex(mapsList.currentIndex, ListView.Contain)
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            var tog = currentMapItem()
            if (tog) {
                var togIdx = mapsList.currentIndex
                backend.setBaseMapEnabled(tog.id, !tog.enabled)
                mapsList.currentIndex = Math.min(togIdx, backend.baseMaps.length - 1)
                mapsList.positionViewAtIndex(mapsList.currentIndex, ListView.Contain)
            }
            event.accepted = true
        } else if (event.key === Qt.Key_E) {
            var ed = currentMapItem()
            if (ed)
                beginEdit(ed)
            event.accepted = true
        }
    }

    function openTab(tabIndex) {
        currentTab = tabIndex
        active = true
    }

    function resetForm() {
        editingMapId = ""
        formProjection = "xyz"
        nameField.text = ""
        urlField.text = ""
        maxZoomField.text = "18"
        attrField.text = ""
        formError = ""
        if (overlay.active)
            overlay.forceActiveFocus()
    }

    function beginEdit(item) {
        editingMapId = item.id
        formProjection = item.projection || "xyz"
        nameField.text = item.name
        urlField.text = item.urlTemplate
        maxZoomField.text = String(item.maxZoom)
        attrField.text = item.attribution || ""
        formError = ""
        nameField.forceActiveFocus()
    }

    function submitForm() {
        var zoom = parseInt(maxZoomField.text.trim(), 10)
        if (isNaN(zoom))
            zoom = 18
        var err = ""
        if (editingMapId !== "") {
            err = backend.updateBaseMap(editingMapId, nameField.text, urlField.text, zoom, attrField.text, formProjection)
        } else {
            err = backend.addCustomBaseMap(nameField.text, urlField.text, zoom, attrField.text, formProjection)
        }
        if (err !== "") {
            formError = err
        } else {
            resetForm()
        }
    }

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
        width: Math.min(720, parent.width - 48)
        height: Math.min(640, parent.height - 56)
        radius: theme.radius
        color: Qt.rgba(0.06, 0.06, 0.08, 0.97)
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
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            // Header bar.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: qsTr("Settings")
                    color: theme.foreground
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Item { Layout.preferredWidth: 8 }

                // Tab pills.
                Repeater {
                    model: [qsTr("Base Maps"), qsTr("Display & Cache")]

                    Rectangle {
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: tabTxt.implicitWidth + 20
                        radius: Math.min(4, theme.radius)
                        color: overlay.currentTab === index ? theme.accent : "#16ffffff"
                        border.width: 1
                        border.color: overlay.currentTab === index ? theme.accent : "#22ffffff"

                        Text {
                            id: tabTxt
                            anchors.centerIn: parent
                            text: modelData
                            color: overlay.currentTab === index ? theme.accentForeground : theme.foreground
                            font.pixelSize: 11
                            font.weight: overlay.currentTab === index ? Font.DemiBold : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                overlay.currentTab = index
                                if (index === 1)
                                    backend.refreshCacheStats()
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Esc / ,"
                    color: "#77ffffff"
                    font.family: "monospace"
                    font.pixelSize: 11
                }

                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: Math.min(4, theme.radius)
                    color: "#18ffffff"
                    Text { anchors.centerIn: parent; text: "×"; color: theme.foreground; font.pixelSize: 14 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: overlay.closeRequested() }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#22ffffff" }

            // TAB 0: BASE MAP MANAGEMENT.
            ColumnLayout {
                visible: overlay.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("↑↓ navigate · Enter select · Space toggle · Shift+↑↓ reorder · E edit")
                        color: "#99ffffff"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: resetTxt.implicitWidth + 16
                        radius: Math.min(4, theme.radius)
                        color: "#14ffffff"
                        border.width: 1
                        border.color: "#22ffffff"

                        Text {
                            id: resetTxt
                            anchors.centerIn: parent
                            text: qsTr("Reset Defaults")
                            color: "#ccffffff"
                            font.pixelSize: 11
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                overlay.resetForm()
                                backend.resetBaseMapsToDefaults()
                            }
                        }
                    }
                }

                // Base maps list.
                ListView {
                    id: mapsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 6
                    model: backend.baseMaps

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 54
                        radius: Math.min(4, theme.radius)
                        color: ListView.isCurrentItem
                            ? "#24ffffff"
                            : (modelData.active ? "#18ffffff" : (modelData.enabled ? "#10ffffff" : "#08ffffff"))
                        border.width: 1
                        border.color: ListView.isCurrentItem
                            ? theme.accent
                            : (modelData.active ? "#66ffffff" : (overlay.editingMapId === modelData.id ? "#88ffffff" : "#1cffffff"))

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            // Enable/disable toggle.
                            Rectangle {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                radius: Math.min(3, theme.radius)
                                color: modelData.enabled ? theme.accent : "transparent"
                                border.width: 1
                                border.color: modelData.enabled ? theme.accent : "#66ffffff"

                                Text {
                                    anchors.centerIn: parent
                                    visible: modelData.enabled
                                    text: "✓"
                                    color: theme.accentForeground
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var idx = index
                                        overlay.forceActiveFocus()
                                        backend.setBaseMapEnabled(modelData.id, !modelData.enabled)
                                        mapsList.currentIndex = Math.min(idx, backend.baseMaps.length - 1)
                                    }
                                }
                            }

                            // Slot key badge.
                            Rectangle {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                radius: Math.min(4, theme.radius)
                                color: modelData.active ? theme.accent : "#18ffffff"

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.slot > 0 && modelData.slot <= 9 ? String(modelData.slot) : "–"
                                    color: modelData.active ? theme.accentForeground : (modelData.enabled ? theme.accent : "#55ffffff")
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                            }

                            // Name + URL summary (click to activate).
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ColumnLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Text {
                                            text: modelData.name
                                            color: modelData.enabled ? theme.foreground : "#77ffffff"
                                            font.pixelSize: 12
                                            font.weight: modelData.active ? Font.DemiBold : Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            visible: !modelData.builtin
                                            text: qsTr("CUSTOM")
                                            color: theme.accent
                                            font.family: "monospace"
                                            font.pixelSize: 9
                                            font.weight: Font.Bold
                                        }

                                        Text {
                                            text: modelData.projection === "equirectangular"
                                                ? "EQUIRECT"
                                                : (modelData.projection === "mercator" ? "MERCATOR" : ("Z" + modelData.maxZoom))
                                            color: "#66ffffff"
                                            font.family: "monospace"
                                            font.pixelSize: 10
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.urlTemplate
                                        color: "#66ffffff"
                                        font.family: "monospace"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var idx = index
                                        overlay.forceActiveFocus()
                                        backend.selectBaseMap(modelData.id)
                                        mapsList.currentIndex = Math.min(idx, backend.baseMaps.length - 1)
                                    }
                                }
                            }

                            // Reorder up.
                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: Math.min(4, theme.radius)
                                color: "#14ffffff"
                                opacity: modelData.canMoveUp ? 1.0 : 0.3
                                Text { anchors.centerIn: parent; text: "↑"; color: theme.foreground; font.pixelSize: 11 }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: modelData.canMoveUp
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        mapsList.currentIndex = index
                                        overlay.moveSelected(-1)
                                    }
                                }
                            }

                            // Reorder down.
                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: Math.min(4, theme.radius)
                                color: "#14ffffff"
                                opacity: modelData.canMoveDown ? 1.0 : 0.3
                                Text { anchors.centerIn: parent; text: "↓"; color: theme.foreground; font.pixelSize: 11 }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: modelData.canMoveDown
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        mapsList.currentIndex = index
                                        overlay.moveSelected(1)
                                    }
                                }
                            }

                            // Edit button.
                            Rectangle {
                                Layout.preferredWidth: editLbl.implicitWidth + 14
                                Layout.preferredHeight: 24
                                radius: Math.min(4, theme.radius)
                                color: "#18ffffff"
                                Text { id: editLbl; anchors.centerIn: parent; text: qsTr("Edit"); color: theme.foreground; font.pixelSize: 10 }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: overlay.beginEdit(modelData)
                                }
                            }

                            // Delete button (custom maps only).
                            Rectangle {
                                visible: !modelData.builtin
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: Math.min(4, theme.radius)
                                color: "#28ef4444"
                                Text { anchors.centerIn: parent; text: "×"; color: "#fca5a5"; font.pixelSize: 13 }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (overlay.editingMapId === modelData.id)
                                            overlay.resetForm()
                                        backend.removeBaseMap(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#22ffffff" }

                // Add / Edit custom base map form.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: overlay.editingMapId !== "" ? qsTr("EDIT BASE MAP") : qsTr("ADD BASE MAP")
                            color: "#88ffffff"
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 1.4
                        }

                        Item { Layout.fillWidth: true }

                        Repeater {
                            model: [
                                { id: "xyz", label: qsTr("Tiled (XYZ)") },
                                { id: "equirectangular", label: qsTr("Equirectangular") },
                                { id: "mercator", label: qsTr("Mercator Image") }
                            ]

                            Rectangle {
                                Layout.preferredHeight: 22
                                Layout.preferredWidth: projLbl.implicitWidth + 14
                                radius: Math.min(4, theme.radius)
                                color: overlay.formProjection === modelData.id ? theme.accent : "#16ffffff"
                                border.width: 1
                                border.color: overlay.formProjection === modelData.id ? theme.accent : "#22ffffff"

                                Text {
                                    id: projLbl
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: overlay.formProjection === modelData.id ? theme.accentForeground : "#ccffffff"
                                    font.pixelSize: 10
                                    font.weight: overlay.formProjection === modelData.id ? Font.DemiBold : Font.Normal
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: overlay.formProjection = modelData.id
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: nameField
                            Layout.preferredWidth: 165
                            placeholderText: qsTr("Name")
                            font.pixelSize: 12
                            Material.roundedScale: theme.radius
                            onAccepted: overlay.submitForm()
                        }

                        TextField {
                            id: urlField
                            Layout.fillWidth: true
                            placeholderText: overlay.formProjection === "xyz"
                                ? qsTr("https://tiles.example.com/{z}/{x}/{y}.png")
                                : qsTr("Image URL (http/https) or local file path…")
                            font.family: "monospace"
                            font.pixelSize: 11
                            Material.roundedScale: theme.radius
                            onAccepted: overlay.submitForm()
                        }

                        TextField {
                            id: maxZoomField
                            visible: overlay.formProjection === "xyz"
                            Layout.preferredWidth: 70
                            placeholderText: qsTr("Max Z")
                            text: "18"
                            font.family: "monospace"
                            font.pixelSize: 12
                            Material.roundedScale: theme.radius
                            onAccepted: overlay.submitForm()
                        }

                        Rectangle {
                            visible: overlay.formProjection !== "xyz"
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: browseTxt.implicitWidth + 18
                            radius: Math.min(4, theme.radius)
                            color: "#1cffffff"
                            border.width: 1
                            border.color: "#2cffffff"

                            Text {
                                id: browseTxt
                                anchors.centerIn: parent
                                text: qsTr("Browse…")
                                color: theme.foreground
                                font.pixelSize: 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.browseBaseMapImage()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: attrField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Attribution (optional)")
                            font.pixelSize: 12
                            Material.roundedScale: theme.radius
                            onAccepted: overlay.submitForm()
                        }

                        Rectangle {
                            visible: overlay.editingMapId !== ""
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: cancelTxt.implicitWidth + 20
                            radius: Math.min(4, theme.radius)
                            color: "#18ffffff"

                            Text { id: cancelTxt; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.foreground; font.pixelSize: 12 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: overlay.resetForm() }
                        }

                        Rectangle {
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: submitTxt.implicitWidth + 24
                            radius: Math.min(4, theme.radius)
                            color: theme.accent

                            Text {
                                id: submitTxt
                                anchors.centerIn: parent
                                text: overlay.editingMapId !== "" ? qsTr("Save Changes") : qsTr("Add Base Map")
                                color: theme.accentForeground
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: overlay.submitForm()
                            }
                        }
                    }

                    Text {
                        visible: overlay.formError !== ""
                        Layout.fillWidth: true
                        text: overlay.formError
                        color: "#fca5a5"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }

            // TAB 1: DISPLAY, FEEDS & CACHE.
            ColumnLayout {
                visible: overlay.currentTab === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14

                Text {
                    text: qsTr("GLOBE RENDERING")
                    color: "#80ffffff"
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.4
                }

                // 3D Terrain toggle.
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("3D Terrain Elevation"); color: theme.foreground; font.pixelSize: 13 }
                        Text { text: qsTr("Displace surface using public AWS Terrarium DEM tiles (T)"); color: "#80ffffff"; font.pixelSize: 11 }
                    }
                    Switch {
                        checked: backend.terrainEnabled
                        onToggled: backend.terrainEnabled = checked
                    }
                }

                // Terrain exaggeration slider.
                RowLayout {
                    Layout.fillWidth: true
                    enabled: backend.terrainEnabled
                    opacity: backend.terrainEnabled ? 1.0 : 0.45
                    spacing: 12

                    Text {
                        text: qsTr("Terrain Exaggeration")
                        color: theme.foreground
                        font.pixelSize: 12
                        Layout.preferredWidth: 160
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0.5
                        to: 3.0
                        stepSize: 0.1
                        value: backend.terrainExaggeration
                        onMoved: backend.terrainExaggeration = value
                    }

                    Text {
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: theme.accent
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                        text: {
                            var _l = translator.language
                            return backend.terrainExaggeration.toLocaleString(Qt.locale(), 'f', 1) + "×"
                        }
                    }
                }

                // Graticule grid toggle.
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Latitude / Longitude Grid"); color: theme.foreground; font.pixelSize: 13 }
                        Text { text: qsTr("Draw 30° graticule meridians and parallels (G)"); color: "#80ffffff"; font.pixelSize: 11 }
                    }
                    Switch {
                        checked: backend.graticuleEnabled
                        onToggled: backend.graticuleEnabled = checked
                    }
                }

                // Atmosphere toggle.
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Atmospheric Rim Glow"); color: theme.foreground; font.pixelSize: 13 }
                        Text { text: qsTr("Fresnel limb halo subtly tinted to the active Omarchy accent"); color: "#80ffffff"; font.pixelSize: 11 }
                    }
                    Switch {
                        checked: backend.atmosphereEnabled
                        onToggled: backend.atmosphereEnabled = checked
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#22ffffff" }

                Text {
                    text: qsTr("KML FEEDS")
                    color: "#80ffffff"
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.4
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Auto-Refresh Live Feeds"); color: theme.foreground; font.pixelSize: 13 }
                        Text { text: qsTr("Poll enabled remote KML/KMZ feeds in the background"); color: "#80ffffff"; font.pixelSize: 11 }
                    }

                    Row {
                        spacing: 4
                        enabled: backend.autoRefreshEnabled
                        opacity: backend.autoRefreshEnabled ? 1.0 : 0.4

                        Repeater {
                            model: [1, 5, 15, 30]
                            Rectangle {
                                width: 38
                                height: 24
                                radius: Math.min(4, theme.radius)
                                color: backend.autoRefreshMinutes === modelData ? theme.accent : "#16ffffff"
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData + "m"
                                    color: backend.autoRefreshMinutes === modelData ? theme.accentForeground : theme.foreground
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.autoRefreshMinutes = modelData
                                }
                            }
                        }
                    }

                    Switch {
                        checked: backend.autoRefreshEnabled
                        onToggled: backend.autoRefreshEnabled = checked
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#22ffffff" }

                Text {
                    text: qsTr("DISK CACHE")
                    color: "#80ffffff"
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.4
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Cached Map & Elevation Tiles"); color: theme.foreground; font.pixelSize: 13 }
                        Text {
                            text: backend.cacheSummary
                            color: theme.accent
                            font.family: "monospace"
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        Layout.preferredHeight: 30
                        Layout.preferredWidth: clearTxt.implicitWidth + 22
                        radius: Math.min(4, theme.radius)
                        color: "#22ef4444"
                        border.width: 1
                        border.color: "#66ef4444"

                        Text {
                            id: clearTxt
                            anchors.centerIn: parent
                            text: qsTr("Clear Tile Cache")
                            color: "#fecaca"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.clearTileCache()
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
