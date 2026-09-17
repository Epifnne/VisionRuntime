import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Parameter form bound to a Parameter endpoint. Renders an editor matching
// the endpoint's declared type and writes through the binding layer.
GroupBox {
	id: root

	property string endpoint: ""

	function descriptor() {
		var all = serviceClient.parameters;
		for (var i = 0; i < all.length; ++i) {
			if (all[i].name === root.endpoint) {
				return all[i];
			}
		}
		return null;
	}

	function currentValue() {
		var value = serviceClient.readParameter(root.endpoint);
		return value === undefined ? "" : value;
	}

	implicitWidth: 220

	ColumnLayout {
		anchors.fill: parent
		spacing: 4

		Label {
			Layout.fillWidth: true
			text: {
				var d = root.descriptor();
				return d ? d.description : "unknown parameter: " + root.endpoint;
			}
			wrapMode: Text.Wrap
			color: "#888888"
			font.pixelSize: 11
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: 8

			Loader {
				id: editorLoader
				Layout.fillWidth: true
				sourceComponent: {
					var d = root.descriptor();
					if (d && d.type === "boolean") {
						return boolEditor;
					}
					return textEditor;
				}
			}

			Button {
				text: "Apply"
				enabled: {
					var d = root.descriptor();
					return d && d.writable;
				}
				onClicked: {
					var d = root.descriptor();
					if (!d) {
						return;
					}
					if (d.type === "boolean") {
						serviceClient.writeParameter(
							root.endpoint, editorLoader.item.checked);
					} else if (d.type === "integer") {
						serviceClient.writeParameter(
							root.endpoint, parseInt(editorLoader.item.text));
					} else if (d.type === "decimal") {
						serviceClient.writeParameter(
							root.endpoint, parseFloat(editorLoader.item.text));
					} else {
						serviceClient.writeParameter(
							root.endpoint, editorLoader.item.text);
					}
				}
			}
		}
	}

	Component {
		id: textEditor
		TextField {
			property var range: {
				var d = root.descriptor();
				if (d && d.minimum !== undefined && d.maximum !== undefined) {
					return "[" + d.minimum + ", " + d.maximum + "]";
				}
				if (d && d.minimum !== undefined) {
					return ">= " + d.minimum;
				}
				return "";
			}
			placeholderText: range
			text: String(root.currentValue())
			selectByMouse: true
		}
	}

	Component {
		id: boolEditor
		CheckBox {
			checked: root.currentValue() === true
		}
	}
}
