// AppTile is one app in the browser grid of the Seamless Windows page. It
// copies the anatomy of the existing app grid delegate (AppView.qml:74):
// a 220x287 delegate on the 230x297 cell, the 200x267 icon area anchored to
// the top, the label under it. Focus behaviour comes from
// NavigableItemDelegate, so keyboard and gamepad navigation reuse the
// existing logic instead of new focus code.
//
// The icon source order is the operator's: window icon by hash (message
// 0x56, future wiring), else the app list boxart role, else the letter tile
// while loading only.
//
// STUB NOTE: Open and Close need the launch path and the window table
// (plan 11.4); the shell shows a stub notice where they fire. Pin belongs to
// the same future wiring.

import QtQuick 2.9
import QtQuick.Controls 2.2

NavigableItemDelegate {
    id: root

    property GridView grid
    property string appName: ""
    property bool appRunning: false
    property bool appBusy: false
    property string boxartSource: ""
    property string iconSource: ""

    signal openRequested(string name)
    signal closeRequested(string name)
    signal pinRequested(string name)

    readonly property int padOuter: 10
    readonly property int labelGap: 8

    width: grid ? grid.cellWidth - 2 * padOuter : 220
    height: grid ? grid.cellHeight - 10 : 287

    // The icon area of the existing delegate: 200x267, anchored to the top
    Image {
        id: appIcon
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.padOuter
        width: 200
        height: 267
        fillMode: Image.PreserveAspectFit
        source: root.boxartSource !== "" ? root.boxartSource : root.iconSource

        BusyIndicator {
            anchors.centerIn: parent
            visible: root.appBusy
            running: root.appBusy
        }
    }

    // Running badge: a filled dot at the top right of the icon area
    Rectangle {
        anchors.top: appIcon.top
        anchors.right: appIcon.right
        anchors.topMargin: 4
        anchors.rightMargin: 4
        width: 12
        height: 12
        radius: 6
        color: "#45c486"
        visible: root.appRunning
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: appIcon.bottom
        anchors.topMargin: root.labelGap
        width: parent.width - 2 * root.padOuter
        horizontalAlignment: Text.AlignHCenter
        elide: Label.ElideRight
        text: root.appName
        font.pointSize: 20
        color: "#cccccc"

        ToolTip.text: root.appName
        ToolTip.delay: 1000
        ToolTip.timeout: 5000
        ToolTip.visible: (parent.hovered || parent.highlighted) && truncated
    }

    NavigableMenu {
        id: tileMenu

        NavigableMenuItem {
            text: qsTr("Open")
            onTriggered: root.openRequested(root.appName)
        }
        NavigableMenuItem {
            text: qsTr("Pin")
            onTriggered: root.pinRequested(root.appName)
        }
        NavigableMenuItem {
            text: qsTr("Close")
            visible: root.appRunning
            onTriggered: root.closeRequested(root.appName)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: tileMenu.popup()
    }
}
