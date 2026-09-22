import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Endpoint universe tree (from a loaded manifest or derived from the
// profile). Drag an endpoint onto a leaf control on the canvas to bind it.
ColumnLayout {
	spacing: 0

	Label {
		Layout.fillWidth: true
		Layout.margins: 6
		visible: designerBackend.endpoints.length === 0
		text: "无端点。加载 manifest 或打开 profile 后自动推导。"
		color: "#888888"
		wrapMode: Text.Wrap
	}

	ListView {
		Layout.fillWidth: true
		Layout.fillHeight: true
		Layout.margins: 6
		spacing: 4
		clip: true
		model: designerBackend.endpoints

		delegate: Rectangle {
			id: entry
			required property var modelData
			width: ListView.view.width
			height: 44
			radius: 4
			color: "#2c3540"
			border.color: {
				switch (modelData.kind) {
				case "parameter": return "#8e6bbf";
				case "command": return "#4f9cf9";
				case "state": return "#4caf7d";
				case "stream": return "#e0a040";
				default: return "#55606c";
				}
			}

			Drag.active: dragArea.drag.active
			Drag.hotSpot.x: width / 2
			Drag.hotSpot.y: height / 2
			Drag.mimeData: ({
				"application/x-visiondesigner-endpoint": modelData.name
			})

			Column {
				anchors.fill: parent
				anchors.margins: 6
				spacing: 2
				RowLayout {
					width: parent.width
					Label {
						text: modelData.kind || "?"
						color: "#9ecfff"
						font.pixelSize: 10
					}
					Label {
						Layout.fillWidth: true
						text: modelData.name || ""
						color: "#e0e0e0"
						elide: Text.ElideRight
					}
				}
				Label {
					width: parent.width
					text: modelData.description || ""
					color: "#7f8c99"
					font.pixelSize: 10
					elide: Text.ElideRight
				}
			}

			MouseArea {
				id: dragArea
				anchors.fill: parent
				drag.target: entry
				onReleased: entry.Drag.drop()
			}
		}
	}
}
