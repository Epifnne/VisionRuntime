import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Big-number statistic bound to a State endpoint. Extracts `field` from the
// JSON payload; without a field it shows the raw payload.
GroupBox {
	id: root

	property string endpoint: ""
	property string field: ""
	property string payload: ""

	property string displayValue: {
		if (!root.payload) {
			return "-";
		}
		if (!root.field) {
			return root.payload;
		}
		try {
			var obj = JSON.parse(root.payload);
			var value = obj[root.field];
			if (value === undefined) {
				return "-";
			}
			return typeof value === "number" && !Number.isInteger(value)
				? value.toFixed(2) : String(value);
		} catch (e) {
			return "-";
		}
	}

	implicitWidth: 160

	function refresh() {
		var snapshot = serviceClient.stateSnapshot(root.endpoint);
		if (snapshot) {
			root.payload = snapshot;
		}
	}

	Component.onCompleted: refresh()

	Connections {
		target: serviceClient
		function onStateChanged(name, payload) {
			if (name === root.endpoint) {
				root.payload = payload;
			}
		}
	}

	Label {
		anchors.centerIn: parent
		text: root.displayValue
		font.pixelSize: 28
		font.bold: true
	}
}
