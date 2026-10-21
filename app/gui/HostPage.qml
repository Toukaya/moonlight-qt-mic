/**
 * @file app/gui/HostPage.qml
 * @brief The second-level page of one connected host: exactly two tabs.
 * @details The interface is the operator's ruling: the device list opens a host
 *          page, and that page carries a tab bar with exactly two tabs -
 *          "Desktop mode", which is the existing app grid and launch path
 *          unchanged, and "Seamless Windows", which shows the host's configured
 *          apps and the live window table together. There is no third tab and no
 *          separate entry: the tab IS the entry. The presentation-mode gate fires
 *          only from the Seamless tab's launch path, so a Desktop-tab launch is
 *          exactly the path it always was.
 */
import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Item {
    id: root

    // Filled by the caller: the host this page belongs to.
    property int computerIndex: -1
    property string hostName: ""
    property bool showHiddenGames: false

    // Kept for the classic route's own property name.
    property string objectName: hostName

    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        spacing: 0

        background: Rectangle { color: "#303030" }

        TabButton {
            id: desktopTab
            text: qsTr("Desktop mode")
            font.pointSize: 13
            height: tabBar.height
            width: Math.max(160, tabBar.width / 2)
            contentItem: Label {
                text: desktopTab.text
                font: desktopTab.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: desktopTab.checked ? "#cccccc" : "#aaaaaa"
            }
            background: Rectangle {
                color: desktopTab.checked ? "#424242" : "transparent"
                Rectangle {
                    visible: desktopTab.checked
                    height: 3
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: "#45c486"
                }
            }
        }

        TabButton {
            id: seamlessTab
            text: qsTr("Seamless Windows")
            font.pointSize: 13
            height: tabBar.height
            width: Math.max(160, tabBar.width / 2)
            contentItem: Label {
                text: seamlessTab.text
                font: seamlessTab.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: seamlessTab.checked ? "#cccccc" : "#aaaaaa"
            }
            background: Rectangle {
                color: seamlessTab.checked ? "#424242" : "transparent"
                Rectangle {
                    visible: seamlessTab.checked
                    height: 3
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: "#00cccc"
                }
            }
        }
    }

    StackLayout {
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: tabBar.currentIndex

        // Tab 1: Desktop mode - the existing app grid and launch path, untouched.
        Loader {
            id: desktopLoader
            source: "AppView.qml"
            onLoaded: {
                item.computerIndex = root.computerIndex
                item.objectName = root.hostName
                if (item.showHiddenGames !== undefined) {
                    item.showHiddenGames = root.showHiddenGames
                }
            }
        }

        // Tab 2: Seamless Windows - the host's configured apps and the live
        // window table in one page. Its launches are the only ones that set the
        // presentation mode.
        Loader {
            id: seamlessLoader
            source: "LibraryView.qml"
            onLoaded: {
                item.hostIndex = root.computerIndex
                item.hostName = root.hostName
            }
        }
    }
}
