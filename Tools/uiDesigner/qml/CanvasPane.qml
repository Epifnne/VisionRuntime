import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Editing canvas: one tab per profile page; each page renders its control
// tree through EditorNode. The page background carries a 16px alignment grid.
Item {
	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		RowLayout {
			Layout.fillWidth: true
			Layout.margins: 6
			spacing: 4

			TabBar {
				id: pageTabs
				Layout.fillWidth: true
				// Do not reset selectedPath here: changing it on every commit
				// (which re-triggers the Repeater model) made the canvas jump
				// back to the page while the user was typing in the product
				// panel.
				Repeater {
					model: {
						window.revision;
						return (window.doc.ui && window.doc.ui.pages)
							? window.doc.ui.pages : [];
					}
					delegate: TabButton {
						text: modelData.title || modelData.id || "page"
					}
				}
			}
			ToolButton {
				text: "＋页面"
				onClicked: window.addPage()
			}
			ToolButton {
				text: "－页面"
				enabled: window.doc.ui && window.doc.ui.pages
					&& window.doc.ui.pages.length > 0
				onClicked: window.removePage(pageTabs.currentIndex)
			}
		}

		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			// No clip here: the drag ghost must follow the cursor beyond the
			// page bounds.
			currentIndex: Math.min(pageTabs.currentIndex,
				Math.max(0, count - 1))

			Repeater {
				model: {
					window.revision;
					return (window.doc.ui && window.doc.ui.pages)
						? window.doc.ui.pages : [];
				}
				delegate: Item {
					property int pageIndex: index

					Rectangle {
						anchors.fill: parent
						color: "#1e242c"

						Canvas {
							anchors.fill: parent
							onPaint: {
								var ctx = getContext("2d");
								ctx.reset();
								ctx.fillStyle = "#28303a";
								for (var x = 8; x < width; x += 16) {
									for (var y = 8; y < height; y += 16) {
										ctx.fillRect(x, y, 1, 1);
									}
								}
							}
						}

						MouseArea {
							anchors.fill: parent
							onClicked: window.selectedPath = String(pageIndex)
						}

						// Controls are layout-sized, not absolutely positioned. The
						// page root fills the whole page so its DropArea covers empty
						// areas; nested containers still take precedence because their
						// DropAreas sit above the page box.
						EditorNode {
							id: pageEditor
							anchors.fill: parent
							anchors.margins: 12
							path: String(pageIndex)
						}
					}
				}
			}
		}
	}
}
