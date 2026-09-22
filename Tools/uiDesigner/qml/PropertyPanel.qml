import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Property panel for the selected node (or page): endpoint binding dropdown
// filtered by the control's required endpoint kind, plus layout properties.
Item {
	id: panel

	property var node: {
		window.revision;
		return window.nodeAt(window.selectedPath);
	}
	readonly property bool isPage: node && node.type === undefined
	readonly property bool isContainer: node && (node.type === "row"
		|| node.type === "column" || node.type === undefined)

	// Accepts a pixel number ("160") or a CSS-like percentage ("50%"); an
	// empty/invalid value clears the size so the control auto-sizes.
	function assignSize(key, text) {
		var trimmed = text.trim();
		if (trimmed === "") {
			delete node[key];
		} else if (trimmed.endsWith("%")) {
			var percent = parseFloat(trimmed);
			if (isNaN(percent) || percent <= 0) {
				delete node[key];
			} else {
				node[key] = trimmed;
			}
		} else {
			var value = parseInt(trimmed);
			if (isNaN(value) || value <= 0) {
				delete node[key];
			} else {
				node[key] = value;
			}
		}
		window.commitDoc();
	}

	function endpointNames(kind) {
		var result = [""];
		var all = designerBackend.endpoints;
		for (var i = 0; i < all.length; ++i) {
			if (all[i].kind === kind) {
				result.push(all[i].name);
			}
		}
		return result;
	}

	ScrollView {
		anchors.fill: parent
		clip: true
		contentWidth: availableWidth

		ColumnLayout {
			width: parent.width
			spacing: 6

			Label {
				Layout.fillWidth: true
				Layout.margins: 8
				visible: !panel.node
				text: "在画布上点击控件或页面查看属性。"
				color: "#888888"
				wrapMode: Text.Wrap
			}

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 8
				visible: panel.node
				title: panel.node
					? (panel.isPage ? "页面属性" : "控件属性: " + (panel.node.type || ""))
					: ""

				GridLayout {
					anchors.fill: parent
					columns: 2
					columnSpacing: 8
					rowSpacing: 6

					Label { text: "id"; visible: panel.isPage }
					TextField {
						Layout.fillWidth: true
						visible: panel.isPage
						enabled: panel.isPage
						text: panel.isPage && panel.node.id !== undefined
							? panel.node.id : ""
						onEditingFinished: {
							if (panel.isPage) {
								panel.node.id = text;
								window.commitDoc();
							}
						}
					}

					Label { text: "标题" }
					TextField {
						Layout.fillWidth: true
						enabled: panel.node
						text: {
							window.revision;
							return panel.node && panel.node.title !== undefined
								? panel.node.title : "";
						}
						onEditingFinished: {
							if (panel.node) {
								panel.node.title = text;
								window.commitDoc();
							}
						}
					}

					Label { text: "布局方向"; visible: panel.isPage }
					ComboBox {
						Layout.fillWidth: true
						visible: panel.isPage
						model: ["column", "row"]
						currentIndex: panel.isPage
							&& panel.node.layout === "row" ? 1 : 0
						onActivated: function(index) {
							if (panel.isPage) {
								panel.node.layout = model[index];
								window.commitDoc();
							}
						}
					}

					Label {
						text: "端点"
						visible: panel.node && !panel.isContainer
					}
					ComboBox {
						Layout.fillWidth: true
						visible: panel.node && !panel.isContainer
						enabled: panel.node && !panel.isContainer
						model: {
							window.revision;
							designerBackend.endpoints;
							return panel.node && !panel.isContainer
								? panel.endpointNames(
									window.controlKinds[panel.node.type])
								: [];
						}
						currentIndex: {
							var endpoint = panel.node && panel.node.endpoint
								? panel.node.endpoint : "";
							var found = model.indexOf(endpoint);
							return found >= 0 ? found : 0;
						}
						onActivated: function(index) {
							if (panel.node) {
								panel.node.endpoint = model[index];
								window.commitDoc();
							}
						}
					}

					Label {
						text: "按钮文字"
						visible: panel.node
							&& panel.node.type === "commandButton"
					}
					TextField {
						Layout.fillWidth: true
						visible: panel.node
							&& panel.node.type === "commandButton"
						enabled: visible
						text: {
							window.revision;
							return panel.node
								&& panel.node.label !== undefined
								? panel.node.label : "";
						}
						onEditingFinished: {
							if (panel.node) {
								panel.node.label = text;
								window.commitDoc();
							}
						}
					}

					Label {
						text: "字段"
						visible: panel.node
							&& panel.node.type === "statNumber"
					}
					TextField {
						Layout.fillWidth: true
						visible: panel.node
							&& panel.node.type === "statNumber"
						enabled: visible
						placeholderText: "JSON 字段名，如 completed"
						text: {
							window.revision;
							return panel.node
								&& panel.node.field !== undefined
								? panel.node.field : "";
						}
						onEditingFinished: {
							if (panel.node) {
								panel.node.field = text;
								window.commitDoc();
							}
						}
					}

					Label { text: "宽度"; visible: !panel.isPage }
					TextField {
						Layout.fillWidth: true
						visible: !panel.isPage
						enabled: !panel.isPage
						placeholderText: "px 或 50%，空 = 自动"
						text: {
							window.revision;
							return panel.node && panel.node.width !== undefined
								? String(panel.node.width) : "";
						}
						onEditingFinished: {
							if (!panel.node) {
								return;
							}
							panel.assignSize("width", text);
						}
					}

					Label { text: "高度"; visible: !panel.isPage }
					TextField {
						Layout.fillWidth: true
						visible: !panel.isPage
						enabled: !panel.isPage
						placeholderText: "px 或 50%，空 = 自动"
						text: {
							window.revision;
							return panel.node && panel.node.height !== undefined
								? String(panel.node.height) : "";
						}
						onEditingFinished: {
							if (!panel.node) {
								return;
							}
							panel.assignSize("height", text);
						}
					}

					CheckBox {
						Layout.columnSpan: 2
						visible: !panel.isPage
						text: "横向拉伸 (fillWidth)"
						checked: panel.node && panel.node.fillWidth === true
						onToggled: {
							if (!panel.node) {
								return;
							}
							if (checked) {
								panel.node.fillWidth = true;
							} else {
								delete panel.node.fillWidth;
							}
							window.commitDoc();
						}
					}

					CheckBox {
						Layout.columnSpan: 2
						visible: !panel.isPage
						text: "纵向拉伸 (fillHeight)"
						checked: panel.node && panel.node.fillHeight === true
						onToggled: {
							if (!panel.node) {
								return;
							}
							if (checked) {
								panel.node.fillHeight = true;
							} else {
								delete panel.node.fillHeight;
							}
							window.commitDoc();
						}
					}
				}
			}

			RowLayout {
				Layout.margins: 8
				visible: panel.node && !panel.isPage
				spacing: 6

				Button {
					text: "上移"
					onClicked: window.reorderNode(window.selectedPath, -1)
				}
				Button {
					text: "下移"
					onClicked: window.reorderNode(window.selectedPath, 1)
				}
				Button {
					text: "删除"
					onClicked: window.removeNode(window.selectedPath)
				}
			}

			Label {
				Layout.fillWidth: true
				Layout.margins: 8
				visible: panel.node && !panel.isPage
				text: "提示：可拖拽控件本体在容器间移动；拖拽左侧端点到控件上直接绑定。"
				color: "#7f8c99"
				font.pixelSize: 11
				wrapMode: Text.Wrap
			}
		}
	}
}
