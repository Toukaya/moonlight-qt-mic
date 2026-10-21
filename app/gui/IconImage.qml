// IconImage decides its source in the operator's order (DOD 8): the
// window icon by hash first (the 0x56 SEAMLESS_MSG_ICON message lands here
// as iconSource once the uplink is wired), else the app-list boxart role
// (the same source AppView already binds), and the letter tile covers only
// while an image loads - never as the steady-state face.
// The Qt image cache keeps decoded images once per source, so scrolling
// does not re-decode; the per-hash pixmap cache of plan 11.7 lands with the
// icon provider.

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

Item {
    id: root

    // Window icon pixels keyed by hash; empty until the table is wired
    property string iconSource: ""
    // The app list boxart role (AppView.qml: model.boxart)
    property string boxartSource: ""
    // App title; names the letter tile shown while an image loads
    property string title: ""
    // The deliberate no-icon face: the app's own placeholder asset
    property string placeholderSource: "qrc:/res/no_app_image.png"

    // Latches a failed source so the source binding cannot oscillate
    // between the broken URL and the placeholder. Cleared when a new
    // source is set: the next title or role may load fine.
    property bool sourceFailed: false

    onBoxartSourceChanged: sourceFailed = false
    onIconSourceChanged: sourceFailed = false

    Image {
        id: pix
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        // Decode at the screen scale so the placeholder stays crisp on 2x
        sourceSize.width: Math.round(width * Screen.devicePixelRatio)
        sourceSize.height: Math.round(height * Screen.devicePixelRatio)
        // Source order (DOD 8): window icon by hash first, then the
        // boxart role; the letter tile covers only the Loading window
        source: root.iconSource !== "" ? root.iconSource
                                       : (root.boxartSource !== "" ? root.boxartSource
                                                                   : root.placeholderSource)
        onStatusChanged: {
            if (pix.status === Image.Error) {
                root.sourceFailed = true
            }
        }
    }

    // Letter tile: covers the Loading window only
    Rectangle {
        anchors.fill: parent
        visible: pix.status === Image.Loading
        radius: 8
        color: root.stableColor(root.title)

        Label {
            anchors.centerIn: parent
            text: root.title.length > 0 ? root.title.charAt(0).toUpperCase() : "?"
            font.pointSize: 22
            font.bold: true
            color: "#cccccc"
        }
    }

    function stableColor(text) {
        // The same title always tiles the same colour
        var hash = 0
        for (var i = 0; i < text.length; i++) {
            hash = (hash * 31 + text.charCodeAt(i)) % 360
        }
        return Qt.hsla(hash / 360.0, 0.42, 0.52, 1)
    }
}
