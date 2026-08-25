// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs

import org.kde.kirigami as Kirigami
import org.kde.fretwork

Kirigami.ApplicationWindow {
    id: root

    property string initialFile: ""

    /**
     * The mixer has no per-track model, because the player's state lives in
     * atomics that the audio thread reads. Bumping this on every change is
     * what makes the bindings below re-read it -- see the comma expressions,
     * which exist to declare a dependency QML cannot otherwise see.
     */
    property int mixerRevision: 0

    title: {
        if (!session.hasScore) {
            return i18n("Fretwork")
        }
        const name = session.artist.length > 0
            ? i18n("%1 — %2", session.title, session.artist)
            : session.title
        // The dot every editor uses for work that is not written down yet.
        return session.modified ? i18n("• %1", name) : name
    }

    // No page header: the toolbar above is the only chrome this wants, and
    // Kirigami's default leaves an empty band where a page title would go.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    width: Kirigami.Units.gridUnit * 56
    height: Kirigami.Units.gridUnit * 40
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 20

    Session {
        id: session
    }

    Connections {
        target: session
        function onMixerChanged() {
            root.mixerRevision++
        }
    }

    Component.onCompleted: {
        if (initialFile.length > 0) {
            session.open(initialFile)
        }
    }

    Dialogs.FileDialog {
        id: fileDialog
        title: i18n("Open a Guitar Pro file")
        nameFilters: [i18n("Guitar Pro 7 and 8 files (*.gp)"), i18n("All files (*)")]
        onAccepted: session.open(selectedFile)
    }

    // ---- the transport ----

    header: QQC2.ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            QQC2.ToolButton {
                icon.name: "document-open"
                text: i18n("Open")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.text: i18n("Open a Guitar Pro file")
                QQC2.ToolTip.visible: hovered
                onClicked: fileDialog.open()
            }

            QQC2.ToolButton {
                icon.name: "edit-undo"
                enabled: session.canUndo
                display: QQC2.AbstractButton.IconOnly
                text: session.canUndo ? i18n("Undo %1", session.undoText) : i18n("Undo")
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: session.undo()
            }

            QQC2.ToolButton {
                icon.name: "edit-redo"
                enabled: session.canRedo
                display: QQC2.AbstractButton.IconOnly
                text: session.canRedo ? i18n("Redo %1", session.redoText) : i18n("Redo")
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: session.redo()
            }

            Kirigami.Separator {
                Layout.fillHeight: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
            }

            QQC2.ToolButton {
                icon.name: session.playing ? "media-playback-pause" : "media-playback-start"
                enabled: session.canPlay
                display: QQC2.AbstractButton.IconOnly
                text: session.playing ? i18n("Pause") : i18n("Play")
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: session.playing ? session.pause() : session.play()
            }

            QQC2.ToolButton {
                icon.name: "media-playback-stop"
                enabled: session.canPlay
                display: QQC2.AbstractButton.IconOnly
                text: i18n("Stop")
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: session.stop()
            }

            QQC2.Label {
                text: session.clock(session.position)
                font.family: "monospace"
                opacity: 0.8
            }

            QQC2.Slider {
                Layout.fillWidth: true
                enabled: session.canPlay && session.length > 0
                from: 0
                to: Math.max(1, session.length)
                // While it is being dragged the playhead must not fight back.
                value: pressed ? value : session.position
                onMoved: session.seek(value)
            }

            QQC2.Label {
                text: session.clock(session.length)
                font.family: "monospace"
                opacity: 0.8
            }

            Kirigami.Separator {
                Layout.fillHeight: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
            }

            QQC2.ComboBox {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                model: session.trackNames
                enabled: session.trackCount > 0
                currentIndex: session.currentTrack
                onActivated: session.currentTrack = currentIndex
                QQC2.ToolTip.text: i18n("Which track is shown")
                QQC2.ToolTip.visible: hovered
            }
        }
    }

    // ---- the score, and the mixer beside it ----

    pageStack.initialPage: Kirigami.Page {
        padding: 0

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScoreView {
                    id: view
                    anchors.fill: parent
                    session: session
                    focus: true

                    // Typing goes to the score, which is what a tablature
                    // editor is: numbers are notes, and the arrows are a caret.
                    Keys.onPressed: event => {
                        if (!session.hasScore) {
                            return
                        }
                        switch (event.key) {
                        case Qt.Key_Left:
                            session.moveCursor(event.modifiers & Qt.ControlModifier
                                               ? "barBack" : "left"); break
                        case Qt.Key_Right:
                            session.moveCursor(event.modifiers & Qt.ControlModifier
                                               ? "barForward" : "right"); break
                        case Qt.Key_Up:    session.moveCursor("up"); break
                        case Qt.Key_Down:  session.moveCursor("down"); break
                        case Qt.Key_Home:  session.moveCursor("start"); break
                        case Qt.Key_End:   session.moveCursor("end"); break
                        case Qt.Key_Delete:
                        case Qt.Key_Backspace: session.clearNote(); break
                        case Qt.Key_Plus:
                        case Qt.Key_Equal: session.transposeNote(1); break
                        case Qt.Key_Minus: session.transposeNote(-1); break
                        case Qt.Key_Space:
                            session.playing ? session.pause() : session.play(); break
                        default:
                            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                                session.typeDigit(event.key - Qt.Key_0)
                            } else {
                                return
                            }
                        }
                        event.accepted = true
                    }

                    WheelHandler {
                        target: null
                        onWheel: event => {
                            view.scrollY -= event.angleDelta.y
                            // Touching the wheel means the reader is looking
                            // somewhere of their own choosing.
                            view.followPlayhead = false
                        }
                    }
                }

                QQC2.ScrollBar {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    orientation: Qt.Vertical
                    visible: view.contentHeight > view.height
                    size: view.height / Math.max(1, view.contentHeight)
                    position: view.scrollY / Math.max(1, view.contentHeight)
                    onPositionChanged: {
                        if (pressed) {
                            view.scrollY = position * view.contentHeight
                            view.followPlayhead = false
                        }
                    }
                }

                QQC2.ToolButton {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Kirigami.Units.largeSpacing
                    icon.name: "followmouse"
                    checkable: true
                    checked: view.followPlayhead
                    visible: session.hasScore
                    text: i18n("Follow the playhead")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onToggled: view.followPlayhead = checked
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 8
                    visible: !session.hasScore
                    icon.name: "music-note-16th"
                    text: i18n("No score open")
                    explanation: i18n("Open a Guitar Pro 7 or 8 file to read it and hear it.")
                    helpfulAction: Kirigami.Action {
                        icon.name: "document-open"
                        text: i18n("Open…")
                        onTriggered: fileDialog.open()
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillHeight: true
                visible: session.hasScore
            }

            // The mixer. Every track has a synth of its own, so soloing is not
            // a re-render -- it is one atomic store away from being heard.
            QQC2.ScrollView {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                Layout.fillHeight: true
                visible: session.hasScore
                clip: true

                ColumnLayout {
                    width: Kirigami.Units.gridUnit * 14
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        level: 4
                        text: i18n("Mixer")
                        Layout.margins: Kirigami.Units.largeSpacing
                        Layout.bottomMargin: 0
                    }

                    Repeater {
                        model: session.trackCount

                        delegate: ColumnLayout {
                            required property int index

                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            Layout.rightMargin: Kirigami.Units.largeSpacing
                            Layout.topMargin: Kirigami.Units.smallSpacing
                            spacing: 0

                            // The comma is not a mistake: it makes this binding
                            // depend on mixerRevision, which is the only thing
                            // that changes when another track is soloed.
                            opacity: (root.mixerRevision, session.isAudible(index)) ? 1.0 : 0.4

                            RowLayout {
                                Layout.fillWidth: true

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: session.trackNames[index]
                                    elide: Text.ElideRight
                                    font.bold: index === session.currentTrack
                                }

                                QQC2.ToolButton {
                                    text: i18n("S")
                                    checkable: true
                                    checked: (root.mixerRevision, session.isSolo(index))
                                    implicitWidth: Kirigami.Units.gridUnit * 1.8
                                    QQC2.ToolTip.text: i18n("Hear only this")
                                    QQC2.ToolTip.visible: hovered
                                    onToggled: session.setSolo(index, checked)
                                }

                                QQC2.ToolButton {
                                    text: i18n("M")
                                    checkable: true
                                    checked: (root.mixerRevision, session.isMuted(index))
                                    implicitWidth: Kirigami.Units.gridUnit * 1.8
                                    QQC2.ToolTip.text: i18n("Silence this")
                                    QQC2.ToolTip.visible: hovered
                                    onToggled: session.setMuted(index, checked)
                                }
                            }

                            QQC2.Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: 2
                                value: session.gain(index)
                                onMoved: session.setGain(index, value)
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }

    footer: QQC2.ToolBar {
        visible: session.status.length > 0
        contentItem: RowLayout {
            Kirigami.Icon {
                source: "dialog-information"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: session.status
                elide: Text.ElideRight
                opacity: 0.8
            }
        }
    }
}
