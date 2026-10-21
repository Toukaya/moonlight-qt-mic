// AppTile is one app in the browser grid of the Seamless Windows page. It
// copies the anatomy of the existing app grid delegate (AppView.qml:74):
// a 220x287 delegate on the 230x297 cell, the 200x267 icon area anchored to
// the top, the label under it - with three recorded departures (label
// under instead of AppView's overlay, pointSize 20 with elide instead of
// 22 with wrap, PreserveAspectFit instead of stretch; see the PR). The
// numbers are DERIVED, not hardcoded: the delegate insets 5 per side from
// the cell, the icon area insets 10 per side from the delegate. Focus
// behaviour comes from NavigableItemDelegate, so keyboard and gamepad
// navigation reuse the existing logic instead of new focus code. The icon
// area binds the app-list boxart role; the icon source order lives in
// IconImage.qml.

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

NavigableItemDelegate {
    id: root

    property GridView grid
    property string appName: ""
    property bool appRunning: false
    property bool appBusy: false
    property string boxartSource: ""

    signal appRequested(int appIndex, string appName, string appId)

    readonly property int padOuter: 10
    readonly property int labelGap: 4
    // The acceptance ladder, composed from the icon box: the delegate
    // insets 5 per side from the cell, the icon area insets 10 per side
    // from the delegate, and the label block sits under the icon - so
    // 230x297 cell -> 220x287 delegate -> 200x267 icon with the label
    // fully inside
    readonly property int cellInset: 5
    readonly property int iconInset: 10
    readonly property int labelBlock: 26

    width: grid ? grid.cellWidth : root.iconWidth + 2 * iconInset
    height: grid ? grid.cellHeight
                 : root.iconHeight + 2 * iconInset + labelBlock

    // The icon area of the existing delegate, anchored to the top, with
    // the label block reserved under it so the label never clips. The
    // source size derives from the screen scale, so the placeholder asset
    // decodes crisp on 2x displays
    Image {
        id: appIcon
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.iconInset
        width: root.width - 2 * root.iconInset
        height: root.height - 2 * root.iconInset - root.labelBlock
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.round(width * Screen.devicePixelRatio)
        sourceSize.height: Math.round(height * Screen.devicePixelRatio)
        source: root.boxartSource

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
        y: appIcon.y + appIcon.height + root.labelGap
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
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
