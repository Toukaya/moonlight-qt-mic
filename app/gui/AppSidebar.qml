// AppSidebar is the left rail of the Seamless Windows page (plan 11.2):
// one ListView with ItemDelegate rows - headers and items share one model
// and one rhythm, so heights cannot drift and the rail scrolls when the
// sections grow. Hover and press feedback come from the delegate; selection
// is one full-width rounded bar with the accent on the current row. Names
// elide in the middle, so "Work Laptop" stays readable when the rail
// narrows. ListView handles Up/Down itself; Enter forwards to the current
// item - no second focus system.
//
// STUB NOTE: the rows are fixed sample data; counts, markers and selection
// come from the models of plan 11.3 when they are wired. Picking a favorite
// or a device asks the shell to browse that host; app rows are
// display-only for now.

import QtQuick 2.9
import QtQuick.Controls 2.2

Rectangle {
    id: root

    color: "#2d2d2d"

    // The host whose apps the content area browses
    property string selectedHost: "Studio PC"

    signal devicePicked(string name)

    // STUB: fixed sample rows; kind is "header" or "item"
    ListModel {
        id: railModel
        ListElement { kind: "header"; title: QT_TR_NOOP("Favorites"); count: "2"; running: false; pickable: false }
        ListElement { kind: "item"; title: "Studio PC"; count: ""; running: false; pickable: true }
        ListElement { kind: "item"; title: "Work Laptop"; count: ""; running: false; pickable: true }
        ListElement { kind: "header"; title: QT_TR_NOOP("Devices"); count: "2"; running: false; pickable: false }
        ListElement { kind: "item"; title: "Studio PC"; count: ""; running: true; pickable: true }
        ListElement { kind: "item"; title: "Work Laptop"; count: ""; running: false; pickable: true }
        ListElement { kind: "header"; title: QT_TR_NOOP("Apps"); count: QT_TR_NOOP("3 open"); running: false; pickable: false }
        ListElement { kind: "item"; title: "Mail"; count: ""; running: true; pickable: false }
        ListElement { kind: "item"; title: "Files"; count: ""; running: false; pickable: false }
        ListElement { kind: "item"; title: "Terminal"; count: ""; running: true; pickable: false }
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
                        visible: row.kind === "item" && row.running
                        width: 6; height: 6; radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#45c486"
                    }
                    Label {
                        text: row.kind === "header" ? qsTr(row.title) : row.title
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
                    text: row.kind === "header" ? qsTr(row.count) : ""
                    color: "#aaaaaa"; font.pointSize: 11
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            onClicked: {
                rail.currentIndex = index
                if (row.pickable) {
                    root.selectedHost = row.title
                    root.devicePicked(row.title)
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
