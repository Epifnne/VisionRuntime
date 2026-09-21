import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Image view bound to a Stream endpoint. Resolves the endpoint name to a
// sourceId via the binding layer, then pulls frames from the "frames"
// QQuickImageProvider. Displays the measured receive frame rate.
GroupBox {
	id: root

	property string endpoint: ""
	property string heading: ""
	property int sourceId: -1
	property int frameCount: 0
	property int lastSecondCount: 0
	property real fps: 0.0

	implicitWidth: 320
	implicitHeight: 240
	title: (root.heading || root.endpoint) + "  [" + root.fps.toFixed(1) + " fps]"

	function resolveSource() {
		root.sourceId = serviceClient.streamSourceId(root.endpoint);
	}

	Component.onCompleted: resolveSource()

	Connections {
		target: serviceClient
		function onRegistryChanged() { root.resolveSource(); }
		function onFrameReceived(sourceId, image) {
			if (sourceId === root.sourceId && root.sourceId >= 0) {
				root.frameCount += 1;
				frameImage.source = "image://frames/" + sourceId
					+ "?n=" + root.frameCount;
			}
		}
	}

	Timer {
		interval: 1000
		running: true
		repeat: true
		onTriggered: {
			root.fps = root.frameCount - root.lastSecondCount;
			root.lastSecondCount = root.frameCount;
		}
	}

	Rectangle {
		anchors.fill: parent
		color: "#222222"

		Image {
			id: frameImage
			anchors.fill: parent
			fillMode: Image.PreserveAspectFit
			cache: false
			source: ""

			property int retries: 0

			onStatusChanged: {
				if (status === Image.Error && root.sourceId >= 0
						&& retries < 10) {
					retries += 1;
					retryTimer.start();
				} else if (status === Image.Ready) {
					retries = 0;
				}
			}
		}

		// The first frame can race the provider update (image request may be
		// issued before the provider slot runs); retry with a fresh query.
		Timer {
			id: retryTimer
			interval: 50
			onTriggered: {
				frameImage.source = "image://frames/" + root.sourceId
					+ "?n=" + root.frameCount + "r" + frameImage.retries;
			}
		}

		Text {
			anchors.centerIn: parent
			visible: root.frameCount === 0
			color: "#888888"
			text: root.sourceId >= 0
				? "waiting for frames (" + root.endpoint + ")"
				: "stream not found: " + root.endpoint
		}
	}
}
