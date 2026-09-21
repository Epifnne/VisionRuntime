import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Recursive dispatcher: instantiates a control tree node from the profile
// `ui` section. Container nodes ("row"/"column") recurse into children;
// leaf nodes map to the generic endpoint-bound controls.
Loader {
	id: root

	property var node: ({})

	Layout.fillWidth: node.fillWidth === true
	Layout.fillHeight: node.fillHeight === true
	Layout.preferredWidth: node.width ? node.width : implicitWidth
	Layout.preferredHeight: node.height ? node.height : implicitHeight

	sourceComponent: {
		if (!node || !node.type) {
			return unknownComponent;
		}
		switch (node.type) {
		case "row": return rowComponent;
		case "column": return columnComponent;
		case "imageView": return imageViewComponent;
		case "stateCard": return stateCardComponent;
		case "commandButton": return commandButtonComponent;
		case "parameterForm": return parameterFormComponent;
		case "statNumber": return statNumberComponent;
		case "resultTable": return resultTableComponent;
		default: return unknownComponent;
		}
	}

	Component {
		id: rowComponent
		RowLayout {
			spacing: 8
			Repeater {
				model: root.node.children || []
				delegate: NodeChild { node: modelData }
			}
		}
	}

	Component {
		id: columnComponent
		ColumnLayout {
			spacing: 8
			Repeater {
				model: root.node.children || []
				delegate: NodeChild { node: modelData }
			}
		}
	}

	// Indirection for recursive child instantiation: loads NodeView by URL
	// so the QML compiler does not see a static type cycle.
	component NodeChild: Loader {
		property var node: ({})
		Layout.fillWidth: node.fillWidth === true
		Layout.fillHeight: node.fillHeight === true
		Layout.preferredWidth: node.width ? node.width : (item ? item.implicitWidth : 0)
		Layout.preferredHeight: node.height ? node.height : (item ? item.implicitHeight : 0)
		Component.onCompleted: setSource("NodeView.qml", { "node": node })
	}

	Component {
		id: imageViewComponent
		ImageView {
			endpoint: root.node.endpoint || ""
			heading: root.node.title || ""
		}
	}

	Component {
		id: stateCardComponent
		StateCard {
			endpoint: root.node.endpoint || ""
			title: root.node.title || root.node.endpoint || ""
		}
	}

	Component {
		id: commandButtonComponent
		CommandButton {
			endpoint: root.node.endpoint || ""
			label: root.node.label || ""
		}
	}

	Component {
		id: parameterFormComponent
		ParameterForm {
			endpoint: root.node.endpoint || ""
			title: root.node.title || root.node.endpoint || ""
		}
	}

	Component {
		id: statNumberComponent
		StatNumber {
			endpoint: root.node.endpoint || ""
			field: root.node.field || ""
			title: root.node.title || root.node.endpoint || ""
		}
	}

	Component {
		id: resultTableComponent
		ResultTable {
			endpoint: root.node.endpoint || ""
			title: root.node.title || root.node.endpoint || ""
		}
	}

	Component {
		id: unknownComponent
		Rectangle {
			implicitWidth: 200
			implicitHeight: 48
			color: "#402020"
			Text {
				anchors.centerIn: parent
				color: "#f08080"
				text: "unknown control: " + (root.node.type || "?")
			}
		}
	}
}
