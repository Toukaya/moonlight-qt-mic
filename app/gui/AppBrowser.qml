// AppBrowser is the content area of the Seamless Windows page (plan 11.3):
// a header with the host label, the search field, the grid/list toggle and
// refresh; below it the tile grid or the one-line list; the empty and error
// states of plan 11.5; and a real footer status bar. The grid is a
// CenteredGridView with NavigableItemDelegate-based tiles, so keyboard and
// gamepad navigation reuse the existing logic instead of new focus code.
// The column count derives from the content width (at least 3 columns), so
// tiles fit at the default 1280x600 window and when maximised.
//
// STUB NOTE: filtering copies rows into a local model because the real
// models are not wired yet (plan 11.4); a proper proxy replaces this when
// they land. Refresh re-filters locally; the real refresh asks the host.

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
    // The model to browse; a stub ListModel for now (plan 11.4)
    property var appsModel

    signal refreshRequested()
    signal openRequested(string name)
    signal closeRequested(string name)
    signal pinRequested(string name)

    // STUB: local filtered copy; replaced by a proxy with the real models
    ListModel {
        id: filteredApps
    }

    // The grid carries the app's own literals (AppView.qml:21): cell
    // 230x297, and CenteredGridView centres whatever columns fit - the
    // operator's enumerated metrics govern (r1 queued the confirm)

    onAppsModelChanged: refilter()

    function refilter() {
        filteredApps.clear()
        if (!appsModel) {
            return
        }
        var needle = searchField.text.toLowerCase()
        for (var i = 0; i < appsModel.count; i++) {
            var row = appsModel.get(i)
            if (needle.length === 0 || row.name.toLowerCase().indexOf(needle) !== -1) {
                filteredApps.append(row)
            }
        }
    }

    function runningCount() {
        var count = 0
        for (var i = 0; i < filteredApps.count; i++) {
            if (filteredApps.get(i).running) {
                count++
            }
        }
        return count
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
                text: root.hostLabel + " - " + qsTr("Apps")
                font.pointSize: 22
                elide: Label.ElideRight
                color: "#cccccc"
            }

            TextField {
                id: searchField
                Layout.preferredWidth: 220
                placeholderText: qsTr("Search apps")
                font.pointSize: 12
                onTextChanged: root.refilter()
            }

            NavigableToolButton {
                id: gridButton
                text: qsTr("Grid")
                font.pointSize: 12
                checkable: true
                checked: !root.listView
                onCheckedChanged: if (checked) root.listView = false
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
                checked: root.listView
                onCheckedChanged: if (checked) root.listView = true
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
        }

        // Content body, grid mode (plan 11.2). Fixed cell metrics from
        // AppView.qml:21; CenteredGridView centres what fits. The bottom
        // margin keeps the last row above the footer; the grid scrolls
        CenteredGridView {
            id: tileGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.listView
            cellWidth: 230
            cellHeight: 297
            topMargin: padInner
            bottomMargin: gridBottomClearance
            model: filteredApps

            delegate: AppTile {
                grid: tileGrid
                appName: name
                appRunning: running
                appBusy: busy
                boxartSource: boxart

                onOpenRequested: root.openRequested(appName)
                onCloseRequested: root.closeRequested(appName)
                onPinRequested: root.pinRequested(appName)
            }
        }

        // Content body, list mode: one line per app (plan 11.7)
        ListView {
            id: listMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.listView
            model: filteredApps
            spacing: 2
            bottomMargin: gridBottomClearance

            delegate: NavigableItemDelegate {
                width: listMode.width
                height: 44
                // The list is the navigation grid for these rows; ListView
                // shares the moveCurrentIndex* API the delegate expects.
                // Null-safe highlight: the base binding evaluates once
                // before grid binds.
                grid: listMode
                highlighted: grid ? grid.activeFocus && grid.currentItem === this : false

                onClicked: root.openRequested(name)

                IconImage {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    title: name
                    boxartSource: boxart
                    width: 32
                    height: 32
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    text: name
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
                    visible: running
                }
            }
        }

        // Empty state (plan 11.5): one line plus refresh
        Column {
            anchors.centerIn: parent
            spacing: 12
            visible: filteredApps.count === 0 && root.errorText === ""

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

        // Content foot: a real footer bar - status left, counts right
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#2d2d2d"

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                text: root.hostLabel + " - " + qsTr("stub: no transport yet")
                font.pointSize: 12
                color: "#aaaaaa"
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                text: filteredApps.count + " " + qsTr("apps") + " - "
                        + root.runningCount() + " " + qsTr("running")
                font.pointSize: 12
                color: "#aaaaaa"
            }
        }
    }
}
