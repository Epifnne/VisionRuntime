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
	property var valueLabels: ({})

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

	label: Label {
		width: Math.max(0, root.availableWidth)
		text: root.title
		elide: Text.ElideRight
	}

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
		anchors.fill: parent
		text: root.valueLabels[root.displayValue] !== undefined
			? root.valueLabels[root.displayValue] : root.displayValue
		horizontalAlignment: Text.AlignHCenter
		verticalAlignment: Text.AlignVCenter
		font.pixelSize: 24
		minimumPixelSize: 14
		fontSizeMode: Text.Fit
		elide: Text.ElideRight
		clip: true
		font.bold: true
		HoverHandler { id: valueHover }
		ToolTip.visible: valueHover.hovered
		ToolTip.text: root.displayValue
	}
}
