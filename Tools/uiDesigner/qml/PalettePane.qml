import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Control palette: drag a chip onto a canvas container to insert a node,
// or click a chip to append it to the currently selected container.
//
// Each chip owns a child ghost used as the drag target, so the ghost moves
// in the chip's own coordinate system (matching how MouseArea drag works).
// The ListView is not clipped and the ghost is reparented to the window's
// contentItem while dragging, so it can follow the cursor onto the canvas
// and its scene rectangle stays consistent with the canvas DropAreas.
Item {
	ListView {
		anchors.fill: parent
		anchors.margins: 8
		spacing: 6
		// No clip: the drag ghost must be able to leave the list bounds.
		model: [
			{ "type": "row", "title": "行容器" },
			{ "type": "column", "title": "列容器" },
			{ "type": "imageView", "title": "图像视图" },
			{ "type": "stateCard", "title": "状态灯卡" },
			{ "type": "commandButton", "title": "命令按钮" },
			{ "type": "parameterForm", "title": "参数表单" },
			{ "type": "statNumber", "title": "统计数字" },
			{ "type": "resultTable", "title": "结果表格" }
		]

		delegate: Rectangle {
			id: chip
			required property var modelData
			width: ListView.view.width
			height: 48
			radius: 4
			color: "#2c3540"
			border.color: "#55606c"

			Column {
				anchors.centerIn: parent
				Label {
					anchors.horizontalCenter: parent.horizontalCenter
					text: chip.modelData.title
					color: "#e0e0e0"
				}
				Label {
					anchors.horizontalCenter: parent.horizontalCenter
					text: chip.modelData.type
					color: "#7f8c99"
					font.pixelSize: 10
				}
			}

			// Drag ghost: a child of the chip, so MouseArea drag deltas apply in
			// the chip's coordinate system. No reparenting: the palette and the
			// canvas are siblings under the window, and neither clips, so the
			// ghost's scene position is consistent with the canvas DropAreas.
			Rectangle {
				id: chipGhost
				// Payload read by the DropArea via drag.source (internal drags do
				// not carry Drag.mimeData; keys identify the drag kind).
				readonly property string controlType: chip.modelData.type
				width: chip.width
				height: chip.height
				opacity: dragArea.drag.active ? 0.75 : 0
				radius: 4
				color: "#3d4f68"
				border.color: "#7fc1ff"
				border.width: 2
				z: 1000

				Column {
					anchors.centerIn: parent
					Label {
						anchors.horizontalCenter: parent.horizontalCenter
						text: chip.modelData.title
						color: "#e0e0e0"
					}
					Label {
						anchors.horizontalCenter: parent.horizontalCenter
						text: chip.modelData.type
						color: "#7f8c99"
						font.pixelSize: 10
					}
				}

				Drag.active: dragArea.drag.active
				Drag.hotSpot.x: width / 2
				Drag.hotSpot.y: height / 2
				Drag.keys: ["visiondesigner-control"]
				Drag.mimeData: ({
					"application/x-visiondesigner-control": chip.modelData.type
				})
			}

			MouseArea {
				id: dragArea
				anchors.fill: parent
				drag.target: chipGhost
				onReleased: {
					// Drop first (uses the ghost's current scene position), then
					// return the ghost to the chip origin for the next drag.
					chipGhost.Drag.drop();
					chipGhost.x = 0;
					chipGhost.y = 0;
				}
				onClicked: window.appendControl(chip.modelData.type)
			}
		}
	}
}
