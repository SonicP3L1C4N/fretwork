// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs

import org.kde.kirigami as Kirigami
import org.kde.fretwork

/**
 * The window: ink chrome around a page of paper.
 *
 * The colours are the application's own rather than the desktop's, and every
 * one of them is in `Ink.qml`. That is a deliberate departure from a KDE
 * application's usual manners, and it is the one a PDF reader and an image
 * editor make too: the thing in the middle is a document, and a document that
 * changed colour with the desktop theme would be a different document. The
 * chrome is dark so that the paper is the brightest thing in the window.
 */
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

    // Which panels are open, remembered between runs: somebody who works with
    // the mixer closed should not have to close it every morning.
    Settings {
        id: panels
        category: "Panels"
        property bool tracks: true
        property bool mixer: true
        property bool status: true
        property bool bars: true
        // Off by default, and not out of tidiness: this is the one panel that
        // opens an audio input, and a program that started listening to the
        // room because it was last left listening to the room would be a
        // program nobody trusts twice.
        property bool tuner: false
    }

    Session {
        id: session
    }

    /**
     * The tuner, listening only while its panel is open.
     *
     * The strings come from the track on the page, which is the whole point of
     * a tuner living in here rather than on a phone: a score in drop C asks
     * for a C and says which peg. With nothing open it is a guitar in standard
     * tuning, which is what somebody holding one has until told otherwise.
     */
    Tuning {
        id: tuner
        listening: panels.tuner
        strings: session.hasScore ? session.stringPitches(session.currentTrack) : []
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
        title: i18n("Open a score")
        nameFilters: [i18n("Scores (*.fw *.gp)"), i18n("Fretwork scores (*.fw)"),
                      i18n("Guitar Pro 7 and 8 files (*.gp)"), i18n("All files (*)")]
        onAccepted: session.open(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveDialog
        title: i18n("Save the score")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "fw"
        nameFilters: [i18n("Fretwork scores (*.fw)")]
        onAccepted: session.saveAs(selectedFile)
    }

    // Saving over an imported Guitar Pro file is not offered: Fretwork does
    // not write that format, so the first save of an import asks where to put
    // a file of its own.
    function saveScore() {
        if (!session.save()) {
            saveDialog.open()
        }
    }

    Shortcut {
        sequences: [StandardKey.Save]
        onActivated: root.saveScore()
    }

    Shortcut {
        sequences: [StandardKey.SaveAs]
        onActivated: saveDialog.open()
    }

    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: fileDialog.open()
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        onActivated: session.copy()
    }

    Shortcut {
        sequences: [StandardKey.Cut]
        onActivated: session.cut()
    }

    Shortcut {
        sequences: [StandardKey.Paste]
        onActivated: session.paste()
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: session.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo]
        onActivated: session.redo()
    }

    // B for bar. Not Insert and Ctrl+Insert, which would read better and are
    // already spoken for: Ctrl+Insert is a copy and Shift+Insert a paste on
    // this desktop, whatever else a program binds them to.
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: session.appendBar()
    }

    Shortcut {
        sequence: "Ctrl+Shift+B"
        onActivated: session.insertBar()
    }

    Shortcut {
        sequence: "Ctrl+Shift+Delete"
        onActivated: session.deleteBar()
    }

    // ---- the controls the chrome is made of ----

    /**
     * A square button on ink: an outline, or a filled magenta one.
     *
     * Its own component rather than a styled ToolButton, because the desktop
     * style draws a button the way the desktop wants one -- which is the one
     * thing this window is deliberately not doing.
     */
    component ChromeButton: QQC2.AbstractButton {
        id: chromeButton

        property bool filled: false
        property bool outlined: true

        implicitWidth: Ink.control
        implicitHeight: Ink.control
        hoverEnabled: true
        opacity: enabled ? 1 : 0.45
        // Typing belongs to the score. A toolbar button that took the keyboard
        // when it was clicked would stop the next number reaching the page.
        focusPolicy: Qt.NoFocus

        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered && text.length > 0
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        background: Rectangle {
            radius: Ink.radius
            color: chromeButton.filled
                ? (chromeButton.down || chromeButton.hovered ? Ink.accentHover : Ink.accent)
                : (chromeButton.down || chromeButton.hovered
                    ? Qt.rgba(0.95, 0.95, 0.95, 0.12) : "transparent")
            border.width: chromeButton.outlined && !chromeButton.filled ? 1 : 0
            border.color: Ink.edge
        }

        contentItem: Kirigami.Icon {
            source: chromeButton.icon.name
            isMask: true
            color: Ink.paper
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
        }
    }

    /** A word on the toolbar that turns a panel on and off. */
    component ChromeToggle: QQC2.AbstractButton {
        id: chromeToggle

        checkable: true
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitHeight: Ink.control
        implicitWidth: toggleLabel.implicitWidth + Kirigami.Units.largeSpacing * 2

        background: Rectangle {
            radius: Ink.radius
            color: chromeToggle.checked
                ? (chromeToggle.hovered ? Ink.accentHover : Ink.accent)
                : (chromeToggle.hovered ? Qt.rgba(0.95, 0.95, 0.95, 0.12) : "transparent")
            border.width: chromeToggle.checked ? 0 : 1
            border.color: Ink.edge
        }

        contentItem: QQC2.Label {
            id: toggleLabel
            text: chromeToggle.text
            color: Ink.paper
            opacity: chromeToggle.enabled ? 1 : 0.45
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    /** Every slider in the window: a thin track with the accent filling it. */
    component InkSlider: QQC2.Slider {
        id: inkSlider

        property color groove: Ink.line

        implicitHeight: Ink.grip + 4

        background: Rectangle {
            x: inkSlider.leftPadding
            y: inkSlider.topPadding + inkSlider.availableHeight / 2 - height / 2
            width: inkSlider.availableWidth
            height: Ink.groove
            radius: height / 2
            color: inkSlider.groove

            Rectangle {
                width: inkSlider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: inkSlider.enabled ? Ink.accent : Ink.quiet
            }
        }

        handle: Rectangle {
            x: inkSlider.leftPadding
               + inkSlider.visualPosition * (inkSlider.availableWidth - width)
            y: inkSlider.topPadding + inkSlider.availableHeight / 2 - height / 2
            width: Ink.grip
            height: Ink.grip
            radius: width / 2
            color: inkSlider.pressed ? Ink.accentHover
                                     : (inkSlider.enabled ? Ink.accent : Ink.quiet)
        }
    }

    /**
     * One track, as a row: what it is, what it is called, and whether it is
     * the one on the page.
     *
     * A list of these down the side rather than a dropdown, because switching
     * between the guitar, the bass and the drums is the thing a person reading
     * a tab does most often, and a menu makes them look for it every time. The
     * drawing is what makes it a glance rather than a read. Down the side and
     * not across the top because a score has as many parts as it has, and a row
     * of them runs out of window while a list does not.
     */
    component TrackRow: QQC2.AbstractButton {
        id: trackRow

        property bool current: false
        property string glyph: ""

        implicitHeight: Kirigami.Units.gridUnit * 2.4
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        background: Rectangle {
            radius: Ink.radius
            color: trackRow.current
                ? (trackRow.hovered ? Ink.accentHover : Ink.accent)
                : (trackRow.hovered ? Ink.rule : "transparent")
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                Layout.leftMargin: Kirigami.Units.smallSpacing
                source: trackRow.glyph
                isMask: true
                color: trackRow.current ? Ink.paper : Ink.ink
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: trackRow.text
                color: trackRow.current ? Ink.paper : Ink.ink
                elide: Text.ElideRight
                font.weight: trackRow.current ? Font.DemiBold : Font.Normal
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    /** S and M: small, square, and lit when they are doing something. */
    component MixerButton: QQC2.AbstractButton {
        id: mixerButton

        property color litFill: Ink.accent

        checkable: true
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitWidth: Ink.smallControl
        implicitHeight: Ink.smallControl

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        background: Rectangle {
            radius: Ink.radius
            color: mixerButton.checked
                ? mixerButton.litFill
                : (mixerButton.hovered ? Qt.rgba(0.13, 0.12, 0.11, 0.08) : "transparent")
            border.width: mixerButton.checked ? 0 : 1
            border.color: Qt.rgba(0.13, 0.12, 0.11, 0.16)
        }

        contentItem: QQC2.Label {
            text: mixerButton.text
            color: mixerButton.checked ? Ink.paper : Ink.ink
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ---- the transport ----

    header: Rectangle {
        implicitHeight: Ink.control + Kirigami.Units.largeSpacing * 2
        color: Ink.ink

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
        Kirigami.Theme.backgroundColor: Ink.ink
        Kirigami.Theme.textColor: Ink.paper
        Kirigami.Theme.highlightColor: Ink.accent

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.largeSpacing * 2
            anchors.rightMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.smallSpacing * 2

            ChromeButton {
                icon.name: "document-open"
                text: i18n("Open a Guitar Pro file")
                onClicked: fileDialog.open()
            }

            ChromeButton {
                icon.name: "document-save"
                enabled: session.hasScore && (session.modified || !session.savesInPlace)
                text: session.savesInPlace ? i18n("Save") : i18n("Save as…")
                onClicked: root.saveScore()
            }

            Item { implicitWidth: Kirigami.Units.smallSpacing }

            ChromeButton {
                icon.name: "edit-undo"
                outlined: false
                enabled: session.canUndo
                text: session.canUndo ? i18n("Undo %1", session.undoText) : i18n("Undo")
                onClicked: session.undo()
            }

            ChromeButton {
                icon.name: "edit-redo"
                outlined: false
                enabled: session.canRedo
                text: session.canRedo ? i18n("Redo %1", session.redoText) : i18n("Redo")
                onClicked: session.redo()
            }

            Item { implicitWidth: Kirigami.Units.smallSpacing }

            ChromeButton {
                icon.name: session.playing ? "media-playback-pause" : "media-playback-start"
                filled: true
                enabled: session.canPlay
                text: session.playing ? i18n("Pause") : i18n("Play")
                onClicked: session.playing ? session.pause() : session.play()
            }

            ChromeButton {
                icon.name: "media-playback-stop"
                enabled: session.canPlay
                text: i18n("Stop")
                onClicked: session.stop()
            }

            // With the transport rather than with the panel toggles at the
            // other end: a click is a thing the playback does, not a thing the
            // window shows. Its level is in the mixer, where levels live.
            ChromeToggle {
                text: i18n("Click")
                enabled: session.hasScore
                checked: session.click
                onToggled: session.click = checked
                QQC2.ToolTip.text: i18n("A metronome on every beat")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            /**
             * How fast, and the one place to change it.
             *
             * Beside the transport because that is what it governs, and a
             * field rather than a pair of nudge buttons because a tempo is a
             * number somebody knows: 96 is typed, not arrived at by pressing
             * up eleven times. It reads the tempo the caret's bar is played
             * at, and writing in it sets one from that bar on.
             *
             * Accented while the bar carries a change of its own and quiet
             * while it is living under an earlier one, because those two look
             * identical and behave differently when they are edited.
             */
            RowLayout {
                spacing: Kirigami.Units.smallSpacing / 2
                visible: session.hasScore

                QQC2.Label {
                    text: "\u2669"
                    color: session.tempoWrittenHere ? Ink.accentOnInk : Ink.faint
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                }

                QQC2.TextField {
                    id: tempoField

                    implicitWidth: Kirigami.Units.gridUnit * 2.6
                    horizontalAlignment: Text.AlignHCenter
                    color: session.tempoWrittenHere ? Ink.paper : Ink.faint
                    font.features: ({ "tnum": 1 })
                    // Whole numbers only, and inside the range the editor will
                    // accept, so the field cannot ask for something refused.
                    validator: IntValidator { bottom: 20; top: 400 }
                    text: Math.round(session.tempoHere)

                    QQC2.ToolTip.text: session.tempoWrittenHere
                        ? i18n("The tempo from this bar on")
                        : i18n("The tempo here, set in an earlier bar")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    background: Rectangle {
                        radius: Ink.radius
                        color: tempoField.activeFocus ? Ink.well : "transparent"
                        border.width: 1
                        border.color: tempoField.activeFocus ? Ink.accent : Ink.edge
                    }

                    onAccepted: {
                        session.setTempoHere(Number(text))
                        // The score gets the keyboard back, or the next fret
                        // number typed would land in here.
                        view.forceActiveFocus()
                    }
                    // Whatever was half typed is dropped rather than applied:
                    // clicking away from a field is not agreeing with it.
                    onActiveFocusChanged: if (!activeFocus) {
                        text = Qt.binding(() => Math.round(session.tempoHere))
                    }
                }
            }

            /**
             * What the bar is in, and the one place to change it.
             *
             * One field with the slash in it rather than two boxes, because a
             * time signature is one thing a musician says in one breath.
             * Writing in it sets the signature from the caret's bar until the
             * next change, which is what "3/4 from here" means to anybody who
             * has written it on paper.
             */
            QQC2.TextField {
                id: timeField

                visible: session.hasScore
                implicitWidth: Kirigami.Units.gridUnit * 2.8
                horizontalAlignment: Text.AlignHCenter
                color: session.timeWrittenHere ? Ink.paper : Ink.faint
                font.features: ({ "tnum": 1 })
                validator: RegularExpressionValidator {
                    regularExpression: /[0-9]{1,2}\/[0-9]{1,2}/
                }
                text: session.timeHere

                QQC2.ToolTip.text: session.timeWrittenHere
                    ? i18n("The time signature from this bar on")
                    : i18n("The time signature here, written in an earlier bar")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                background: Rectangle {
                    radius: Ink.radius
                    color: timeField.activeFocus ? Ink.well : "transparent"
                    border.width: 1
                    border.color: timeField.activeFocus ? Ink.accent : Ink.edge
                }

                onAccepted: {
                    session.setTimeHere(text)
                    view.forceActiveFocus()
                }
                onActiveFocusChanged: if (!activeFocus) {
                    text = Qt.binding(() => session.timeHere)
                }
            }

            QQC2.Label {
                text: session.clock(session.position)
                color: Ink.paper
                // Tabular figures, so a running clock does not shuffle sideways
                // as the digits change under it.
                font.features: ({ "tnum": 1 })
            }

            InkSlider {
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 6
                enabled: session.canPlay && session.length > 0
                from: 0
                to: Math.max(1, session.length)
                // While it is being dragged the playhead must not fight back.
                value: pressed ? value : session.position
                onMoved: session.seek(value)
            }

            QQC2.Label {
                text: session.clock(session.length)
                color: Ink.faint
                font.features: ({ "tnum": 1 })
            }

            Item { implicitWidth: Kirigami.Units.smallSpacing }

            ChromeToggle {
                text: i18n("Tracks")
                enabled: session.hasScore
                checked: panels.tracks
                onToggled: panels.tracks = checked
            }

            ChromeToggle {
                text: i18n("Bars")
                enabled: session.hasScore
                checked: panels.bars
                onToggled: panels.bars = checked
            }

            ChromeToggle {
                text: i18n("Mixer")
                enabled: session.hasScore
                checked: panels.mixer
                onToggled: panels.mixer = checked
            }

            ChromeToggle {
                text: i18n("Tuner")
                // The only panel toggle that does not want a score: a guitar
                // is in standard tuning until a file says otherwise.
                checked: panels.tuner
                onToggled: panels.tuner = checked
            }

            ChromeToggle {
                text: i18n("Status")
                checked: panels.status
                onToggled: panels.status = checked
            }
        }
    }

    // ---- the score, and the mixer beside it ----

    pageStack.initialPage: Kirigami.Page {
        padding: 0

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.backgroundColor: Ink.paper
        Kirigami.Theme.textColor: Ink.ink
        Kirigami.Theme.highlightColor: Ink.accent

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // The parts, on the far side of the score from the mixer: one of
            // these panels says which part you are looking at and the other
            // says what it sounds like, and they are different questions.
            Rectangle {
                Layout.preferredWidth: Ink.tracksWidth
                Layout.fillHeight: true
                visible: session.hasScore && panels.tracks
                color: Ink.panel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing * 2
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Heading {
                        level: 2
                        text: i18n("Tracks")
                        color: Ink.ink
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: Kirigami.Units.smallSpacing
                        boundsBehavior: Flickable.StopAtBounds
                        model: session.trackCount

                        delegate: TrackRow {
                            required property int index

                            width: ListView.view.width
                            glyph: session.trackIcons[index]
                            text: session.trackNames[index]
                            current: index === session.currentTrack
                            // Dimmed where it cannot be heard, the same as the
                            // mixer dims it: two panels disagreeing about
                            // whether a track is on would be worse than either.
                            opacity: (root.mixerRevision, session.isAudible(index)) ? 1.0 : 0.5
                            onClicked: {
                                session.currentTrack = index
                                view.forceActiveFocus()
                            }
                        }
                    }
                }

                Kirigami.Separator {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: Ink.rule
                }
            }

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
                        const control = event.modifiers & Qt.ControlModifier
                        // Alt and a movement key moves the note rather than
                        // the caret, which is the gesture every editor uses
                        // for "the same thing, somewhere else".
                        const alt = event.modifiers & Qt.AltModifier
                        // Shift and a movement key widens the selection from
                        // wherever it started, which is what shift does.
                        const extend = event.modifiers & Qt.ShiftModifier

                        // Ctrl and a digit is a note value rather than a fret:
                        // 1 is a semibreve, 2 a minim, and on down by halves,
                        // which is the arithmetic musicians already have in
                        // their heads.
                        if (control && event.key >= Qt.Key_1 && event.key <= Qt.Key_7) {
                            session.setDuration(1 << (event.key - Qt.Key_1))
                            event.accepted = true
                            return
                        }

                        switch (event.key) {
                        case Qt.Key_Left:
                            session.moveCursor(control ? "barBack" : "left", extend); break
                        case Qt.Key_Right:
                            session.moveCursor(control ? "barForward" : "right", extend); break
                        case Qt.Key_Up:
                            if (alt) {
                                session.moveNoteAcross(1)
                            } else {
                                control ? session.scaleDuration(1)
                                        : session.moveCursor("up", extend)
                            }
                            break
                        case Qt.Key_Down:
                            if (alt) {
                                session.moveNoteAcross(-1)
                            } else {
                                control ? session.scaleDuration(-1)
                                        : session.moveCursor("down", extend)
                            }
                            break
                        case Qt.Key_Home:  session.moveCursor("start", extend); break
                        case Qt.Key_End:   session.moveCursor("end", extend); break
                        case Qt.Key_Insert: session.insertBeat(); break
                        case Qt.Key_Delete:
                        case Qt.Key_Backspace:
                            // Delete takes the note off the string; Ctrl and
                            // Delete takes the whole beat out of the bar.
                            control ? session.deleteBeat() : session.clearNote(); break
                        case Qt.Key_Period: session.toggleDot(); break
                        case Qt.Key_Plus:
                        case Qt.Key_Equal: session.transpose(1); break
                        case Qt.Key_Minus: session.transpose(-1); break
                        // Letters mark what is already there rather than
                        // typing anything: x is a dead note, and the rest are
                        // the first letter of what they do.
                        case Qt.Key_X: session.toggleMark("dead"); break
                        case Qt.Key_G: session.toggleMark("ghost"); break
                        case Qt.Key_P: session.toggleMark("palmMute"); break
                        case Qt.Key_L: session.toggleMark("letRing"); break
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

                // On the paper rather than on the ink, so this one is drawn
                // the other way round: ink on paper until it is doing
                // something, and then paper on magenta.
                ChromeButton {
                    id: followButton
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Kirigami.Units.largeSpacing
                    icon.name: "followmouse"
                    filled: view.followPlayhead
                    visible: session.hasScore
                    text: i18n("Follow the playhead")
                    onClicked: view.followPlayhead = !view.followPlayhead

                    background: Rectangle {
                        radius: Ink.radius
                        color: view.followPlayhead
                            ? (followButton.hovered ? Ink.accentHover : Ink.accent)
                            : (followButton.hovered ? Ink.rule : Ink.panel)
                        border.width: view.followPlayhead ? 0 : 1
                        border.color: Ink.staff
                    }

                    contentItem: Kirigami.Icon {
                        source: "followmouse"
                        isMask: true
                        color: view.followPlayhead ? Ink.paper : Ink.ink
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 8
                    visible: !session.hasScore
                    icon.name: "music-note-16th"
                    text: i18n("No score open")
                    explanation: i18n("Open a Guitar Pro 7 or 8 file, or a score you saved.")
                    helpfulAction: Kirigami.Action {
                        icon.name: "document-open"
                        text: i18n("Open…")
                        onTriggered: fileDialog.open()
                    }
                }
            }

            // The mixer. Every track has a synth of its own, so soloing is not
            // a re-render -- it is one atomic store away from being heard.
            Rectangle {
                Layout.preferredWidth: Ink.mixerWidth
                Layout.fillHeight: true
                visible: session.hasScore && panels.mixer
                color: Ink.panel

                Kirigami.Separator {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: Ink.rule
                }

                QQC2.ScrollView {
                    id: mixerScroll
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing * 2
                    clip: true
                    // Nothing in a mixer strip wants to scroll sideways, and a
                    // horizontal bar that appears because a slider is a pixel
                    // too wide is worse than no bar at all.
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: mixerScroll.availableWidth
                        spacing: Kirigami.Units.largeSpacing * 2

                        Kirigami.Heading {
                            level: 2
                            text: i18n("Mixer")
                            color: Ink.ink
                        }

                        Repeater {
                            model: session.trackCount

                            delegate: ColumnLayout {
                                required property int index

                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                // The comma is not a mistake: it makes this
                                // binding depend on mixerRevision, which is the
                                // only thing that changes when another track is
                                // soloed.
                                opacity: (root.mixerRevision, session.isAudible(index)) ? 1.0 : 0.5

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: session.trackNames[index]
                                        elide: Text.ElideRight
                                        color: (root.mixerRevision, session.isSolo(index))
                                            ? Ink.accentDeep : Ink.ink
                                        font.weight: (root.mixerRevision, session.isSolo(index))
                                            ? Font.DemiBold : Font.Normal
                                    }

                                    MixerButton {
                                        text: i18n("S")
                                        checked: (root.mixerRevision, session.isSolo(index))
                                        QQC2.ToolTip.text: i18n("Hear only this")
                                        onToggled: session.setSolo(index, checked)
                                    }

                                    MixerButton {
                                        text: i18n("M")
                                        litFill: Ink.ink
                                        checked: (root.mixerRevision, session.isMuted(index))
                                        QQC2.ToolTip.text: i18n("Silence this")
                                        onToggled: session.setMuted(index, checked)
                                    }
                                }

                                InkSlider {
                                    Layout.fillWidth: true
                                    groove: Ink.rule
                                    from: 0
                                    to: 2
                                    value: session.gain(index)
                                    onMoved: session.setGain(index, value)
                                }
                            }
                        }

                        // Below a rule, because it is not one of the parts:
                        // no S, because soloing a track is not a reason to
                        // lose the beat you are hearing it against.
                        Kirigami.Separator {
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.smallSpacing
                            color: Ink.rule
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            opacity: session.click ? 1.0 : 0.5

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: i18n("Click")
                                    color: session.click ? Ink.accentDeep : Ink.ink
                                    font.weight: session.click ? Font.DemiBold : Font.Normal
                                }

                                MixerButton {
                                    text: i18n("On")
                                    checked: session.click
                                    QQC2.ToolTip.text: i18n("A metronome on every beat")
                                    onToggled: session.click = checked
                                }
                            }

                            InkSlider {
                                Layout.fillWidth: true
                                groove: Ink.rule
                                from: 0
                                to: 2
                                value: session.clickGain
                                onMoved: session.clickGain = value
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.largeSpacing
                            text: i18n("One synth per track. Solo and mute take effect live.")
                            color: Ink.quiet
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            wrapMode: Text.WordWrap
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }
            }
        }
    }

    // ---- the bars, and the status bar ----

    footer: ColumnLayout {
        spacing: 0

        /**
         * The tuner: what the strings should be, and where one of them is.
         *
         * The whole of what makes this different from a tuner on a phone is on
         * the left of it -- the names came out of the score, so a piece in drop
         * C asks for a C and says which peg to turn. A chromatic tuner would
         * hear the same note and leave the player to know whether it was the
         * right one.
         *
         * A band across the bottom rather than a panel beside the score,
         * because tuning is a thing done to the instrument and not to the
         * document: it wants to be wide, read from across the room, and gone
         * again when it is finished with.
         */
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 6
            visible: panels.tuner
            color: Ink.panelDeep

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                color: Ink.rule
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing * 2
                anchors.rightMargin: Kirigami.Units.largeSpacing * 2
                anchors.topMargin: Kirigami.Units.largeSpacing
                anchors.bottomMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing * 2

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 0

                    Kirigami.Heading {
                        level: 2
                        text: i18n("Tuner")
                        color: Ink.ink
                    }

                    QQC2.Label {
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 9
                        text: session.hasScore ? session.trackNames[session.currentTrack]
                                               : i18n("standard tuning")
                        color: Ink.quiet
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }

                // Every string of the part on the page, and which one is
                // sounding. Lowest first, the way the score writes them down.
                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: tuner.stringNames

                        delegate: Rectangle {
                            id: pill

                            required property int index
                            required property string modelData

                            readonly property bool lit: tuner.heard && tuner.string === index

                            implicitWidth: Kirigami.Units.gridUnit * 2.4
                            implicitHeight: Kirigami.Units.gridUnit * 2
                            radius: Ink.radius
                            color: pill.lit ? (tuner.inTune ? Ink.accent : Ink.accentTint)
                                            : "transparent"
                            border.width: pill.lit ? 0 : 1
                            border.color: Ink.staff
                            // A held reading fades rather than vanishing: the
                            // person reading this is looking at a machine head.
                            opacity: !pill.lit || tuner.fresh ? 1 : 0.55

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: pill.modelData
                                color: pill.lit ? (tuner.inTune ? Ink.paper : Ink.accentDeep)
                                                : Ink.ink
                                font.weight: pill.lit ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }

                /**
                 * The needle, fifty cents either side of the mark.
                 *
                 * Fifty because that is where the answer stops being "this
                 * string is out" and starts being "that is a different note":
                 * a scale somebody reads while turning a peg wants to be over
                 * the range they are turning it through.
                 */
                Item {
                    id: needle

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 10

                    readonly property real span: width / 2 - Kirigami.Units.gridUnit
                    readonly property real offset:
                        Math.max(-1, Math.min(1, tuner.cents / 50)) * needle.span

                    QQC2.Label {
                        id: heardNote
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        text: tuner.heard ? tuner.noteName : ""
                        color: tuner.inTune ? Ink.accentDeep : Ink.ink
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                        font.weight: Font.DemiBold
                        opacity: tuner.fresh ? 1 : 0.55
                    }

                    QQC2.Label {
                        anchors.verticalCenter: heardNote.verticalCenter
                        anchors.left: heardNote.right
                        anchors.leftMargin: Kirigami.Units.largeSpacing
                        visible: tuner.heard
                        text: tuner.inTune
                            ? i18n("in tune")
                            : i18nc("how far out of tune, in cents", "%1%2 ¢",
                                    tuner.cents > 0 ? "+" : "", Math.round(tuner.cents))
                        color: Ink.quiet
                        // Tabular figures: a needle whose label shuffled
                        // sideways as it settled would be its own distraction.
                        font.features: ({ "tnum": 1 })
                    }

                    // Not "scale": every Item has a property of that name, and
                    // an id that collides with one is an id the bindings inside
                    // a delegate quietly resolve the wrong way.
                    Rectangle {
                        id: dial

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        // Room for the marker, which stands half its height
                        // above and below the groove and would otherwise be
                        // painted over by the panel underneath.
                        anchors.bottomMargin: Kirigami.Units.gridUnit
                        height: Ink.groove
                        radius: height / 2
                        color: Ink.rule

                        Repeater {
                            model: [-50, -25, 0, 25, 50]

                            delegate: Rectangle {
                                required property int modelData

                                readonly property bool centre: modelData === 0

                                width: centre ? 3 : 2
                                height: centre ? Kirigami.Units.gridUnit * 1.3
                                               : Kirigami.Units.gridUnit * 0.7
                                radius: width / 2
                                // The mark itself is darker than the rest, and
                                // goes magenta when the string is on it: the
                                // scale should say where in tune is even while
                                // nothing is being played at it.
                                color: centre ? (tuner.inTune ? Ink.accent : Ink.quiet)
                                              : Ink.staff
                                x: dial.width / 2 + modelData / 50 * needle.span - width / 2
                                y: dial.height / 2 - height / 2
                            }
                        }

                        Rectangle {
                            width: 5
                            height: Kirigami.Units.gridUnit * 1.6
                            radius: width / 2
                            color: tuner.inTune ? Ink.accent : Ink.accentDeep
                            x: dial.width / 2 + needle.offset - width / 2
                            y: dial.height / 2 - height / 2
                            opacity: tuner.heard ? (tuner.fresh ? 1 : 0.45) : 0

                            Behavior on x {
                                NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                            }
                            Behavior on opacity {
                                NumberAnimation { duration: 160 }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 12
                    spacing: Kirigami.Units.smallSpacing

                    // The frequency while something is sounding, and what the
                    // input is doing while nothing is. Not "flat" and "sharp":
                    // the needle points and the number is signed, and a third
                    // way of saying the same thing is one too many.
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: tuner.heard
                            ? i18nc("a frequency in hertz", "%1 Hz", tuner.hertz.toFixed(1))
                            : tuner.message
                        color: tuner.error.length > 0 ? Ink.accentDeep : Ink.quiet
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignRight
                        font.features: ({ "tnum": 1 })
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    // How loud the input is, so that a dead cable and a quiet
                    // room do not look the same as each other.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Ink.groove
                        radius: height / 2
                        color: Ink.rule
                        visible: tuner.running

                        Rectangle {
                            width: parent.width * tuner.level
                            height: parent.height
                            radius: parent.radius
                            color: Ink.staff

                            Behavior on width {
                                NumberAnimation { duration: 80 }
                            }
                        }
                    }
                }
            }
        }

        /**
         * Every bar of the piece, in a row.
         *
         * A score is read a line at a time and navigated a bar at a time, and
         * until now the only way to reach bar 96 was to scroll to it. The bar
         * being played is lit in the same colour the page lights it, so this
         * says where the music is as well as where you are.
         */
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Ink.barBoxHeight + Kirigami.Units.largeSpacing * 2
            visible: panels.bars && session.hasScore
            color: Ink.panelDeep

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                color: Ink.rule
            }

            ListView {
                id: barStrip

                property int lit: -1

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing * 2
                anchors.rightMargin: Kirigami.Units.largeSpacing * 2
                orientation: ListView.Horizontal
                spacing: Kirigami.Units.smallSpacing
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: session.barCount

                delegate: RowLayout {
                    id: barCell
                    required property int index

                    property string section: session.sectionAt(index)
                    property bool playing: index === session.currentBar
                    property bool atCaret: index === session.caretBar

                    height: barStrip.height
                    spacing: Kirigami.Units.smallSpacing

                    // Where the score names a section, its name goes in front
                    // of the bar it starts: a strip of numbers with no words in
                    // it is a ruler rather than a map.
                    QQC2.Label {
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        visible: barCell.section.length > 0
                        text: barCell.section
                        color: Ink.quiet
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }

                    Rectangle {
                        Layout.preferredWidth: Ink.barBoxWidth
                        Layout.preferredHeight: Ink.barBoxHeight
                        Layout.alignment: Qt.AlignVCenter
                        radius: Ink.radius
                        color: barCell.playing ? Ink.accent
                             : (barCell.atCaret ? Ink.accentTint
                             : (barMouse.containsMouse ? Ink.rule : Ink.paper))
                        border.width: 1
                        border.color: barCell.atCaret && !barCell.playing ? Ink.accent : Ink.rule

                        QQC2.Label {
                            anchors.centerIn: parent
                            text: barCell.index + 1
                            color: barCell.playing ? Ink.paper
                                 : (barCell.atCaret ? Ink.accentDeep : Ink.ink)
                            font.weight: barCell.playing || barCell.atCaret
                                ? Font.DemiBold : Font.Normal
                            font.features: ({ "tnum": 1 })
                        }

                        MouseArea {
                            id: barMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                session.goToBar(barCell.index)
                                // Jumping to a bar is usually the first half of
                                // writing something in it.
                                view.forceActiveFocus()
                            }
                        }
                    }
                }

                // Following the music, and following the caret: both put a bar
                // in view, and neither drags the strip about while the other is
                // what somebody is watching.
                Connections {
                    target: session

                    function onPositionChanged() {
                        if (session.playing && session.currentBar !== barStrip.lit) {
                            barStrip.lit = session.currentBar
                            if (barStrip.lit >= 0) {
                                barStrip.positionViewAtIndex(barStrip.lit, ListView.Contain)
                            }
                        }
                    }

                    function onCursorMoved() {
                        if (!session.playing && session.caretBar >= 0) {
                            barStrip.positionViewAtIndex(session.caretBar, ListView.Contain)
                        }
                    }
                }
            }
        }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Kirigami.Units.gridUnit * 1.9
        visible: panels.status
        color: Ink.ink

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.largeSpacing * 2
            anchors.rightMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.largeSpacing * 2

            // Where the caret is. First, because it is the one thing on this
            // bar that is true at every moment.
            QQC2.Label {
                text: session.caretText
                color: Ink.paper
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.weight: Font.DemiBold
                visible: session.hasScore
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: session.status
                color: Ink.faint
                elide: Text.ElideRight
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            // What one press of undo would take back, in the colour the accent
            // becomes on ink.
            QQC2.Label {
                text: i18n("Undo: %1", session.undoText)
                color: Ink.accentOnInk
                visible: session.canUndo
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }
    }
}
