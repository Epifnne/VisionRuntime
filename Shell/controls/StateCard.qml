import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// State card bound to a State endpoint. Shows a status light (derived from
// a "state" field when the payload is JSON) plus the payload content.
GroupBox {
	id: root

	property string endpoint: ""
	property bool compact: false
	property string payload: ""
	property var parsed: {
		try { return JSON.parse(root.payload); } catch (e) { return null; }
	}
	property color lightColor: {
		if (root.parsed && (root.parsed.lastError || root.parsed.error)) {
			return "#e53935";
		}
		if (root.parsed && root.parsed.state !== undefined) {
			switch (String(root.parsed.state)) {
			case "running": return "#4caf50";
			case "idle":
			case "configuring":
			case "stopping": return "#ffb300";
			default: return "#e53935";
			}
		}
		return root.payload ? "#4caf50" : "#9e9e9e";
	}
	property string displayText: {
		if (root.parsed) {
			if (root.parsed.state !== undefined) {
				var text = String(root.parsed.state);
				var error = root.parsed.lastError || root.parsed.error;
				if (root.compact) {
					var labels = { running: "检测中", idle: "待机", configuring: "准备中", stopping: "停止中" };
					return (labels[text] || text) + (error ? " · 异常" : "");
				}
				if (error) {
					text += " (" + error + ")";
				}
				return text;
			}
			return JSON.stringify(root.parsed, null, 2);
		}
		return root.payload || "no data";
	}

	implicitWidth: 200

	function refresh() {
		var snapshot = serviceClient.stateSnapshot(root.endpoint);
		if (snapshot) {
			root.payload = snapshot;
		}
	}

	Component.onCompleted: refresh()

	Connections {
		target: serviceClient
		function onStateChanged(name, payload) {
			if (name === root.endpoint) {
				root.payload = payload;
			}
		}
	}

	RowLayout {
		anchors.fill: parent
		spacing: 8

		Rectangle {
			width: 12
			height: 12
			radius: 6
			color: root.lightColor
		}

		Label {
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			text: root.displayText
			wrapMode: root.compact ? Text.NoWrap : Text.Wrap
			maximumLineCount: root.compact ? 1 : 3
			elide: Text.ElideRight
			clip: true
			font.family: "Consolas"
			HoverHandler { id: statusHover }
			ToolTip.visible: statusHover.hovered
			ToolTip.text: root.payload
		}
	}
}
