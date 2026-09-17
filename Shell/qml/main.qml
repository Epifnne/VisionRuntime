import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../controls"

// Generic profile-driven shell. The page tree is parsed from the profile
// `ui` section (serviceClient.uiJson) and instantiated through NodeView;
// nothing product-specific is hardcoded here.
ApplicationWindow {
	id: window
	visible: true
	width: 1280
	height: 800
	title: "visionShell"

	property var uiDefinition: ({})

	function reloadUi() {
		try {
			window.uiDefinition = JSON.parse(serviceClient.uiJson);
		} catch (e) {
			window.uiDefinition = {};
		}
	}

	Component.onCompleted: reloadUi()

	Connections {
		target: serviceClient
		function onRegistryChanged() { window.reloadUi(); }
	}

	header: ToolBar {
		ColumnLayout {
			anchors.fill: parent
			spacing: 0

			RowLayout {
				Layout.fillWidth: true

				ToolButton {
					text: "Load Profile"
					onClicked: profileDialog.open()
				}

				TabBar {
					id: tabBar
					Layout.fillWidth: true

					Repeater {
						model: window.uiDefinition.pages || []
						delegate: TabButton {
							text: modelData.title || modelData.id || "page"
						}
					}
				}

				Label {
					text: serviceClient.lastError
					color: "red"
					elide: Text.ElideRight
					Layout.maximumWidth: 400
				}
			}
		}
	}

	StackLayout {
		anchors.fill: parent
		currentIndex: tabBar.currentIndex

		Repeater {
			model: window.uiDefinition.pages || []

			delegate: Page {
				property var pageDefinition: modelData

				NodeView {
					anchors.fill: parent
					anchors.margins: 8
					node: ({
						"type": pageDefinition.layout === "row" ? "row" : "column",
						"children": pageDefinition.children || []
					})
				}
			}
		}
	}

	Label {
		anchors.centerIn: parent
		visible: !window.uiDefinition.pages || window.uiDefinition.pages.length === 0
		color: "#888888"
		text: "no pages defined in profile ui section"
	}

	FileDialog {
		id: profileDialog
		title: "Select Profile"
		nameFilters: ["JSON files (*.json)"]
		onAccepted: serviceClient.loadProfile(selectedFile)
	}
}
