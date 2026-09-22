import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Product-side profile editing: identity, sources, model package, pipeline
// parameters and the exposed endpoint subset. Writes go straight into
// window.doc; structural edits also refresh the endpoint universe.
Item {
	id: panel

	function commitStructural() {
		window.commitDoc();
		designerBackend.deriveEndpoints(window.currentJson());
	}

	// Live-save a text field's value as the user types (debounced so the
	// canvas is not rebuilt on every keystroke). Because the text field owns
	// the binding, committing doc does not reset the text being typed.
	Timer {
		id: liveSaveTimer
		interval: 300
		onTriggered: panel.commitStructural()
	}
	function liveEdit(assign, text) {
		assign(text);
		liveSaveTimer.restart();
	}

	function numberOrKeep(text, fallback) {
		var value = parseFloat(text);
		return isNaN(value) ? fallback : value;
	}

	// Collapsible section header; the content item toggles with `collapsed`.
	component CollapseHeader: Rectangle {
		id: headerRoot
		property bool collapsed: false
		property string title: ""
		implicitHeight: 24
		radius: 4
		color: "#2c3540"
		RowLayout {
			anchors.fill: parent
			anchors.leftMargin: 6
			anchors.rightMargin: 6
			spacing: 6
			Label {
				text: headerRoot.collapsed ? "▸" : "▾"
				color: "#7f8c99"
				font.pixelSize: 11
			}
			Label {
				Layout.fillWidth: true
				text: headerRoot.title
				color: "#e0e0e0"
				font.pixelSize: 11
				elide: Text.ElideRight
			}
		}
		MouseArea {
			anchors.fill: parent
			onClicked: headerRoot.collapsed = !headerRoot.collapsed
		}
	}

	// Endpoint universe entries of one kind, for the grouped exposure list.
	function endpointsOfKind(kind) {
		var result = [];
		var all = designerBackend.endpoints;
		for (var i = 0; i < all.length; ++i) {
			if ((all[i].kind || "") === kind) {
				result.push(all[i]);
			}
		}
		return result;
	}

	function isExposed(name) {
		window.revision;
		if (!Array.isArray(window.doc.endpoints)) {
			return false;
		}
		for (var i = 0; i < window.doc.endpoints.length; ++i) {
			if (window.doc.endpoints[i].name === name) {
				return true;
			}
		}
		return false;
	}

	function setExposed(name, on) {
		if (!Array.isArray(window.doc.endpoints)) {
			window.doc.endpoints = [];
		}
		if (on) {
			if (!isExposed(name)) {
				window.doc.endpoints.push({ "name": name });
			}
		} else {
			for (var i = 0; i < window.doc.endpoints.length; ++i) {
				if (window.doc.endpoints[i].name === name) {
					window.doc.endpoints.splice(i, 1);
					break;
				}
			}
		}
		window.commitDoc();
	}

	ScrollView {
		anchors.fill: parent
		clip: true
		contentWidth: availableWidth

		ColumnLayout {
			width: parent.width
			spacing: 4

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 6
				title: "产品"

				GridLayout {
					anchors.fill: parent
					columns: 2
					rowSpacing: 6

					Label { text: "id" }
					TextField {
						Layout.fillWidth: true
						text: window.doc.id || ""
						onTextChanged: panel.liveEdit(function(t) {
							window.doc.id = t;
						}, text)
					}
					Label { text: "名称" }
					TextField {
						Layout.fillWidth: true
						text: window.doc.name || ""
						onTextChanged: panel.liveEdit(function(t) {
							window.doc.name = t;
						}, text)
					}
				}
			}

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 6
				title: "相机 / 源"

				ColumnLayout {
					anchors.fill: parent
					spacing: 6

					Repeater {
						model: {
							window.revision;
							return Array.isArray(window.doc.sources)
								? window.doc.sources.length : 0;
						}

delegate: ColumnLayout {
						id: sourceBox
						required property int index
						property var source: {
							window.revision;
							return window.doc.sources[index];
						}
						property bool collapsed: false

						Layout.fillWidth: true
						spacing: 2

						function summary() {
							if (!sourceBox.source) {
								return "";
							}
							var s = sourceBox.source;
							var text = (s.id || "") + " (" + (s.type || "") + ")";
							if (sourceBox.collapsed && s.type === "camera") {
								var model = s.modelName || "";
								var address = s.serialNumber || s.ipAddress || "";
								if (model || address) {
									text += "  " + model
										+ (model && address ? " / " : "") + address;
								}
							}
							return text;
						}

						// Collapse header; click toggles the field grid below.
						Rectangle {
							Layout.fillWidth: true
							height: 24
							radius: 4
							color: "#2c3540"

							RowLayout {
								anchors.fill: parent
								anchors.leftMargin: 6
								anchors.rightMargin: 6
								spacing: 6
								Label {
									text: sourceBox.collapsed ? "▸" : "▾"
									color: "#7f8c99"
									font.pixelSize: 11
								}
								Label {
									Layout.fillWidth: true
									text: sourceBox.summary()
									color: "#e0e0e0"
									font.pixelSize: 11
									elide: Text.ElideRight
								}
							}
							MouseArea {
								anchors.fill: parent
								onClicked: sourceBox.collapsed = !sourceBox.collapsed
							}
						}

						GridLayout {
							Layout.fillWidth: true
							visible: !sourceBox.collapsed
								columns: 2
								rowSpacing: 4

								Label { text: "id" }
								TextField {
									Layout.fillWidth: true
									text: sourceBox.source
										? sourceBox.source.id : ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.id = t;
									}, text)
								}

								Label { text: "类型" }
								ComboBox {
									Layout.fillWidth: true
									model: ["directory", "camera"]
									currentIndex: sourceBox.source
										&& sourceBox.source.type === "camera"
										? 1 : 0
									onActivated: function(i) {
										sourceBox.source.type = model[i];
										panel.commitStructural();
									}
								}

								Label {
									text: "目录"
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
									text: visible && sourceBox.source
										? (sourceBox.source.directory || "") : ""
									onEditingFinished: {
										sourceBox.source.directory = text;
										panel.commitStructural();
									}
								}

								Label {
									text: "扩展名"
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
									placeholderText: ".ppm,.bmp"
									text: visible && sourceBox.source
										&& Array.isArray(
											sourceBox.source.extensions)
										? sourceBox.source.extensions.join(",")
										: ""
									onEditingFinished: {
										var parts = text.split(",");
										var list = [];
										for (var i = 0; i < parts.length; ++i) {
											var trimmed = parts[i].trim();
											if (trimmed) {
												list.push(trimmed);
											}
										}
										sourceBox.source.extensions = list;
										panel.commitStructural();
									}
								}

								Label {
									text: "帧间隔 ms"
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: !sourceBox.source
										|| sourceBox.source.type !== "camera"
									text: visible && sourceBox.source
										? String(
											sourceBox.source
												.frameIntervalMilliseconds
												|| 100) : ""
									onEditingFinished: {
										sourceBox.source
											.frameIntervalMilliseconds =
												panel.numberOrKeep(text, 100);
										panel.commitStructural();
									}
								}

								Label {
									text: "序列号"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
									text: visible
										? (sourceBox.source.serialNumber || "")
										: ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.serialNumber = t;
									}, text)
								}

								Label {
									text: "厂商"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
								}
								ComboBox {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
									model: ["海康", "大华", "Basler", "其他"]
									currentIndex: {
										if (!sourceBox.source) {
											return 0;
										}
										switch (sourceBox.source.vendor) {
										case "hikrobot": return 0;
										case "daheng": return 1;
										case "basler": return 2;
										default: return 3;
										}
									}
									onActivated: function(i) {
										var vendor = ["hikrobot", "daheng",
											"basler", "other"][i];
										if (!sourceBox.source
												|| sourceBox.source.vendor === vendor) {
											return;
										}
										sourceBox.source.vendor = vendor;
										panel.commitStructural();
									}
								}

								Label {
									text: "IP 地址"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
									placeholderText: "可选，GigE 相机可按 IP 接入"
									text: visible && sourceBox.source
										? (sourceBox.source.ipAddress || "") : ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.ipAddress = t;
									}, text)
								}

								Label {
									text: "像素格式"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
								}
								TextField {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
									text: visible
										? (sourceBox.source.pixelFormat || "")
										: ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.pixelFormat = t;
									}, text)
								}

								// 定时采集: continuous (free-run, optional frameRate),
								// timedTrigger (a frame every triggerIntervalMilliseconds),
								// softwareTrigger (frame on the softwareTrigger endpoint).
								Label {
									text: "采集模式"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
								}
								ComboBox {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
									model: ["continuous", "timedTrigger",
										"softwareTrigger"]
									currentIndex: {
										var mode = sourceBox.source
											? (sourceBox.source.mode || "continuous")
											: "continuous";
										var found = model.indexOf(mode);
										return found >= 0 ? found : 0;
									}
									onActivated: function(i) {
										sourceBox.source.mode = model[i];
										panel.commitStructural();
									}
								}

								Label {
									text: "触发间隔 ms"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
										&& sourceBox.source.mode === "timedTrigger"
								}
								TextField {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
										&& sourceBox.source.mode === "timedTrigger"
									text: visible && sourceBox.source
										? String(sourceBox.source
											.triggerIntervalMilliseconds || 100) : ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.triggerIntervalMilliseconds =
											panel.numberOrKeep(t, 100);
									}, text)
								}

								Label {
									text: "帧率 fps"
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
										&& (!sourceBox.source.mode
											|| sourceBox.source.mode === "continuous")
								}
								TextField {
									Layout.fillWidth: true
									visible: sourceBox.source
										&& sourceBox.source.type === "camera"
										&& (!sourceBox.source.mode
											|| sourceBox.source.mode === "continuous")
									text: visible && sourceBox.source
										&& sourceBox.source.frameRate !== undefined
										? String(sourceBox.source.frameRate) : ""
									onTextChanged: panel.liveEdit(function(t) {
										sourceBox.source.frameRate =
											panel.numberOrKeep(t, 0);
									}, text)
								}

								Button {
									Layout.columnSpan: 2
									text: "删除此源"
									onClicked: {
										window.doc.sources.splice(
											sourceBox.index, 1);
										panel.commitStructural();
									}
								}
							}
						}
					}

					RowLayout {
						spacing: 6
						Button {
							text: "＋目录源"
							onClicked: {
								if (!Array.isArray(window.doc.sources)) {
									window.doc.sources = [];
								}
								window.doc.sources.push({
									"id": "cam"
										+ window.doc.sources.length,
									"type": "directory",
									"role": "offline replay",
									"directory": "",
									"extensions": [".bmp"],
									"loop": true,
									"frameIntervalMilliseconds": 100
								});
								panel.commitStructural();
							}
						}
						Button {
							text: "＋相机源"
							onClicked: {
								if (!Array.isArray(window.doc.sources)) {
									window.doc.sources = [];
								}
								window.doc.sources.push({
									"id": "cam"
										+ window.doc.sources.length,
									"type": "camera",
									"role": "inspection",
										"vendor": "hikrobot",
									"serialNumber": "",
									"ipAddress": "",
									"pixelFormat": "Mono8"
								});
								panel.commitStructural();
							}
						}
					}
				}
			}

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 6
				title: "模型"

				ColumnLayout {
					anchors.fill: parent
					spacing: 4

					CollapseHeader {
						id: modelHeader
						Layout.fillWidth: true
						title: "模型包与后端"
					}

					GridLayout {
						Layout.fillWidth: true
						visible: !modelHeader.collapsed
						columns: 2
						rowSpacing: 4

						Label { text: "模型包路径" }
						TextField {
							Layout.fillWidth: true
							text: window.doc.model
								? (window.doc.model.packagePath || "") : ""
							onTextChanged: panel.liveEdit(function(t) {
								if (!window.doc.model) { window.doc.model = {}; }
								window.doc.model.packagePath = t;
							}, text)
						}
						Label { text: "backendId" }
						TextField {
							Layout.fillWidth: true
							text: window.doc.model
								? (window.doc.model.backendId || "") : ""
							onTextChanged: panel.liveEdit(function(t) {
								if (!window.doc.model) { window.doc.model = {}; }
								window.doc.model.backendId = t;
							}, text)
						}
						Label { text: "device" }
						TextField {
							Layout.fillWidth: true
							text: window.doc.model
								? (window.doc.model.device || "") : ""
							onTextChanged: panel.liveEdit(function(t) {
								if (!window.doc.model) { window.doc.model = {}; }
								window.doc.model.device = t;
							}, text)
						}
						Label { text: "插件目录" }
						TextField {
							Layout.fillWidth: true
							text: window.doc.model
								? (window.doc.model.pluginDirectory || "") : ""
							onTextChanged: panel.liveEdit(function(t) {
								if (!window.doc.model) { window.doc.model = {}; }
								window.doc.model.pluginDirectory = t;
							}, text)
						}
					}
				}
			}

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 6
				title: "管线"

				ColumnLayout {
					anchors.fill: parent
					spacing: 4

					CollapseHeader {
						id: pipelineHeader
						Layout.fillWidth: true
						title: "缩放 / 裁剪 / 批处理"
					}

					// The side panel is fixed at 240 px; a Flow wraps the fields
					// so nothing is truncated regardless of the available width.
					Flow {
						Layout.fillWidth: true
						visible: !pipelineHeader.collapsed
						spacing: 6

						Repeater {
							model: [
								{ "key": "resizeShortSide", "label": "缩放短边" },
								{ "key": "cropWidth", "label": "裁剪宽" },
								{ "key": "cropHeight", "label": "裁剪高" },
								{ "key": "threshold", "label": "阈值" },
								{ "key": "maxBatchSize", "label": "批大小" },
								{ "key": "flushTimeoutMilliseconds",
									"label": "刷批 ms" },
								{ "key": "queueCapacity", "label": "队列容量" }
							]
							delegate: ColumnLayout {
								required property var modelData
								Label {
									text: modelData.label
									font.pixelSize: 10
									color: "#7f8c99"
								}
								TextField {
									Layout.preferredWidth: 90
									text: {
										window.revision;
										return window.doc.pipeline
											&& window.doc.pipeline[modelData.key]
												!== undefined
											? String(window.doc.pipeline[
												modelData.key]) : "";
									}
									onTextChanged: panel.liveEdit(function(t) {
										if (!window.doc.pipeline) {
											window.doc.pipeline = {};
										}
										window.doc.pipeline[modelData.key] =
											panel.numberOrKeep(t, 0);
									}, text)
								}
							}
						}

						ColumnLayout {
							Label {
								text: "满队列策略"
								font.pixelSize: 10
								color: "#7f8c99"
							}
							ComboBox {
								model: ["block", "drop"]
								currentIndex: window.doc.pipeline
									&& window.doc.pipeline.queueFullPolicy === "drop"
									? 1 : 0
								onActivated: function(i) {
									if (!window.doc.pipeline) {
										window.doc.pipeline = {};
									}
									window.doc.pipeline.queueFullPolicy = model[i];
									window.commitDoc();
								}
							}
						}
					}
				}
			}

			GroupBox {
				Layout.fillWidth: true
				Layout.margins: 6
				title: "端点暴露"

				ColumnLayout {
					anchors.fill: parent
					spacing: 2

					// Grouped by endpoint kind; empty groups are hidden.
					Repeater {
						model: [
							{ "kind": "stream", "label": "视频流",
								"color": "#e0a040" },
							{ "kind": "state", "label": "状态",
								"color": "#4caf7d" },
							{ "kind": "parameter", "label": "参数",
								"color": "#8e6bbf" },
							{ "kind": "command", "label": "命令",
								"color": "#4f9cf9" }
						]
						delegate: ColumnLayout {
							id: kindGroup
							required property var modelData
							readonly property var entries: panel.endpointsOfKind(
								modelData.kind)

							Layout.fillWidth: true
							spacing: 2
							visible: entries.length > 0

							Label {
								text: kindGroup.modelData.label
								color: kindGroup.modelData.color
								font.pixelSize: 11
								font.bold: true
								topPadding: 4
							}
							Repeater {
								model: kindGroup.entries
								delegate: CheckBox {
									required property var modelData
									text: modelData.name
									font.pixelSize: 11
									checked: panel.isExposed(modelData.name)
									onToggled: panel.setExposed(
										modelData.name, checked)
								}
							}
						}
					}
				}
			}
		}
	}
}
