// AppSidebar is the left rail of the Seamless Windows page (plan 11.2):
// one ListView whose rows rebuild from the real models - the Devices
// section from ComputerModel (every host the manager knows), the Apps
// section from the AppModel of the picked host. Rows are a fixed 28/36, the
// selection is one full-width rounded bar with the accent on the current
// row, names elide in the middle, and ListView keeps the keyboard.
//
// Note: the plan's Favorites section is not in this build - ComputerModel
// exposes no favourite role, so there is nothing real to show there yet;
// the section arrives with the favourite wiring.
//
// Row access to the C++ models goes through DelegateModel.items, the way
// QML reads QAbstractListModel rows from JavaScript.

import QtQuick 2.9
import QtQuick.Controls 2.2

Rectangle {
    id: root

    color: "#2d2d2d"

    // The host whose apps the content area browses
    property string selectedHost: ""
    property var computerModel
    property var appsModel

    signal devicePicked(int index, string name)

    ListModel {
        id: railModel
    }

    // Read access to the C++ model rows for the rebuild
    DelegateModel {
        id: computerVisual
        model: root.computerModel
    }

    DelegateModel {
        id: appsVisual
        model: root.appsModel
    }

    function rebuild() {
        railModel.clear()
        if (root.computerModel) {
            railModel.append({ kind: "header", title: qsTr("Devices"),
                               count: String(computerVisual.items.count), running: false,
                               pickable: false, index: -1 })
            for (var i = 0; i < computerVisual.items.count; i++) {
                var computer = computerVisual.items.get(i).model
                railModel.append({ kind: "item", title: computer.name,
                                   count: "", running: computer.online,
                                   pickable: true, index: i })
            }
        }
        if (root.appsModel && root.appsModel.count > 0) {
            railModel.append({ kind: "header", title: qsTr("Apps"),
                               count: String(appsVisual.items.count) + " " + qsTr("open"),
                               running: false, pickable: false, index: -1 })
            for (var j = 0; j < appsVisual.items.count; j++) {
                var app = appsVisual.items.get(j).model
                railModel.append({ kind: "item", title: app.name,
                                   count: "", running: app.running,
                                   pickable: false, index: j })
            }
        }
    }

    onComputerModelChanged: rebuild()
    onAppsModelChanged: rebuild()

    Connections {
        target: root.computerModel
        onRowsInserted: rail.rebuild()
        onRowsRemoved: rail.rebuild()
        onModelReset: rail.rebuild()
    }

    Connections {
        target: root.appsModel
        onRowsInserted: rail.rebuild()
        onRowsRemoved: rail.rebuild()
        onModelReset: rail.rebuild()
    }

    ListView {
        id: rail
        model: railModel
        anchors.fill: parent
        clip: true
        focus: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: ItemDelegate {
            id: row
            width: ListView.view.width
            height: row.kind === "header" ? 28 : 36
            padding: 0
            highlighted: ListView.isCurrentItem

            background: Rectangle {
                anchors.fill: parent
                radius: 6
                color: row.highlighted ? "#424242" : (row.hovered ? "#424242" : "transparent")
                Rectangle {
                    visible: row.highlighted
                    width: 3; height: parent.height - 12; radius: 2
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    color: "#45c486"
                }
            }

            contentItem: Item {
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 14
                    spacing: 8
                    Rectangle {
                        visible: row.kind === "item" && model.running
                        width: 6; height: 6; radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#45c486"
                    }
                    Label {
                        text: row.kind === "header" ? qsTr(model.title) : model.title
                        font.pointSize: row.kind === "header" ? 11 : 13
                        color: row.kind === "header" ? "#aaaaaa"
                               : (row.highlighted ? "#FFFFFF" : "#cccccc")
                        elide: Text.ElideMiddle
                        width: rail.width - 70
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Label {
                    visible: row.kind === "header"
                    text: model.count
                    color: "#aaaaaa"; font.pointSize: 11
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            onClicked: {
                rail.currentIndex = index
                if (model.pickable) {
                    root.selectedHost = model.title
                    root.devicePicked(model.index, model.title)
                }
            }
        }

        Keys.onReturnPressed: {
            if (currentIndex >= 0) {
                itemAtIndex(currentIndex).clicked()
            }
        }
    }
}
