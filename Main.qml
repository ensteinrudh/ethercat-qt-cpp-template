import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Ethercat Test")

    // Access EtherCAT controller exposed from C++
    property var ethercat: ethercatController

    GridLayout {
        anchors.fill: parent
        anchors.margins: 20
        columns: 2
        rowSpacing: 20
        columnSpacing: 20

        // Status indicator
        Rectangle {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            height: 40
            color: ethercat.connected ? "#8AFF8A" : "#FF8A8A"
            radius: 5

            Text {
                anchors.centerIn: parent
                text: ethercat.statusMessage
                font.pixelSize: 14
            }
        }

        // Position input
        Label {
            text: "Target Position:"
        }

        TextField {
            id: positionInput
            Layout.fillWidth: true
            placeholderText: "Enter position"
            validator: IntValidator {}
            enabled: ethercat.connected
        }

        // Velocity input
        Label {
            text: "Target Velocity:"
        }

        TextField {
            id: velocityInput
            Layout.fillWidth: true
            placeholderText: "Enter velocity"
            validator: IntValidator { bottom: 1 }
            enabled: ethercat.connected
        }

        // Send button
        Button {
            Layout.columnSpan: 2
            Layout.alignment: Qt.AlignHCenter
            text: "Send Command"
            enabled: ethercat.connected &&
                    positionInput.text.length > 0 &&
                    velocityInput.text.length > 0

            onClicked: {
                ethercat.moveToPosition(
                    parseInt(positionInput.text),
                    parseInt(velocityInput.text)
                )
            }
        }

        // Status display
        GroupBox {
            title: "Status"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                Label {
                    text: "Current Position: " + ethercat.actualPosition
                    font.pixelSize: 14
                }

                Label {
                    text: "Status Word: " + ethercat.statusWord
                    font.pixelSize: 14
                }

                Item { Layout.fillHeight: true } // Spacer
            }
        }
    }
}
