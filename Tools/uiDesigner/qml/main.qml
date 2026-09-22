import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// visionDesigner main window: endpoint tree / control palette / product form
// on the left, control-tree canvas (or live preview) in the center, property
// panel on the right, validation issues in the footer.
//
// All edits mutate `window.doc` (the parsed profile JSON) and finish through
// `commitDoc()`, which bumps `revision`; canvas delegates re-resolve their
// node from `revision` so the tree fully reflects the document after every
// edit (this is also what makes save/reload roundtrips exact).
ApplicationWindow {
	id: window
	visible: true
	width: 1600
	height: 900
	title: (designerBackend.filePath || "untitled") + " - visionDesigner"

	property var doc: ({})
	property int revision: 0
	property string selectedPath: ""
	property bool previewMode: false
	// Diagnostic: last drop/insert event, shown live in the header so the
	// drag result is visible without reading the console.
	property string dropDiag: "尚未拖放"

	// Control type -> required endpoint kind (mirrors Shell NodeView).
	readonly property var controlKinds: ({
		"imageView": "stream",
		"stateCard": "state",
		"commandButton": "command",
		"parameterForm": "parameter",
		"statNumber": "state",
		"resultTable": "state"
	})

	function ensureUi() {
		if (!doc.ui || typeof doc.ui !== "object") {
			doc.ui = { "pages": [] };
		}
		if (!Array.isArray(doc.ui.pages)) {
			doc.ui.pages = [];
		}
	}

	function adoptJson(text) {
		doc = JSON.parse(text);
		ensureUi();
		selectedPath = "";
		revision += 1;
		designerBackend.deriveEndpoints(currentJson());
	}

	function currentJson() {
		return JSON.stringify(doc, null, 2);
	}

	function commitDoc() {
		revision += 1;
	}

	// Path format: "<page>/<child>/<grandchild>/..." (indices).
	function nodeAt(path) {
		if (!path || !doc.ui || !Array.isArray(doc.ui.pages)) {
			return null;
		}
		var parts = path.split("/");
		var node = doc.ui.pages[parseInt(parts[0])];
		for (var i = 1; i < parts.length && node; ++i) {
			node = (node.children || [])[parseInt(parts[i])];
		}
		return node || null;
	}

	function parentOf(path) {
		var parts = path.split("/");
		if (parts.length < 2) {
			return null;
		}
		var index = parseInt(parts.pop());
		return { "node": nodeAt(parts.join("/")), "index": index,
			"path": parts.join("/") };
	}

	function makeNode(type) {
		var node = { "type": type };
		if (type === "row" || type === "column") {
			node.children = [];
		} else {
			node.endpoint = "";
			node.title = "";
			if (type === "commandButton") {
				node.label = "";
			}
			if (type === "statNumber") {
				node.field = "";
			}
		}
		return node;
	}

	function insertNode(containerPath, index, node) {
		var container = nodeAt(containerPath);
		if (!container) {
			return;
		}
		if (!Array.isArray(container.children)) {
			container.children = [];
		}
		if (index < 0 || index > container.children.length) {
			index = container.children.length;
		}
		container.children.splice(index, 0, node);
		window.dropDiag = "插入 " + (node ? node.type : "?")
			+ " → " + containerPath + "[" + index + "]"
			+ " 共" + container.children.length + "个子项";
		commitDoc();
	}

	// Click-to-add fallback for the palette: appends to the selected
	// container (or the selected leaf's parent, or the first page).
	function appendControl(type) {
		var target = "0";
		var selected = nodeAt(selectedPath);
		if (selected) {
			if (selected.type === "row" || selected.type === "column"
					|| selected.type === undefined) {
				target = selectedPath;
			} else {
				var parent = parentOf(selectedPath);
				if (parent) {
					target = parent.path;
				}
			}
		}
		var container = nodeAt(target);
		if (!container) {
			return;
		}
		insertNode(target, -1, makeNode(type));
		selectedPath = target + "/" + (container.children.length - 1);
	}

	function removeNode(path) {
		var parent = parentOf(path);
		if (!parent || !parent.node || !Array.isArray(parent.node.children)) {
			return;
		}
		parent.node.children.splice(parent.index, 1);
		if (selectedPath === path || selectedPath.indexOf(path + "/") === 0) {
			selectedPath = "";
		}
		commitDoc();
	}

	function moveNode(sourcePath, containerPath, index) {
		if (sourcePath === containerPath) {
			return;
		}
		// Refuse to drop a container into its own subtree.
		if (containerPath.indexOf(sourcePath + "/") === 0) {
			return;
		}
		var node = nodeAt(sourcePath);
		if (!node) {
			return;
		}
		var parent = parentOf(sourcePath);
		if (!parent || !parent.node) {
			return;
		}
		if (parent.path === containerPath && parent.index < index) {
			index -= 1;
		}
		parent.node.children.splice(parent.index, 1);
		var container = nodeAt(containerPath);
		if (!container) {
			return;
		}
		if (!Array.isArray(container.children)) {
			container.children = [];
		}
		if (index < 0 || index > container.children.length) {
			index = container.children.length;
		}
		container.children.splice(index, 0, node);
		if (selectedPath === sourcePath) {
			selectedPath = "";
		}
		commitDoc();
	}

	function reorderNode(path, delta) {
		var parent = parentOf(path);
		if (!parent || !parent.node || !Array.isArray(parent.node.children)) {
			return;
		}
		var target = parent.index + delta;
		if (target < 0 || target >= parent.node.children.length) {
			return;
		}
		var children = parent.node.children;
		var tmp = children[parent.index];
		children[parent.index] = children[target];
		children[target] = tmp;
		selectedPath = parent.path + "/" + target;
		commitDoc();
	}

	function addPage() {
		ensureUi();
		doc.ui.pages.push({
			"id": "page" + doc.ui.pages.length,
			"title": "Page " + (doc.ui.pages.length + 1),
			"layout": "column",
			"children": []
		});
		commitDoc();
	}

	function removePage(index) {
		if (!doc.ui || !Array.isArray(doc.ui.pages)
				|| index < 0 || index >= doc.ui.pages.length) {
			return;
		}
		doc.ui.pages.splice(index, 1);
		selectedPath = "";
		commitDoc();
	}

	function saveTo(path) {
		if (designerBackend.saveProfile(path, currentJson())) {
			window.title = path + " - visionDesigner";
		}
	}

	function enterPreview() {
		serviceClient.setProfile(currentJson(), designerBackend.fileDirectory);
		previewPane.reload();
		window.previewMode = true;
	}

	Component.onCompleted: {
		if (startupProfile !== "") {
			var text = designerBackend.loadProfile(startupProfile);
			if (text) {
				adoptJson(text);
			}
		} else {
			adoptJson(designerBackend.newFromTemplate("twoCameraDirectory"));
		}
		if (startupPreview) {
			enterPreview();
		}
	}

	header: ToolBar {
		RowLayout {
			anchors.fill: parent
			spacing: 6

			ComboBox {
				id: templateCombo
				model: designerBackend.templateIds
				implicitWidth: 180
			}
			ToolButton {
				text: "新建"
				onClicked: window.adoptJson(
					designerBackend.newFromTemplate(templateCombo.currentText))
			}
			ToolButton {
				text: "打开"
				onClicked: openDialog.open()
			}
			ToolButton {
				text: "保存"
				onClicked: {
					if (designerBackend.filePath) {
						window.saveTo(designerBackend.filePath);
					} else {
						saveDialog.open();
					}
				}
			}
			ToolButton {
				text: "另存为"
				onClicked: saveDialog.open()
			}
			ToolButton {
				text: "校验"
				onClicked: designerBackend.validate(window.currentJson())
			}
			ToolButton {
				text: "加载 manifest"
				onClicked: manifestDialog.open()
			}
			ToolButton {
				text: "发布"
				onClicked: publishDialog.open()
			}
			ToolSeparator {}
			ToolButton {
				text: window.previewMode ? "退出预览" : "预览"
				checkable: true
				checked: window.previewMode
				onClicked: {
					if (window.previewMode) {
						window.previewMode = false;
					} else {
						window.enterPreview();
					}
				}
			}
			Label {
				Layout.fillWidth: true
				text: designerBackend.statusMessage
				color: "#9ecfff"
				elide: Text.ElideMiddle
			}
			Label {
				text: window.dropDiag
				color: "#7fd08f"
				font.pixelSize: 11
			}
		}
	}

	RowLayout {
		anchors.fill: parent
		spacing: 0

		ColumnLayout {
			// Fixed side panels: only the canvas (Layout.fillWidth) is
			// elastic, so the canvas always gets every spare pixel.
			Layout.preferredWidth: 240
			Layout.minimumWidth: 240
			Layout.maximumWidth: 240
			Layout.fillHeight: true
			spacing: 0
			visible: !window.previewMode
			TabBar {
				id: leftTabs
				Layout.fillWidth: true
				TabButton { text: "控件" }
				TabButton { text: "产品" }
			}
			StackLayout {
				Layout.fillWidth: true
				Layout.fillHeight: true
				currentIndex: leftTabs.currentIndex

				PalettePane {}
				ProductPanel {}
			}
		}

		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			currentIndex: window.previewMode ? 1 : 0

			CanvasPane {}
			PreviewPane { id: previewPane }
		}

		PropertyPanel {
			Layout.preferredWidth: 260
			Layout.minimumWidth: 260
			Layout.maximumWidth: 260
			Layout.fillHeight: true
			visible: !window.previewMode
		}
	}

	footer: ToolBar {
		visible: designerBackend.issues.length > 0
		ListView {
			id: issueList
			anchors.fill: parent
			clip: true
			model: designerBackend.issues
			delegate: RowLayout {
				width: issueList.width
				spacing: 8
				Label {
					text: modelData.severity === "error" ? "✖" : "⚠"
					color: modelData.severity === "error"
						? "#ff7070" : "#ffb300"
				}
				Label {
					Layout.fillWidth: true
					text: (modelData.path ? "[" + modelData.path + "] " : "")
						+ modelData.message
					elide: Text.ElideRight
					MouseArea {
						anchors.fill: parent
						enabled: modelData.path !== ""
							&& modelData.path.indexOf("/") >= 0
						onClicked: {
							window.previewMode = false;
							window.selectedPath = modelData.path;
						}
					}
				}
			}
		}
	}

	FileDialog {
		id: openDialog
		title: "打开产品 profile"
		nameFilters: ["JSON files (*.json)"]
		onAccepted: {
			var text = designerBackend.loadProfile(selectedFile);
			if (text) {
				window.adoptJson(text);
			}
		}
	}

	FileDialog {
		id: saveDialog
		title: "保存产品 profile"
		fileMode: FileDialog.SaveFile
		nameFilters: ["JSON files (*.json)"]
		onAccepted: window.saveTo(selectedFile)
	}

	FileDialog {
		id: manifestDialog
		title: "加载 manifest JSON"
		nameFilters: ["JSON files (*.json)"]
		onAccepted: designerBackend.loadManifest(selectedFile)
	}

	FolderDialog {
		id: publishDialog
		title: "选择产品包发布目录"
		onAccepted: designerBackend.publish(selectedFolder, window.currentJson())
	}
}
