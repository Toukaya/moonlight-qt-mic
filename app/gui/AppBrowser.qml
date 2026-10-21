// AppBrowser is the content area of the Seamless Windows page (plan 11.3):
// a header with the host label, the search field, the grid/list toggle and
// refresh; below it the tile grid or the one-line list; the empty and error
// states of plan 11.5; and a real footer status bar. The grid is a
// CenteredGridView with NavigableItemDelegate-based tiles, so keyboard and
// gamepad navigation reuse the existing logic instead of new focus code.
//
// The grid binds the real AppModel of the picked host - the same model the
// existing AppView uses (name / running / boxart / appid roles). The grid
// carries the app's own literals (AppView.qml:21): cell 230x297, and
// CenteredGridView centres whatever columns fit. The search field jumps the
// grid selection to the first matching app. Refresh asks the shell, which
// re-initialises the model from ComputerManager.

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Rectangle {
    id: root

    color: "transparent"
    clip: true

    // The padding ladder: one outer, one inner, and the footer height the
    // grid clears - every spacing in this file derives from these three
    readonly property int padOuter: 10
    readonly property int padInner: 5
    readonly property int footerHeight: 28
    readonly property int gridBottomClearance: footerHeight + 16

    property string hostLabel: ""
    property string errorText: ""
    property bool listView: false
    property bool showWindows: false
    // The real app list of the picked host (AppModel)
    property var appsModel
    // The real applied window table of the seamless session
    property var windowsModel
    // The boxart of the app the page launched - the running-windows list's
    // fallback face while the window icons are in flight
    property string launchedBoxart: ""

    signal refreshRequested()
    // A tile was activated: the shell launches through the app's own path
    signal appRequested(int appIndex, string appName, string appId)

    // The search field jumps the grid selection to the first app whose
    // name matches, so the control is live without copying the model
    function selectFirstMatch(needle) {
        if (!appsModel) {
            return
        }
        var text = needle.toLowerCase()
        for (var i = 0; i < appsModel.count; i++) {
            var app = appsModel.getApp(i)
            if (app.name.toLowerCase().indexOf(text) !== -1) {
                tileGrid.currentIndex = i
                return
            }
        }
        tileGrid.currentIndex = -1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Error banner (plan 11.5): inline, with a retry button
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: padInner
            radius: 6
            color: "#424242"
            visible: root.errorText !== ""

            RowLayout {
                anchors.fill: parent
                anchors.margins: padInner
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: root.errorText
                    font.pointSize: 12
                    color: "#cccccc"
                    wrapMode: Text.Wrap
                }

                Button {
                    text: qsTr("Retry")
                    onClicked: root.refreshRequested()
                }
            }
        }

        // Content top: host label, search, view toggle, refresh (plan 11.2)
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: padOuter
            spacing: 8

            Label {
                Layout.fillWidth: true
                // The title never reads "- Apps": with no host bound it says so
                // and points at the device list instead of claiming emptiness.
                text: root.hostLabel.length > 0
                      ? root.hostLabel + " - " + qsTr("Apps")
                      : qsTr("Select a device")
                font.pointSize: 22
                elide: Label.ElideRight
                color: "#cccccc"
            }

            TextField {
                id: searchField
                Layout.preferredWidth: 220
                placeholderText: qsTr("Search apps")
                font.pointSize: 12
                onTextChanged: root.selectFirstMatch(text)
            }

            NavigableToolButton {
                id: gridButton
                text: qsTr("Grid")
                font.pointSize: 12
                checkable: true
                checked: !root.listView && !root.showWindows
                onCheckedChanged: if (checked) { root.listView = false; root.showWindows = false }
                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Grid view")
                background: Rectangle {
                    radius: 4
                    color: gridButton.checked ? "#2d2d2d" : "#424242"
                    border.color: gridButton.checked ? "#00cccc" : "#4A4A4A"
                }
            }

            NavigableToolButton {
                id: listButton
                text: qsTr("List")
                font.pointSize: 12
                checkable: true
                checked: root.listView && !root.showWindows
                onCheckedChanged: if (checked) { root.listView = true; root.showWindows = false }
                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("List view")
                background: Rectangle {
                    radius: 4
                    color: listButton.checked ? "#2d2d2d" : "#424242"
                    border.color: listButton.checked ? "#00cccc" : "#4A4A4A"
                }
            }

            NavigableToolButton {
                iconSource: "qrc:/res/update.svg"
                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Refresh")
                onClicked: root.refreshRequested()
            }

            NavigableToolButton {
                text: qsTr("Windows")
                font.pointSize: 12
                checkable: true
                checked: root.showWindows
                onCheckedChanged: if (checked) { root.listView = false; root.showWindows = true }
                                 else root.showWindows = false
                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Running windows")
            }
        }

        // Content body, grid mode (plan 11.2). Fixed cell metrics from
        // AppView.qml:21; CenteredGridView centres what fits. The bottom
        // margin keeps the last row above the footer; the grid scrolls
        CenteredGridView {
            id: tileGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.listView && !root.showWindows
            cellWidth: 230
            cellHeight: 297
            topMargin: padInner
            bottomMargin: gridBottomClearance
            model: appsModel

            delegate: AppTile {
                grid: tileGrid
                appIndex: index
                appId: model.appId
                appName: model.name
                appRunning: model.running
                appBusy: model.busy
                boxartSource: model.boxart

                onAppRequested: root.appRequested(appIndex, appName, appId)
                onOpenRequested: root.appRequested(appIndex, appName, appId)
                onCloseRequested: appsModel.quitRunningApp()
            }
        }

        // Content body, list mode: one line per app (plan 11.7)
        ListView {
            id: listMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.listView && !root.showWindows
            model: appsModel
            spacing: 2
            bottomMargin: gridBottomClearance

            delegate: NavigableItemDelegate {
                width: listMode.width
                height: 44
                // List rows: no GridView behind them, so the base grid
                // bindings are overridden and the arrows drive the list's
                // own currentIndex; Return opens
                highlighted: false

                Keys.onUpPressed: listMode.decrementCurrentIndex()
                Keys.onDownPressed: listMode.incrementCurrentIndex()
                Keys.onLeftPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocus)
                Keys.onRightPressed: nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocus)
                Keys.onReturnPressed: root.appRequested(index, model.name, model.appid)
                Keys.onEnterPressed: root.appRequested(index, model.name, model.appid)

                onClicked: root.appRequested(index, model.name, model.appid)

                IconImage {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    title: model.name
                    boxartSource: model.boxart
                    width: 32
                    height: 32
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    text: model.name
                    font.pointSize: 12
                    elide: Label.ElideRight
                    color: "#cccccc"
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    width: 8
                    height: 8
                    radius: 4
                    color: "#45c486"
                    visible: model.running
                }
            }
        }

        // Content body, running windows: the real applied table rows
        // (SeamlessWindowsModel). Outside a session the table is empty
        // because the host publishes no windows. Icon order per DOD 8:
        // the model's icon role first, the launched app's boxart second
        ListView {
            id: windowsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showWindows
            model: windowsModel
            spacing: 2

            delegate: NavigableItemDelegate {
                width: windowsList.width
                height: 44
                highlighted: false

                Keys.onUpPressed: windowsList.decrementCurrentIndex()
                Keys.onDownPressed: windowsList.incrementCurrentIndex()

                IconImage {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    width: 32
                    height: 32
                    title: model.title
                    iconSource: model.icon
                    boxartSource: root.launchedBoxart
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    text: model.title
                    font.pointSize: 12
                    elide: Label.ElideRight
                    color: "#cccccc"
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: qsTr("window id %1").arg(model.id)
                    font.pointSize: 11
                    color: "#aaaaaa"
                }
            }

            Label {
                anchors.centerIn: parent
                visible: windowsModel && windowsModel.count === 0
                text: qsTr("No windows are open")
                font.pointSize: 20
                color: "#aaaaaa"
            }
        }

        // Content foot: a real footer bar - host left, count right
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: footerHeight
            color: "#2d2d2d"

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                text: root.hostLabel
                font.pointSize: 12
                color: "#aaaaaa"
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                text: tileGrid.count + " " + qsTr("apps")
                font.pointSize: 12
                color: "#aaaaaa"
            }
        }
    }

    // Empty state (plan 11.5): one line plus refresh. A root-level overlay:
    // it must not live inside the ColumnLayout, where anchors are undefined
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: tileGrid.count === 0 && root.errorText === ""
        z: 10

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("This host publishes no apps")
            font.pointSize: 20
            color: "#aaaaaa"
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Refresh")
            onClicked: root.refreshRequested()
        }
    }
}
