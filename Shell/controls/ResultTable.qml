import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Result table bound to a State endpoint. Parses the JSON payload and shows
// key/value rows; arrays and nested values are rendered as JSON text.
GroupBox {
	id: root

	property string endpoint: ""
	property string payload: ""
	property var rows: {
		if (!root.payload) {
			return [];
		}
		try {
			var obj = JSON.parse(root.payload);
			var result = [];
			if (Array.isArray(obj)) {
				for (var i = 0; i < obj.length; ++i) {
					result.push({
						"key": "[" + i + "]",
						"value": typeof obj[i] === "object"
							? JSON.stringify(obj[i]) : String(obj[i])
					});
				}
			} else {
				for (var key in obj) {
					var value = obj[key];
					result.push({
						"key": key,
						"value": typeof value === "object"
							? JSON.stringify(value) : String(value)
					});
				}
			}
			return result;
		} catch (e) {
			return [{ "key": "payload", "value": root.payload }];
		}
	}

	implicitWidth: 280
	implicitHeight: 160

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

	ListView {
		anchors.fill: parent
		clip: true
		model: root.rows

		delegate: RowLayout {
			width: ListView.view.width
			spacing: 8

			Label {
				Layout.preferredWidth: 140
				text: modelData.key
				color: "#888888"
				elide: Text.ElideRight
			}
			Label {
				Layout.fillWidth: true
				text: modelData.value
				font.family: "Consolas"
				elide: Text.ElideRight
			}
		}
	}
}
