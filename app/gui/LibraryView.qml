// LibraryView is the shell of the separate Seamless Windows page: the left
// rail, the content stack and the overlay slots of plan 11.2. The page is
// additive and entered explicitly from the toolbar; the ruling that scopes
// it is plan 6.0, and the old pages keep their own code path untouched.
//
// The page carries no sample data: the Devices section reads the real
// ComputerModel, the apps grid reads the real AppModel of the picked host
// (the same model AppView uses, with the same launch path through
// StreamSegue), and the running-windows list reads the real SeamlessModel
// table through SeamlessWindowsModel. ComputerManager is optional at
// construction (the typeof guard lets the page render before the manager
// exists); the guard is also what keeps the page loadable outside the app.

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import ComputerModel 1.0
import AppModel 1.0
import SeamlessWindowsModel 1.0

Item {
    id: root

    // The host the page is bound to, received at push time (a per-host
    // destination, not a global page)
    property int hostIndex: -1
    property string hostName: ""
    property string errorText: ""

    // The real devices (ComputerModel) and the real app list of the picked
    // host (AppModel) - the same models AppView uses
    ComputerModel {
        id: computerModel
    }

    AppModel {
        id: appsModel

        onComputerLost: root.errorText = qsTr("The host is no longer reachable.")
    }

    // The real applied window table of the seamless session (build's model)
    SeamlessWindowsModel {
        id: windowsModel
    }

    function selectHost(index, name) {
        root.computerIndex = index
        root.hostName = name
        browser.hostLabel = name
        appsModel.initialize(ComputerManager, index, true)
        console.log("[seamless-page] bind: ComputerManager=" + (typeof ComputerManager)
                    + " computers=" + computerVisual.items.count
                    + " host=" + name
                    + " index=" + index
                    + " apps=" + appsVisual.items.count)
    }

    function launchApp(appIndex, appName, appId) {
        var runningId = appsModel.getRunningAppId()
        if (runningId !== 0 && runningId !== appId) {
            showNotice(qsTr("Quit %1 before launching %2.")
                       .arg(appsModel.getRunningAppName()).arg(appName))
            return
        }
        var component = Qt.createComponent("StreamSegue.qml")
        if (component.status === Component.Error) {
            showNotice(component.errorString())
            return
        }
        // The running-windows list's boxart fallback is the launched app's
        var info = appsModel.getApp(appIndex)
        browser.launchedBoxart = info.boxart
        var segue = component.createObject(root.StackView.view, {
            "appName": appName,
            "session": appsModel.createSessionForApp(appIndex),
            "isResume": runningId === appId
        })
        root.StackView.view.push(segue)
    }

    function showNotice(message) {
        notice.showToast(message)
        noticeTimer.restart()
    }

    // The rail rows rebuild from the real models
    Component.onCompleted: {
        // The host arrives at push time; the page binds on construction.
        // A page constructed without a host pops back to the device list
        // with one log line (it never renders an empty host state)
        if (hostIndex >= 0 && typeof ComputerManager !== "undefined") {
            selectHost(hostIndex, hostName)
        } else {
            console.log("[seamless-page] constructed without a host - popping back to the device list")
            root.StackView.view.pop()
        }
    }



    Connections {
        target: root.appsModel
        function onRowsInserted(parent, first, last) { root.selectHost(root.computerIndex, root.hostName) }
        function onRowsRemoved(parent, first, last) { root.selectHost(root.computerIndex, root.hostName) }
        function onModelReset() { root.selectHost(root.computerIndex, root.hostName) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AppSidebar {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            computerModel: computerModel
            appsModel: appsModel
            windowsModel: windowsModel
            selectedHost: root.hostName

            onDevicePicked: root.selectHost(index, name)
        }

        AppBrowser {
            id: browser
            Layout.fillWidth: true
            Layout.fillHeight: true
            hostLabel: root.hostName
            hostBound: root.computerIndex >= 0
            appsModel: appsModel
            windowsModel: windowsModel
            launchedBoxart: root.launchedBoxart
            errorText: root.errorText

            onRefreshRequested: root.selectHost(root.computerIndex, root.hostName)
            onAppRequested: root.launchApp(appIndex, appName, appId)
        }
    }

    // Notices for actions that need the session (lane 2)
    Toast {
        id: notice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        z: 100
        opacity: 0
    }

    Timer {
        id: noticeTimer
        interval: 3000
        onTriggered: notice.opacity = 0
    }

    function showToast(message) {
        notice.showToast(message)
        noticeTimer.restart()
    }
}
