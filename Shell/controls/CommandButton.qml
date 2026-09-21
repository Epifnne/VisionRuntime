import QtQuick
import QtQuick.Controls

// Command button bound to a Command endpoint. Invokes the command through
// the binding layer; failures surface via serviceClient.lastError.
Button {
	id: root

	property string endpoint: ""
	property string label: ""

	text: root.label || root.endpoint
	implicitWidth: 120

	onClicked: serviceClient.invokeCommand(root.endpoint)
}
