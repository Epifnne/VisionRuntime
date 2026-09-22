import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Live preview: instantiates the real Shell control set (Shell/controls/
// NodeView.qml, loaded by file URL) bound to the simulated `serviceClient`
// (PreviewClient). No designer-side reimplementation of the controls exists,
// so preview and production rendering cannot drift apart.
Item {
	id: previewPane
	clip: true

	// Bump to force page Loaders to re-create their NodeView instances.
	property int reloadToken: 0

	function reload() {
		reloadToken += 1;
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		RowLayout {
			Layout.fillWidth: true
			Layout.margins: 6
			spacing: 6

			Label {
				text: "控件目录"
				color: "#7f8c99"
			}
			TextField {
				Layout.fillWidth: true
				text: designerBackend.controlsPath
				placeholderText: "Shell/controls 目录路径"
				onEditingFinished: {
					designerBackend.controlsPath = text;
					previewPane.reload();
				}
			}
			ToolButton {
				text: "重载预览"
				onClicked: {
					serviceClient.setProfile(
						window.currentJson(),
						designerBackend.fileDirectory);
					previewPane.reload();
				}
			}
		}

		Label {
			Layout.fillWidth: true
			Layout.margins: 6
			visible: designerBackend.controlsPath === ""
			text: "未定位 Shell/controls 目录，请手动填写后重载。"
			color: "#ff8080"
			wrapMode: Text.Wrap
		}

		TabBar {
			id: previewTabs
			Layout.fillWidth: true
			Repeater {
				model: {
					window.revision;
					previewPane.reloadToken;
					return (window.doc.ui && window.doc.ui.pages)
						? window.doc.ui.pages : [];
				}
				delegate: TabButton {
					text: modelData.title || modelData.id || "page"
				}
			}
		}

		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			currentIndex: Math.min(previewTabs.currentIndex,
				Math.max(0, count - 1))

			Repeater {
				model: {
					window.revision;
					previewPane.reloadToken;
					return (window.doc.ui && window.doc.ui.pages)
						? window.doc.ui.pages : [];
				}
				delegate: Item {
					property var pageData: modelData

					// Node must be passed at creation time (same as Shell's
					// NodeChild): the Shell controls resolve their endpoint
					// bindings in Component.onCompleted, so assigning node after
					// the load leaves stream resolution permanently broken.
					Loader {
						anchors.fill: parent
						Component.onCompleted: {
							if (designerBackend.controlsPath !== "") {
								setSource("file:///"
									+ designerBackend.controlsPath
									+ "/NodeView.qml", {
									"node": {
										"type": pageData.layout === "row"
											? "row" : "column",
										"children": pageData.children || []
									}
								});
							}
						}
					}
				}
			}
		}
	}
}
