import QtQuick
import QtQuick.Controls
Rectangle {
    function refreshCaption() {
        tabs.leftPadding = Qt.binding(function() { return tabs.width < 780 ? 6 : 0; });
        for (let button of commands.children) {
            button.visible = Qt.binding(function() { return tabs.width > 300; });
            button.enabled = Qt.binding(function() { return tabs.width > 300; });
        }
    }
    width: 820; height: 500; color: "#f6f5f3"
    TabBar {
        id: tabs; objectName: "applicationTabs"
        anchors.top: parent.top; width: parent.width; height: 40
        leftPadding: width < 780 ? 6 : 0; rightPadding: 120; spacing: 2
        background: Rectangle { color: "#e1e3e8" }
        TabButton { text: "Home"; width: 140 }
        TabButton { text: "Research"; width: 140 }
        TabButton { text: "Downloads"; width: 140 }
    }
    Row {
        id: commands
        anchors.right: parent.right; height: 40
        Button { objectName: "minimizeButton"; visible: tabs.width > 300; text: "−"; width: 40; height: 40; onClicked: Window.window.showMinimized() }
        Button { objectName: "maximizeButton"; visible: tabs.width > 300; text: "□"; width: 40; height: 40; onClicked: Window.window.visibility === Window.Maximized ? Window.window.showNormal() : Window.window.showMaximized() }
        Button { objectName: "closeButton"; visible: tabs.width > 300; text: "×"; width: 40; height: 40; onClicked: Window.window.close() }
    }
    TextField { id: address; objectName: "addressField"; anchors.top: tabs.bottom; width: parent.width; height: 44; text: "https://example.local/research" }
    Text { anchors.centerIn: parent; text: "Qt Quick / GPU\n\nOriginal application tab " + (tabs.currentIndex + 1) + "\n\nEditable address field and scene graph remain active"; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 18; color: "#30343b" }
    Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 18; height: 18; color: "#3f73d0" }
}
