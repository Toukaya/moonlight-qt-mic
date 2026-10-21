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

    // The host whose apps the page browses: index into ComputerManager's
    // computer list, set by picking a device in the rail
    property int computerIndex: -1
    property string hostName: ""

    // Pushed with the host bound (the per-host destination): the entry passes
    // these, and construction binds immediately - the page never asks a global
    // question and never renders the "no apps" state without a host.
    property int hostIndex: -1
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
                    + " selected=" + index
                    + " apps=" + appsVisual.items.count)
    }

    function onComputerLost() {
        root.errorText = qsTr("The host is no longer reachable.")
    }

    // The launch path of the existing app grid (AppView.qml:206-231): the
    // same running-app guard, the same StreamSegue with a session from
    // AppModel.createSessionForApp, pushed on the same StackView
    function launchApp(appIndex, appName, appId) {
        // The seamless page launches a second app instead of refusing it: the host
        // appends the new app's process root to the session's launched set, so the
        // first app keeps running and both window sets coexist. The classic guard
        // (AppView.qml:207-221) is untouched - that is the ordinary stream path.
        var runningId = appsModel.getRunningAppId()
        if (runningId === appId) {
            showNotice(qsTr("%1 is already open.").arg(appName))
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
            "session": appsModel.createSessionForApp(appIndex)
        })
        root.StackView.view.push(segue)
    }

    function showNotice(message) {
        notice.showToast(message)
        noticeTimer.restart()
    }

    // The rail rows rebuild from the real models, and the host comes from the
    // push: this page is a per-host destination, so construction binds the host
    // it was pushed with and nothing else has to guess.
    Component.onCompleted: {
        if (hostIndex < 0) {
            // Constructed without a host: leave for the device list instead of
            // pretending the host published nothing.
            console.log("[seamless-page] pop: no host bound at construction")
            if (typeof stackView !== "undefined" && stackView) {
                stackView.pop()
            }
            return
        }
        if (typeof ComputerManager !== "undefined") {
            computerModel.initialize(ComputerManager)
        }
        root.selectHost(hostIndex, root.hostName)
        console.log("[seamless-page] bind: hostIndex=" + hostIndex +
                    " host=" + root.hostName + " apps=" + appsModel.count)
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
