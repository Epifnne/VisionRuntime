import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Recursive control-tree editor node. Containers (row/column, and the page
// object itself) accept drops from the palette (insert) and from other nodes
// (move); leaf controls accept endpoint drops from the endpoint tree.
//
// Recursion goes through Loader + setSource("EditorNode.qml") because
// qmlcachegen rejects statically recursive QML types (same constraint as
// Shell/controls/NodeView.qml).
Loader {
	id: root

	property string path: ""
	// Resolve the node lazily through a function so a commit (revision bump)
	// does not tear down and rebuild this Loader mid-interaction. Callers
	// read `root.node` which re-resolves on each access after a commit.
	property var node: {
		window.revision; // re-resolve after every committed edit
		return window.nodeAt(root.path);
	}
	readonly property bool isContainer: node
		&& (node.type === "row" || node.type === "column"
			|| node.type === undefined)
	readonly property bool horizontal: node
		&& (node.type === "row"
			|| (node.type === undefined && node.layout === "row"))

	// width / height accept either a pixel number or a CSS-like percentage
	// string ("50%"). A percentage becomes a layout weight (needs fillWidth /
	// fillHeight to take effect); a plain number is a fixed pixel size.
	function sizeIsPercent(key) {
		var value = node ? node[key] : undefined;
		return typeof value === "string" && value.trim().endsWith("%");
	}
	function sizePercent(key) {
		if (!sizeIsPercent(key)) {
			return 0;
		}
		var value = parseFloat(node[key]);
		return isNaN(value) ? 0 : value;
	}
	function sizePixels(key) {
		var value = node ? node[key] : undefined;
		if (typeof value === "number") {
			return value;
		}
		if (typeof value === "string" && !sizeIsPercent(key)) {
			var parsed = parseFloat(value);
			return isNaN(parsed) ? 0 : parsed;
		}
		return 0;
	}

	readonly property real widthPercent: sizePercent("width")
	readonly property real heightPercent: sizePercent("height")
	readonly property real widthPx: sizePixels("width")
	readonly property real heightPx: sizePixels("height")
	readonly property bool fillsWidth: node
		&& (node.fillWidth === true || widthPercent > 0)
	readonly property bool fillsHeight: node
		&& (node.fillHeight === true || heightPercent > 0)

	Layout.fillWidth: fillsWidth
	Layout.fillHeight: fillsHeight
	Layout.preferredWidth: widthPercent > 0 ? widthPercent
		: (widthPx > 0 ? widthPx : implicitWidth)
	Layout.preferredHeight: heightPercent > 0 ? heightPercent
		: (heightPx > 0 ? heightPx : implicitHeight)

	sourceComponent: isContainer ? containerComponent : leafComponent

	// Indirection for recursive child instantiation. The model is an array of
	// child path strings (not the Repeater index): inline-component delegates
	// do not resolve the surrounding `index` context property reliably. The
	// Layout attached properties must live on this delegate (mirroring Shell's
	// NodeView.NodeChild); without them the surrounding layout never
	// re-distributes when asynchronously loaded children get their size.
	component NodeChild: Loader {
		property string childPath: ""
		readonly property var childNode: window.nodeAt(childPath)
		readonly property real childWidthPercent: {
			var value = childNode ? childNode.width : undefined;
			if (typeof value === "string" && value.trim().endsWith("%")) {
				var parsed = parseFloat(value);
				return isNaN(parsed) ? 0 : parsed;
			}
			return 0;
		}
		readonly property real childHeightPercent: {
			var value = childNode ? childNode.height : undefined;
			if (typeof value === "string" && value.trim().endsWith("%")) {
				var parsed = parseFloat(value);
				return isNaN(parsed) ? 0 : parsed;
			}
			return 0;
		}
		readonly property real childWidthPx: {
			var value = childNode ? childNode.width : undefined;
			if (typeof value === "number") {
				return value;
			}
			if (typeof value === "string" && childWidthPercent === 0) {
				var parsed = parseFloat(value);
				return isNaN(parsed) ? 0 : parsed;
			}
			return 0;
		}
		readonly property real childHeightPx: {
			var value = childNode ? childNode.height : undefined;
			if (typeof value === "number") {
				return value;
			}
			if (typeof value === "string" && childHeightPercent === 0) {
				var parsed = parseFloat(value);
				return isNaN(parsed) ? 0 : parsed;
			}
			return 0;
		}

		Layout.fillWidth: childNode
			&& (childNode.fillWidth === true || childWidthPercent > 0)
		Layout.fillHeight: childNode
			&& (childNode.fillHeight === true || childHeightPercent > 0)
		// Fall back to the child's implicit size so a delegate never
		// collapses to 0x0 (which would cover the container DropArea).
		Layout.preferredWidth: childWidthPercent > 0 ? childWidthPercent
			: (childWidthPx > 0 ? childWidthPx
				: (item && item.implicitWidth > 0 ? item.implicitWidth : 160))
		Layout.preferredHeight: childHeightPercent > 0 ? childHeightPercent
			: (childHeightPx > 0 ? childHeightPx
				: (item && item.implicitHeight > 0 ? item.implicitHeight : 66))
		Component.onCompleted: setSource("EditorNode.qml", {
			"path": childPath
		})
	}

	function childPaths() {
		window.revision; // re-evaluate after every committed edit
		var result = [];
		var children = (root.node && root.node.children)
			? root.node.children : [];
		for (var i = 0; i < children.length; ++i) {
			result.push(root.path + "/" + i);
		}
		return result;
	}

	Component {
		id: containerComponent

		Rectangle {
			id: box
			readonly property bool selected: window.selectedPath === root.path
			readonly property bool isPage: root.node
				&& root.node.type === undefined
			property bool dragHover: false

			radius: 4
			color: dragHover ? "#3d4f68"
				: (selected ? "#2c3e58" : "#252e38")
			border.color: dragHover ? "#7fc1ff"
				: (selected ? "#4f9cf9" : "#3a4754")
			border.width: (selected || dragHover) ? 2 : 1
			implicitWidth: flowLoader.implicitWidth + 12
			implicitHeight: flowLoader.implicitHeight + header.height + 14
			// The page root fills its loader (the canvas sizes that loader to
			// the whole page) so the page DropArea covers empty areas too;
			// nested containers take drop precedence as their DropAreas sit
			// deeper in the scene. Non-page containers keep content sizing.
			width: isPage && parent ? parent.width : implicitWidth
			height: isPage && parent ? parent.height : implicitHeight

			// Extract the drop point regardless of whether this Qt version
			// exposes DragEvent.position as a property or a method.
			function dropPosition(drop) {
				var p = drop.position;
				if (typeof p === "function") {
					p = drop.position();
				}
				if (p && p.x !== undefined) {
					return p;
				}
				return Qt.point(drop.x !== undefined ? drop.x : 0,
					drop.y !== undefined ? drop.y : 0);
			}

			function insertionIndex(x, y) {
				var flow = flowLoader.item;
				if (!flow) {
					return 0;
				}
				var count = 0;
				for (var i = 0; i < flow.children.length; ++i) {
					var child = flow.children[i];
					if (!child || child.width <= 0 || child.height <= 0) {
						continue;
					}
					var center = child.mapToItem(
						box, child.width / 2, child.height / 2);
					if (root.horizontal ? x < center.x : y < center.y) {
						return count;
					}
					++count;
				}
				return count;
			}

			Rectangle {
				id: header
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				height: 18
				color: "transparent"
				Label {
					anchors.left: parent.left
					anchors.leftMargin: 5
					anchors.verticalCenter: parent.verticalCenter
					text: root.node ? (root.node.type || "page") : ""
					font.pixelSize: 10
					color: "#7f8c99"
				}
				MouseArea {
					id: headerDrag
					anchors.fill: parent
					drag.target: box.isPage ? undefined : boxGhost
					onPressed: window.selectedPath = root.path
					onReleased: {
						boxGhost.Drag.drop();
						boxGhost.x = 0;
						boxGhost.y = 0;
					}
				}
			}

			Loader {
				id: flowLoader
				anchors.left: parent.left
				anchors.top: header.bottom
				anchors.margins: 6
				anchors.topMargin: 2
				sourceComponent: root.horizontal ? rowFlow : columnFlow
			}

			Component {
				id: rowFlow
				RowLayout {
					spacing: 6
					Repeater {
						model: root.childPaths()
						delegate: NodeChild { childPath: modelData }
					}
					Label {
						visible: !root.node || !root.node.children
							|| root.node.children.length === 0
						text: "拖拽控件到这里"
						color: "#5a6672"
						font.pixelSize: 11
					}
				}
			}

			Component {
				id: columnFlow
				ColumnLayout {
					spacing: 6
					Repeater {
						model: root.childPaths()
						delegate: NodeChild { childPath: modelData }
					}
					Label {
						visible: !root.node || !root.node.children
							|| root.node.children.length === 0
						text: "拖拽控件到这里"
						color: "#5a6672"
						font.pixelSize: 11
					}
				}
			}

			// Drag ghost: the actual drag target. It stays instantiated (opacity
			// 0 when idle) so its scene rectangle is always valid; toggling
			// `visible` on release made Drag.drop() lose the drop position.
			Rectangle {
				id: boxGhost
				// Payload read by the DropArea via drag.source.
				readonly property string nodePath: root.path
				width: box.width
				height: box.height
				opacity: headerDrag.drag.active ? 0.75 : 0
				radius: 4
				color: "#3d4f68"
				border.color: "#7fc1ff"
				border.width: 2
				z: 1000
				Label {
					anchors.left: parent.left
					anchors.leftMargin: 5
					anchors.top: parent.top
					anchors.topMargin: 2
					text: root.node ? (root.node.type || "page") : ""
					font.pixelSize: 10
					color: "#cfe6ff"
				}

				Drag.active: !box.isPage && headerDrag.drag.active
				Drag.hotSpot.x: 20
				Drag.hotSpot.y: 8
				Drag.keys: ["visiondesigner-node"]
			}

			DropArea {
				id: boxDrop
				anchors.fill: parent
				// Every container's DropArea sits BELOW its flow (z:-1): it is a
				// sibling of flowLoader inside the box, so a default z would cover
				// nested row/column containers and swallow their drops. This makes
				// the deepest container under the cursor win at any nesting depth,
				// while the box's own empty areas still receive drops.
				z: -1
				keys: ["visiondesigner-control", "visiondesigner-node"]
				// Drive the hover highlight from containsDrag; checking
				// drag.formats inside onEntered is unreliable for internal drags.
				onContainsDragChanged: {
					box.dragHover = containsDrag;
					if (containsDrag) {
						var r = boxDrop.mapToItem(null, 0, 0);
						window.dropDiag = "hover path=" + root.path
							+ " rect=(" + Math.round(r.x) + ","
							+ Math.round(r.y) + " "
							+ Math.round(boxDrop.width) + "x"
							+ Math.round(boxDrop.height) + ")"
							+ " page=" + box.isPage;
					}
				}
				onDropped: function(drop) {
					box.dragHover = false;
					// Internal drags carry no mimeData; the payload lives on the
					// drag source item (the ghost), reached via the DropArea's
					// drag.source (DragEvent has no `drag` property of its own).
					var source = boxDrop.drag.source;
					var typeName = source && source.controlType !== undefined
						? source.controlType : "";
					var sourcePath = source && source.nodePath !== undefined
						? source.nodePath : "";
					window.dropDiag = "onDropped path=" + root.path
						+ " type=" + (typeName || "-")
						+ " src=" + (sourcePath || "-");
					if (!typeName && !sourcePath) {
						return;
					}
					// DragEvent coordinates: `position` is a point property on
					// some Qt versions and a method on others; fall back to x/y.
					var pos = box.dropPosition(drop);
					var index = box.insertionIndex(pos.x, pos.y);
					if (typeName) {
						window.insertNode(root.path, index,
							window.makeNode(typeName));
					} else {
						window.moveNode(sourcePath, root.path, index);
					}
					drop.accept();
				}
			}
		}
	}

	Component {
		id: leafComponent

		Rectangle {
			id: leaf
			readonly property bool selected: window.selectedPath === root.path
			property bool dragHover: false

			radius: 4
			color: dragHover ? "#3d4f68"
				: (selected ? "#2c3e58" : "#2c3540")
			border.color: dragHover ? "#7fc1ff"
				: (selected ? "#4f9cf9" : "#55606c")
			border.width: (selected || dragHover) ? 2 : 1
			implicitWidth: 160
			implicitHeight: 66

			Column {
				anchors.fill: parent
				anchors.margins: 6
				spacing: 2
				Label {
					text: root.node ? root.node.type : ""
					color: "#9ecfff"
					font.pixelSize: 10
				}
				Label {
					width: parent.width
					text: root.node
						? ((root.node.title || root.node.label) || "") : ""
					color: "#e0e0e0"
					elide: Text.ElideRight
				}
				Label {
					width: parent.width
					text: root.node
						? (root.node.endpoint || "<未绑定端点>") : ""
					color: root.node && root.node.endpoint
						? "#7fd08f" : "#ff8080"
					font.pixelSize: 10
					elide: Text.ElideRight
				}
			}

			// Drag ghost (see the container ghost): opacity-controlled so the
			// scene rectangle stays valid through the drop.
			Rectangle {
				id: leafGhost
				// Payload read by the DropArea via drag.source.
				readonly property string nodePath: root.path
				width: leaf.width
				height: leaf.height
				opacity: leafDrag.drag.active ? 0.75 : 0
				radius: 4
				color: "#3d4f68"
				border.color: "#7fc1ff"
				border.width: 2
				z: 1000
				Column {
					anchors.fill: parent
					anchors.margins: 6
					spacing: 2
					Label {
						text: root.node ? root.node.type : ""
						color: "#9ecfff"
						font.pixelSize: 10
					}
					Label {
						width: parent.width
						text: root.node
							? ((root.node.title || root.node.label) || "") : ""
						color: "#e0e0e0"
						elide: Text.ElideRight
					}
				}

				Drag.active: leafDrag.drag.active
				Drag.hotSpot.x: width / 2
				Drag.hotSpot.y: height / 2
				Drag.keys: ["visiondesigner-node"]
			}

			MouseArea {
				id: leafDrag
				anchors.fill: parent
				drag.target: leafGhost
				onPressed: window.selectedPath = root.path
				onReleased: {
					leafGhost.Drag.drop();
					leafGhost.x = 0;
					leafGhost.y = 0;
				}
			}
		}
	}
}
