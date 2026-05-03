// Ported from wjbeckett/artemis (GPL). ServerCommandManager menu items
// removed — logabell does not implement that subsystem.
import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

Rectangle {
    id: quickMenu
    width: 500
    height: 400
    color: "#2d2d2d"
    radius: 10
    border.color: "#444"
    border.width: 1
    visible: true
    opacity: 1.0

    property string toastMessage: ""
    property bool showToast: false

    Behavior on opacity {
        NumberAnimation { duration: 200 }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            closeMenu()
        } else if (event.key === Qt.Key_Up) {
            menuListView.decrementCurrentIndex()
        } else if (event.key === Qt.Key_Down) {
            menuListView.incrementCurrentIndex()
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            executeCurrentItem()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        Text {
            text: qsTr("Quick Menu")
            font.pointSize: 24
            font.bold: true
            color: "#00cccc"
            Layout.alignment: Qt.AlignHCenter
        }

        ListView {
            id: menuListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true

            model: mainMenuModel

            delegate: Button {
                width: menuListView.width
                height: 60
                flat: true

                background: Rectangle {
                    color: parent.down ? "#333" : (parent.hovered ? "#444" : "transparent")
                    border.color: parent.hovered ? "#00cccc" : "transparent"
                    border.width: 2
                    radius: 5
                }

                onClicked: {
                    executeAction(model.action)
                }

                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 15
                    spacing: 15

                    Text {
                        text: model.icon
                        font.pointSize: 20
                        color: "white"
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                        Layout.preferredWidth: 40
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        Text {
                            text: model.text
                            font.pointSize: 14
                            font.bold: true
                            color: "white"
                        }

                        Text {
                            text: model.description
                            font.pointSize: 10
                            color: "#cccccc"
                        }
                    }
                }
            }
        }

        Button {
            text: qsTr("Close (Esc)")
            Layout.alignment: Qt.AlignHCenter
            onClicked: closeMenu()
        }
    }

    Rectangle {
        id: toastNotification
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 40, toastText.implicitWidth + 20)
        height: 40
        radius: 20
        color: "#333"
        border.color: "#666"
        border.width: 1
        visible: showToast
        opacity: showToast ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }

        Text {
            id: toastText
            text: toastMessage
            color: "white"
            font.pointSize: 12
            anchors.centerIn: parent
        }
    }

    Timer {
        id: toastTimer
        interval: 2000
        onTriggered: { showToast = false }
    }

    Timer {
        id: closeTimer
        interval: 1000
        onTriggered: { closeMenu() }
    }

    ListModel {
        id: mainMenuModel
        ListElement {
            text: qsTr("Disconnect")
            icon: "⏹"
            action: "disconnect"
            description: qsTr("Disconnect from server")
        }
        ListElement {
            text: qsTr("Quit")
            icon: "❌"
            action: "quit"
            description: qsTr("Quit streaming session")
        }
        ListElement {
            text: qsTr("Clipboard Upload")
            icon: "📋"
            action: "clipboard_upload"
            description: qsTr("Upload clipboard to server")
        }
        ListElement {
            text: qsTr("Fetch Clipboard")
            icon: "📥"
            action: "clipboard_fetch"
            description: qsTr("Fetch clipboard from server")
        }
        ListElement {
            text: qsTr("Toggle Performance Stats")
            icon: "📊"
            action: "toggle_stats"
            description: qsTr("Show/hide performance statistics")
        }
        ListElement {
            text: qsTr("Toggle Mouse Capture")
            icon: "🖱"
            action: "toggle_mouse"
            description: qsTr("Toggle mouse capture mode")
        }
        ListElement {
            text: qsTr("Toggle Keyboard Capture")
            icon: "⌨"
            action: "toggle_keyboard"
            description: qsTr("Toggle keyboard capture mode")
        }
        ListElement {
            text: qsTr("Toggle Fullscreen")
            icon: "🖥"
            action: "toggle_fullscreen"
            description: qsTr("Toggle fullscreen mode")
        }
    }

    function closeMenuDelayed() {
        closeTimer.restart();
    }

    function closeMenu() {
        if (typeof quickMenuManager !== 'undefined') {
            showActionFeedback("Closing menu...")
            quickMenuManager.hide();
        }
    }

    function executeCurrentItem() {
        var currentItem = menuListView.model.get(menuListView.currentIndex)
        if (currentItem) {
            executeAction(currentItem.action)
        }
    }

    function executeAction(action) {
        showActionFeedback(action)
        if (typeof quickMenuManager !== 'undefined') {
            quickMenuManager.executeAction(action)
        }
        closeMenuDelayed();
    }

    function showActionFeedback(action) {
        var message = ""
        switch(action) {
            case "disconnect":     message = "Disconnecting from server..."; break
            case "quit":           message = "Quitting session..."; break
            case "clipboard_upload": message = "Uploading clipboard to server..."; break
            case "clipboard_fetch":  message = "Fetching clipboard from server..."; break
            case "toggle_stats":   message = "Toggling performance stats..."; break
            case "toggle_mouse":   message = "Toggling mouse capture..."; break
            case "toggle_keyboard": message = "Toggling keyboard capture..."; break
            case "toggle_fullscreen": message = "Toggling fullscreen..."; break
            default:               message = action
        }

        if (message) {
            toastMessage = message
            showToast = true
            toastTimer.restart()
        }
    }

    Component.onCompleted: {
        focus = true
    }
}
