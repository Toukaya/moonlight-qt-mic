// LibraryView is the shell of the separate Seamless Windows page: the left
// rail, the content stack and the overlay slots of plan 11.2. The page is
// additive and entered explicitly from the toolbar; the ruling that scopes it
// is plan 6.0, and the old pages keep their own code path untouched.
//
// STUB MODEL: fixed sample rows so the page is reviewable before the window
// table exists (plan 11.4). The models of plan 11.3 replace this stub. The
// actions that need the window table show a stub notice where they fire; no
// protocol wiring is present.

import QtQuick 2.9
import QtQuick.Layouts 1.3

Item {
    id: root

    // STUB: fixed sample rows; the real app model replaces this (plan 11.4).
    // Three rows carry distinct art and the rest the app's own no-boxart
    // placeholder, so the captures demonstrate both faces: an app WITH an
    // icon shows it (no placeholder), an app without one shows the
    // deliberate placeholder face
    ListModel {
        id: stubApps
        ListElement { name: "Notes"; running: false; busy: false; boxart: "qrc:/res/no_app_image.png" }
        ListElement { name: "Files"; running: false; busy: false; boxart: "qrc:/res/moonlight.svg" }
        ListElement { name: "Mail"; running: true; busy: false; boxart: "qrc:/res/no_app_image.png" }
        ListElement { name: "Photos"; running: false; busy: false; boxart: "qrc:/res/no_app_image.png" }
        ListElement { name: "Terminal"; running: true; busy: false; boxart: "qrc:/res/ic_videogame_asset_white_48px.svg" }
        ListElement { name: "IDE"; running: false; busy: true; boxart: "qrc:/res/no_app_image.png" }
        ListElement { name: "Music"; running: false; busy: false; boxart: "qrc:/res/no_app_image.png" }
        ListElement { name: "Reader"; running: false; busy: false; boxart: "qrc:/res/no_app_image.png" }
    }

    // STUB: swap point for the real app model (plan 11.4)
    property var appModel: stubApps

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AppSidebar {
            Layout.preferredWidth: 260
            Layout.fillHeight: true

            onDevicePicked: {
                browser.hostLabel = name
                browser.refilter()
            }
        }

        AppBrowser {
            id: browser
            Layout.fillWidth: true
            Layout.fillHeight: true
            hostLabel: qsTr("Studio PC")
            appsModel: root.appModel

            onRefreshRequested: browser.refilter()
            onOpenRequested: root.showStubNotice(qsTr("Stub: launching arrives with the window table"))
            onCloseRequested: root.showStubNotice(qsTr("Stub: closing arrives with the window table"))
            onPinRequested: root.showStubNotice(qsTr("Stub: pinning arrives with the window table"))
        }
    }

    // Stub marker for the actions that need the window table (plan 11.4)
    Toast {
        id: stubToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        z: 100
        opacity: 0
    }

    Timer {
        id: stubToastTimer
        interval: 3000
        onTriggered: stubToast.opacity = 0
    }

    function showStubNotice(message) {
        stubToast.showToast(message)
        stubToastTimer.restart()
    }
}
