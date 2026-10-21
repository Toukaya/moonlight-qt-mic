// IconImage decides its source in this order (operator ruling 00:56Z:
// "boxart for entries that come from the existing app list, the icon
// message for the rest" - boxart when present):
//   1. the app list boxart role - the same source AppView already binds
//      (AppView.qml: model.boxart) - for every entry the app list carries;
//   2. the window icon by hash, for entries the app list does not know -
//      the 0x56 SEAMLESS_MSG_ICON message (uint32 hash, then PNG bytes,
//      once per hash) lands here as iconSource; future wiring, no protocol
//      yet;
//   3. the letter tile WHILE LOADING ONLY (Image.Loading) - never the
//      steady-state face: when no source exists, or a source failed to
//      decode, the tile shows the app's own no-boxart placeholder instead
//      (the deliberate, stated no-icon face per the refused-blob rule).
// The Qt image cache keeps decoded images once per source, so scrolling
// does not re-decode; the per-hash pixmap cache of plan 11.7 lands with the
// icon provider.

import QtQuick 2.9
import QtQuick.Controls 2.2

Item {
    id: root

    // Window icon pixels keyed by hash; empty until the table is wired
    property string iconSource: ""
    // The app list boxart role (AppView.qml: model.boxart)
    property string boxartSource: ""
    // App title, used by the loading-only letter tile
    property string title: ""
    // The deliberate no-icon face: the app's own placeholder asset
    property string placeholderSource: "qrc:/res/no_app_image.png"

    Image {
        id: pix
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        // Source order: boxart, then window icon; empty or failed sources
        // fall back to the placeholder, so the letter tile only ever covers
        // the Loading window
        source: {
            var picked = root.boxartSource !== "" ? root.boxartSource
                                                  : root.iconSource
            if (picked === "" || pix.status === Image.Error) {
                return root.placeholderSource
            }
            return picked
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
