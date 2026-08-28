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

    /** A rig named on the command line: samples and effects, by track. */
    property var initialSamplers: ({})
    property var initialEffects: ({})

    /** And the SoundFont, if one was named. Empty means the usual search. */
    property string initialSoundFont: ""

    /**
     * The mixer has no per-track model, because the player's state lives in
     * atomics that the audio thread reads. Bumping this on every change is
     * what makes the bindings below re-read it -- see the comma expressions,
     * which exist to declare a dependency QML cannot otherwise see.
     */
    property int mixerRevision: 0

    /**
     * The same trick for the chains, which are not a model either.
     *
     * `chainHere` answers for the part on the page and `effectsSummary` for
     * any of them, and neither is a property QML can watch per track. Bumping
     * this is what re-reads them -- see the comma expressions below, which
     * exist to declare a dependency QML cannot otherwise see.
     */
    property int effectsRevision: 0

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
        // Off by default, like the tuner: a band of amplifier controls is not
        // what somebody opening a tab to read it wants to see first.
        property bool effects: false
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
        // The comma is not a mistake: it makes this depend on tuningHere,
        // which is what changes when the instrument is retuned. Without it the
        // tuner would go on asking for the tuning the score arrived with.
        strings: session.hasScore
            ? (session.tuningHere, session.stringPitches(session.currentTrack))
            : []
    }

    Connections {
        target: session
        function onMixerChanged() {
            root.mixerRevision++
        }
        function onEffectsChanged() {
            root.effectsRevision++
        }
    }

    Component.onCompleted: {
        // Before the score, so the first player built is built with it rather
        // than being thrown away and built again.
        if (initialSoundFont.length > 0) {
            session.useSoundFont(initialSoundFont)
        }
        if (initialFile.length > 0) {
            session.open(initialFile)
            session.applyRig(initialSamplers, initialEffects)
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
        id: sfzDialog
        title: i18n("Choose a sample library")
        nameFilters: [i18n("SFZ instruments (*.sfz)"), i18n("All files (*)")]
        onAccepted: session.setSamplerHere(selectedFile)
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

    /**
     * A knob that turns, for a control on ink with a range and no names.
     *
     * A knob and not a slider, on the deck and nowhere else. Everywhere else
     * in this window a level is a distance -- a fader, a seek bar, a gain --
     * and a distance is the honest drawing of one. An amplifier's controls are
     * not distances: nobody has ever seen a linear presence control, and the
     * six of them belong in a block the eye reads as a front panel rather than
     * in six rows of a list.
     *
     * Built on `Dial`, so dragging, the scroll wheel and the arrow keys all
     * work without being written here; only the drawing is ours.
     */
    component InkKnob: QQC2.Dial {
        id: inkKnob

        implicitWidth: Kirigami.Units.gridUnit * 2.2
        implicitHeight: implicitWidth
        // Vertical, because a rotary drag on a control this size is a wrist
        // movement nobody makes accurately and every plugin host gave up on.
        inputMode: QQC2.Dial.Vertical
        wheelEnabled: true
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: inkKnob.hovered || inkKnob.pressed ? Ink.line : Ink.well
            border.width: 1
            border.color: Ink.edge
        }

        handle: Item {
            anchors.fill: parent

            Rectangle {
                x: parent.width / 2 - width / 2
                y: parent.height * 0.13
                width: 2
                height: parent.height * 0.3
                radius: 1
                color: inkKnob.pressed ? Ink.accent : Ink.paper

                transform: Rotation {
                    origin.x: 1
                    origin.y: inkKnob.height * 0.37
                    angle: inkKnob.angle
                }
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
        implicitHeight: toolFlow.implicitHeight + Kirigami.Units.largeSpacing * 2
        color: Ink.ink

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
        Kirigami.Theme.backgroundColor: Ink.ink
        Kirigami.Theme.textColor: Ink.paper
        Kirigami.Theme.highlightColor: Ink.accent

        /**
         * Two groups: what the program does, and what the window shows.
         *
         * A single row of all of it is longer than a narrow window and simply
         * ran off the end -- the transport, the clocks and every panel toggle
         * were there and unreachable, which is worse than not fitting because
         * nothing says they are missing. In a flow the toggles drop to a
         * second line when the first cannot hold both groups, and the toolbar
         * grows by a row to make room for them.
         */
        Flow {
            id: toolFlow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Kirigami.Units.largeSpacing * 2
            anchors.rightMargin: Kirigami.Units.largeSpacing * 2
            anchors.topMargin: Kirigami.Units.largeSpacing
            height: implicitHeight
            spacing: Kirigami.Units.smallSpacing * 2

            // Measured against what each group is asking for rather than what
            // it got, so this decides the widths below without depending on
            // them and going round in a circle.
            /** What the groups that cannot stretch want between them. */
            readonly property real fixed: fileGroup.implicitWidth + barGroup.implicitWidth
                + toggles.implicitWidth + spacing * 3

            /** Whether all four fit on one line, with room left for the slider. */
            readonly property bool oneLine:
                fixed + Kirigami.Units.gridUnit * 6 + 4 <= width


        RowLayout {
            id: fileGroup
            spacing: Kirigami.Units.smallSpacing * 2

            ChromeButton {
                icon.name: "document-new"
                text: i18n("Start a new score")
                onClicked: {
                    session.newScore()
                    view.forceActiveFocus()
                }
            }

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

        }

        RowLayout {
            id: barGroup
            spacing: Kirigami.Units.smallSpacing * 2

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

        }

        /**
         * The clocks and the slider, which is the group that takes the slack.
         *
         * Given the room left over when everything fits on one line, and a
         * line of its own when it does not: a slider squeezed to nothing is a
         * slider nobody can drag, and one wrapped onto its own row is still a
         * transport.
         */
        RowLayout {
            id: positionGroup

            width: toolFlow.oneLine
                ? Math.floor(toolFlow.width - toolFlow.fixed) - 2
                : toolFlow.width
            spacing: Kirigami.Units.smallSpacing * 2

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

        }

        RowLayout {
            id: toggles

            spacing: Kirigami.Units.smallSpacing * 2

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
                text: i18n("Effects")
                enabled: session.hasScore
                checked: panels.effects
                onToggled: panels.effects = checked
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

                /**
                 * The list and the inspector scroll together.
                 *
                 * They were fighting over a narrow column: the inspector has a
                 * fixed number of controls in it and the list was given
                 * whatever was left, which on a window of ordinary height was
                 * two rows of a four-part score. A list of parts that cannot
                 * show the parts is the wrong thing to squeeze, and there is
                 * no arrangement of a column this size that fits both, so the
                 * panel scrolls instead of choosing.
                 */
                QQC2.ScrollView {
                    id: trackScroll

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing * 2
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                    width: trackScroll.availableWidth
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Heading {
                        level: 2
                        text: i18n("Tracks")
                        color: Ink.ink
                    }

                    ListView {
                        Layout.fillWidth: true
                        // As tall as it needs, because the panel around it is
                        // what scrolls now: a list inside a scrolling panel
                        // that also scrolls is two scrollbars for one thing.
                        Layout.preferredHeight: contentHeight
                        interactive: false
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

                    /**
                     * What the chosen part is, and what parts there are.
                     *
                     * Under the list because all of it is about the row lit up
                     * in it. A part is a column through every bar of the score
                     * -- adding one puts an empty bar in every bar and taking
                     * one out removes a column -- which is why these are four
                     * deliberate buttons rather than a drag.
                     *
                     * Tighter spacing than the panel above it, because the
                     * list is what this panel is for and an inspector that
                     * squeezed it down to two rows would have the tail wagging
                     * the dog.
                     */
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Separator {
                            Layout.fillWidth: true
                            Layout.bottomMargin: Kirigami.Units.smallSpacing
                            color: Ink.rule
                        }

                        QQC2.TextField {
                            id: nameField

                            Layout.fillWidth: true
                            color: Ink.ink
                            text: session.trackNameHere

                            background: Rectangle {
                                radius: Ink.radius
                                color: nameField.activeFocus ? Ink.paper : "transparent"
                                border.width: 1
                                border.color: nameField.activeFocus ? Ink.accent : Ink.rule
                            }

                            onAccepted: {
                                session.renameTrack(session.currentTrack, text)
                                view.forceActiveFocus()
                            }
                            onActiveFocusChanged: if (!activeFocus) {
                                text = Qt.binding(() => session.trackNameHere)
                            }
                        }

                        // What it is, and the way to make it something else:
                        // one control, because the name of the instrument and
                        // the list of instruments are the same question.
                        QQC2.Button {
                            Layout.fillWidth: true
                            flat: true
                            text: session.instrumentHere
                            onClicked: instrumentMenu.popup()

                            QQC2.Menu {
                                id: instrumentMenu
                                Repeater {
                                    model: session.instrumentNames
                                    delegate: QQC2.MenuItem {
                                        required property int index
                                        required property string modelData
                                        text: modelData
                                        onTriggered: session.setTrackInstrument(
                                            session.currentTrack,
                                            session.instrumentIds[index])
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            MixerButton {
                                text: i18n("+")
                                checkable: false
                                QQC2.ToolTip.text: i18n("Add a part after this one")
                                onClicked: addMenu.popup()

                                QQC2.Menu {
                                    id: addMenu
                                    Repeater {
                                        model: session.instrumentNames
                                        delegate: QQC2.MenuItem {
                                            required property int index
                                            required property string modelData
                                            text: modelData
                                            onTriggered: session.addTrack(
                                                session.instrumentIds[index])
                                        }
                                    }
                                }
                            }

                            MixerButton {
                                text: i18n("\u2212")
                                checkable: false
                                litFill: Ink.ink
                                QQC2.ToolTip.text: i18n("Take this part out")
                                onClicked: session.removeTrack(session.currentTrack)
                            }

                            Item { Layout.fillWidth: true }

                            // The arrows carry a text-presentation selector.
                            // Without it they come out of a colour emoji font,
                            // in a colour that is in no palette this window has.
                            MixerButton {
                                text: i18n("\u2191\ufe0e")
                                checkable: false
                                QQC2.ToolTip.text: i18n("Move it up")
                                onClicked: session.moveTrack(session.currentTrack, -1)
                            }

                            MixerButton {
                                text: i18n("\u2193\ufe0e")
                                checkable: false
                                QQC2.ToolTip.text: i18n("Move it down")
                                onClicked: session.moveTrack(session.currentTrack, 1)
                            }
                        }
                    }

                    /**
                     * Which recordings the part is played from.
                     *
                     * A menu of what is installed rather than a file dialog:
                     * the libraries somebody has are in a folder the program
                     * knows about, and picking one should not mean navigating
                     * to it. Grouped by the library they came in, because a
                     * drum kit alone can hold forty programmes and a flat list
                     * of five hundred is a list nobody reads.
                     *
                     * Not written into the score. Which recordings a part is
                     * played through is a property of this machine, and a .fw
                     * naming a path on somebody's disk would open wrong
                     * everywhere else.
                     */
                    QQC2.Button {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        visible: session.stringsHere > 0 || session.instrumentHere.length > 0
                        flat: true
                        text: session.samplerHere.length > 0
                            ? i18n("Samples: %1", session.samplerHere)
                            : i18n("Samples: General MIDI")
                        onClicked: samplesMenu.popup()

                        // A programme name is as long as somebody named it,
                        // and the panel is as wide as it is.
                        contentItem: QQC2.Label {
                            text: parent.text
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Ink.ink
                        }

                        QQC2.Menu {
                            id: samplesMenu

                            QQC2.MenuItem {
                                text: i18n("A General MIDI programme")
                                onTriggered: session.setSamplerHere("")
                            }

                            QQC2.MenuSeparator {}

                            // An Instantiator and not a Repeater: a submenu is
                            // a popup rather than an item, and a Repeater can
                            // only make items.
                            Instantiator {
                                model: session.collections
                                onObjectAdded: (index, object) =>
                                    samplesMenu.insertMenu(index + 2, object)
                                onObjectRemoved: (index, object) =>
                                    samplesMenu.removeMenu(object)

                                delegate: QQC2.Menu {
                                    id: collectionMenu
                                    required property string modelData

                                    title: collectionMenu.modelData

                                    Repeater {
                                        // Only this collection's programmes,
                                        // which is what makes the menu a menu
                                        // rather than a wall.
                                        model: session.libraries.filter(
                                            entry => entry.collection
                                                     === collectionMenu.modelData)
                                        delegate: QQC2.MenuItem {
                                            required property var modelData
                                            text: modelData.name
                                            onTriggered: session.setSamplerHere(modelData.path)
                                        }
                                    }
                                }
                            }

                            QQC2.MenuSeparator {}

                            QQC2.MenuItem {
                                text: i18n("From a file…")
                                onTriggered: sfzDialog.open()
                            }

                            QQC2.MenuItem {
                                text: i18n("Look again")
                                onTriggered: session.rescanLibraries()
                            }
                        }
                    }

                    /**
                     * What the instrument on the page is tuned to.
                     *
                     * Under the list because it belongs to whichever part is
                     * chosen there, and written as names rather than numbers
                     * because that is how a guitarist says a tuning. One field
                     * for all six: somebody who wants drop D types drop D, and
                     * six spin boxes would be six things to get right instead
                     * of one.
                     *
                     * Retuning moves the pitches and leaves the frets: the tab
                     * on the page does not change, because nobody rewrote it.
                     */
                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.topMargin: -Kirigami.Units.largeSpacing
                                          + Kirigami.Units.smallSpacing
                        visible: session.stringsHere > 0
                        color: Ink.rule
                    }

                    QQC2.Label {
                        // A drum kit has no strings and no business here.
                        Layout.topMargin: -Kirigami.Units.largeSpacing
                                          + Kirigami.Units.smallSpacing
                        visible: session.stringsHere > 0
                        text: i18n("Tuning")
                        color: Ink.quiet
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.weight: Font.DemiBold
                    }

                    QQC2.TextField {
                        id: tuningField

                        visible: session.stringsHere > 0
                        Layout.topMargin: -Kirigami.Units.largeSpacing
                                          + Kirigami.Units.smallSpacing
                        Layout.fillWidth: true
                        color: Ink.ink
                        text: session.tuningHere
                        placeholderText: i18n("E2 A2 D3 G3 B3 E4")

                        background: Rectangle {
                            radius: Ink.radius
                            color: tuningField.activeFocus ? Ink.paper : "transparent"
                            border.width: 1
                            border.color: tuningField.activeFocus ? Ink.accent : Ink.rule
                        }

                        onAccepted: {
                            session.setTuningHere(text)
                            view.forceActiveFocus()
                        }
                        onActiveFocusChanged: if (!activeFocus) {
                            text = Qt.binding(() => session.tuningHere)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: -Kirigami.Units.largeSpacing
                                          + Kirigami.Units.smallSpacing
                        visible: session.stringsHere > 0
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Capo")
                            color: Ink.quiet
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.weight: Font.DemiBold
                        }

                        // A capo raises every string at once, so this moves
                        // every note in the part with it -- the fret numbers
                        // are counted from the capo and do not change.
                        //
                        // A field and not a spin box: the desktop style draws
                        // one of those out of the desktop's own colours, which
                        // is the thing this window is deliberately not doing,
                        // and is the lesson the track dropdown already taught.
                        QQC2.TextField {
                            id: capoField

                            implicitWidth: Kirigami.Units.gridUnit * 3
                            horizontalAlignment: Text.AlignHCenter
                            color: session.capoHere > 0 ? Ink.accentDeep : Ink.ink
                            font.features: ({ "tnum": 1 })
                            validator: IntValidator { bottom: 0; top: 12 }
                            text: session.capoHere

                            background: Rectangle {
                                radius: Ink.radius
                                color: capoField.activeFocus ? Ink.paper : "transparent"
                                border.width: 1
                                border.color: capoField.activeFocus ? Ink.accent : Ink.rule
                            }

                            onAccepted: {
                                session.setCapoHere(Number(text))
                                view.forceActiveFocus()
                            }
                            onActiveFocusChanged: if (!activeFocus) {
                                text = Qt.binding(() => session.capoHere)
                            }
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

                                /**
                                 * What this part is played through, in a line.
                                 *
                                 * The mixer is the one panel that shows every
                                 * part at once, and a chain is routing -- so
                                 * this is where "which of these is the one
                                 * going through the amplifier" gets answered
                                 * without clicking through four parts to find
                                 * out. The knobs are on the effects deck; this
                                 * only says that there are some.
                                 */
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: (root.effectsRevision,
                                           session.effectsSummary(index))
                                    elide: Text.ElideRight
                                    color: Ink.quiet
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
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

                            /**
                             * How fast, what that is called, and what it is in.
                             *
                             * Large, because this is the number a player looks
                             * up for and the toolbar's copy of it is a field
                             * sized to be typed in rather than read across a
                             * room. The word beside it is the half a musician
                             * recognises before the digits: 150 is a setting,
                             * Allegro is what the piece is.
                             */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                visible: session.hasScore

                                QQC2.Label {
                                    text: Math.round(session.tempoHere)
                                    color: Ink.ink
                                    font.weight: Font.DemiBold
                                    font.pointSize:
                                        Kirigami.Theme.defaultFont.pointSize * 2.9
                                    font.features: ({ "tnum": 1 })
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignBaseline
                                    text: session.tempoTermHere
                                    color: Ink.quiet
                                    elide: Text.ElideRight
                                    font.italic: true
                                }

                                QQC2.Label {
                                    Layout.alignment: Qt.AlignBaseline
                                    text: session.timeHere
                                    color: Ink.quiet
                                    font.features: ({ "tnum": 1 })
                                }
                            }

                            /**
                             * The bar being counted, as notes rather than as a
                             * number.
                             *
                             * One glyph per beat the bar is counted in, which
                             * is not always one per beat the denominator names
                             * -- 6/8 is two beats of three quavers and shows
                             * two. The first is printed in full ink because a
                             * downbeat is the one everybody is listening for,
                             * and the beat sounding takes the accent with a
                             * dot under it, so a player glancing up from the
                             * neck can see where in the bar the music is.
                             */
                            RowLayout {
                                id: countedBeats

                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.smallSpacing
                                visible: session.hasScore && session.beatsHere > 0
                                spacing: 0

                                Repeater {
                                    model: session.beatsHere

                                    delegate: Item {
                                        id: beatCell
                                        required property int index

                                        readonly property bool downbeat: index === 0
                                        readonly property bool sounding:
                                            index === session.beatNow

                                        Layout.fillWidth: true
                                        implicitHeight: beatGlyph.implicitHeight
                                                        + beatDot.height
                                                        + Kirigami.Units.smallSpacing

                                        QQC2.Label {
                                            id: beatGlyph
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: parent.top
                                            text: "\u2669"
                                            color: beatCell.sounding ? Ink.accent
                                                 : (beatCell.downbeat ? Ink.ink : Ink.staff)
                                            font.pointSize:
                                                Kirigami.Theme.defaultFont.pointSize
                                                * (beatCell.downbeat ? 1.9 : 1.55)
                                        }

                                        // The dot a conductor's hand makes at
                                        // the bottom of a beat, and the only
                                        // mark here that moves.
                                        Rectangle {
                                            id: beatDot
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: beatGlyph.bottom
                                            anchors.topMargin: Kirigami.Units.smallSpacing / 2
                                            width: Kirigami.Units.smallSpacing
                                            height: width
                                            radius: width / 2
                                            opacity: beatCell.sounding ? 1 : 0
                                            color: Ink.accent
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                opacity: session.click ? 1.0 : 0.5

                                QQC2.Label {
                                    text: i18n("gain")
                                    color: Ink.quiet
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                }

                                InkSlider {
                                    id: clickGain
                                    Layout.fillWidth: true
                                    groove: Ink.rule
                                    from: 0
                                    to: 2
                                    value: session.clickGain
                                    onMoved: session.clickGain = value
                                }

                                QQC2.Label {
                                    text: session.clickGain.toFixed(1)
                                    color: Ink.quiet
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    font.features: ({ "tnum": 1 })
                                }
                            }
                        }

                        /**
                         * Every part as a pair of ports in the graph.
                         *
                         * In the mixer because that is where this program says
                         * what goes where. It is the point of a synth per
                         * track made true outside this window: a DAW links to
                         * these and records the stems as they play, instead of
                         * importing files written after the fact.
                         */
                        Kirigami.Separator {
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.smallSpacing
                            color: Ink.rule
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: session.ports
                                    ? i18n("Ports — %1 pairs",
                                           session.portCount)
                                    : i18n("Ports")
                                color: session.ports ? Ink.accentDeep : Ink.ink
                                font.weight: session.ports ? Font.DemiBold : Font.Normal
                            }

                            MixerButton {
                                text: i18n("On")
                                checked: session.ports
                                QQC2.ToolTip.text:
                                    i18n("A pair of ports per part, for a DAW to record")
                                onToggled: session.ports = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18n("Follow the graph")
                                color: session.following ? Ink.accentDeep : Ink.ink
                                font.weight: session.following ? Font.DemiBold : Font.Normal
                            }

                            MixerButton {
                                text: i18n("On")
                                checked: session.following
                                QQC2.ToolTip.text:
                                    i18n("The DAW starts it and says where in the piece it is")
                                onToggled: session.following = checked
                            }
                        }

                        // Said out loud, because both of these were silence
                        // with no explanation the first time somebody used
                        // them: ports nothing is listening to, and a follower
                        // with nothing to follow.
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: session.ports && session.portLinks === 0
                            text: i18n("Nothing is linked to the ports, so nothing comes out of the speakers.")
                            color: Ink.accentDeep
                            wrapMode: Text.WordWrap
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: session.following
                            text: session.graphRolling
                                ? i18n("The graph is rolling; the transport is its own.")
                                : i18n("Waiting for the graph to roll — press play in the DAW, not here.")
                            color: session.graphRolling ? Ink.quiet : Ink.accentDeep
                            wrapMode: Text.WordWrap
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
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
            id: tunerPanel

            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 13
            visible: panels.tuner
            // Ink, unlike the panels either side of it. A tuner is read at
            // arm's length while both hands are busy, and the ladder is a row
            // of lit blocks -- which is a thing that glows on a dark ground
            // and a thing that stains on a light one.
            color: Ink.ink

            /** How far out the ladder reads, either side of the mark. */
            readonly property real span: 50
            /** Blocks each side of the centre. One block is `span / steps` cents. */
            readonly property int steps: 10

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing * 3
                anchors.rightMargin: Kirigami.Units.largeSpacing * 3
                anchors.topMargin: Kirigami.Units.largeSpacing * 2
                anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                spacing: Kirigami.Units.largeSpacing * 2

                // ---- what is being tuned, and whether anything is listening ----

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing * 1.5

                    QQC2.Label {
                        text: i18nc("the name of this panel", "TUNER")
                        color: Ink.faint
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.letterSpacing: 2
                    }

                    // The part, how it is tuned, where its capo is and what A
                    // is taken to be. The last of those is the only assumption
                    // in the tuner worth arguing about, so it is said out loud
                    // rather than left for somebody to discover.
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: session.hasScore
                            ? i18nc("part, tuning, capo and concert pitch",
                                    "%1 · %2 · capo %3 · A = 440",
                                    session.trackNames[session.currentTrack],
                                    session.tuningHere, session.capoHere)
                            : i18n("standard tuning · A = 440")
                        color: Ink.faint
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    ChromeToggle {
                        text: tuner.running ? i18n("Listening") : i18n("Listen")
                        checked: tuner.listening
                        onToggled: panels.tuner = checked
                    }
                }

                // ---- the note, and the ladder it is read on ----

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Kirigami.Units.largeSpacing * 3

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 7
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: tuner.heard ? tuner.noteName : "—"
                            color: tuner.heard ? Ink.paper : Ink.edge
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 3.4
                            font.weight: Font.DemiBold
                            opacity: tuner.fresh || !tuner.heard ? 1 : 0.55
                        }

                        /**
                         * How far out, which way, and which way to turn.
                         *
                         * In the colour of the direction it names, so that the
                         * sentence and the ladder agree without either being
                         * read twice. "Tighten" and "slacken" rather than
                         * "sharp" and "flat" alone: the two words name the
                         * thing to do, and the ladder has already said which
                         * side of the mark it is on.
                         */
                        QQC2.Label {
                            Layout.alignment: Qt.AlignHCenter
                            visible: tuner.heard
                            text: tuner.inTune
                                ? i18n("in tune")
                                : i18nc("how far out, and which way to turn the peg",
                                        "%1 ¢ %2 — %3", Math.round(Math.abs(tuner.cents)),
                                        tuner.cents < 0 ? i18n("flat") : i18n("sharp"),
                                        tuner.cents < 0 ? i18n("tighten") : i18n("slacken"))
                            color: tuner.inTune ? Ink.arrived
                                                : (tuner.cents < 0 ? Ink.flatNearest
                                                                   : Ink.accentOnInk)
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.features: ({ "tnum": 1 })
                            opacity: tuner.fresh ? 1 : 0.55
                        }

                        // What the input is doing while nothing is sounding,
                        // so that a dead cable does not read as a quiet room.
                        QQC2.Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 9
                            visible: !tuner.heard
                            text: tuner.message
                            color: tuner.error.length > 0 ? Ink.accentOnInk : Ink.quiet
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Kirigami.Units.smallSpacing

                        /**
                         * Twenty-one blocks: ten either side of the mark, and
                         * the mark itself wider than the rest.
                         *
                         * They light from where the string is inward to the
                         * mark rather than out from the mark, because what is
                         * being watched is the gap closing. Taller toward the
                         * centre for the same reason a ruler's inch marks are
                         * taller than its eighths -- the eye finds the middle
                         * without reading anything.
                         */
                        RowLayout {
                            id: ladder

                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 2.6
                            spacing: 2

                            /** Blocks from the mark to where the string is. */
                            readonly property int reach:
                                tuner.heard && !tuner.inTune
                                    ? Math.min(tunerPanel.steps,
                                               Math.ceil(Math.abs(tuner.cents)
                                                         / (tunerPanel.span / tunerPanel.steps)))
                                    : 0

                            Repeater {
                                model: tunerPanel.steps * 2 + 1

                                delegate: Rectangle {
                                    id: block

                                    required property int index

                                    /** Negative is flat, zero is the mark. */
                                    readonly property int fromCentre:
                                        block.index - tunerPanel.steps
                                    readonly property int distance: Math.abs(block.fromCentre)
                                    readonly property bool centre: block.distance === 0

                                    /**
                                     * Lit where it lies between the mark and
                                     * the reading, on the reading's own side.
                                     */
                                    readonly property bool lit:
                                        !block.centre && ladder.reach > 0
                                        && block.distance <= ladder.reach
                                        && (block.fromCentre < 0) === (tuner.cents < 0)

                                    readonly property var flatRamp: [Ink.flatNearest,
                                                                     Ink.flatNearer,
                                                                     Ink.flatNear, Ink.flat]
                                    readonly property var sharpRamp: [Ink.accentOnInk,
                                                                      Ink.sharpNearer,
                                                                      Ink.sharpNear, Ink.accent]

                                    Layout.fillWidth: true
                                    // The mark is wider as well as taller: it
                                    // is the one block somebody is trying to
                                    // land on.
                                    Layout.preferredWidth: block.centre ? 1.4 : 1
                                    Layout.preferredHeight: block.centre
                                        ? ladder.height
                                        : (block.distance <= 3 ? ladder.height * 0.77
                                                               : block.distance <= 6
                                                                   ? ladder.height * 0.64
                                                                   : ladder.height * 0.5)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: 1

                                    color: block.centre
                                        ? (tuner.inTune ? Ink.arrived : Ink.well)
                                        : block.lit
                                            ? (block.fromCentre < 0
                                                   ? block.flatRamp[Math.min(3, block.distance - 1)]
                                                   : block.sharpRamp[Math.min(3, block.distance - 1)])
                                            : Ink.well
                                    // The mark keeps its ring whether or not
                                    // anything has landed on it, so the thing
                                    // being aimed at is visible before the aim.
                                    border.width: block.centre ? 1 : 0
                                    border.color: Ink.arrived
                                    opacity: !block.lit || tuner.fresh ? 1 : 0.55

                                    Behavior on color {
                                        ColorAnimation { duration: 90 }
                                    }
                                }
                            }
                        }

                        /**
                         * The scale, built on the ladder's own slots.
                         *
                         * One invisible cell per block, with a number in every
                         * fifth: a row of five labels spaced by hand lands
                         * them near the quarters rather than on them, and a
                         * scale whose nought is not over the mark is a scale
                         * saying something false about the thing beside it.
                         */
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: ladder.spacing

                            Repeater {
                                model: tunerPanel.steps * 2 + 1

                                delegate: QQC2.Label {
                                    id: mark

                                    required property int index

                                    readonly property int fromCentre:
                                        mark.index - tunerPanel.steps

                                    Layout.fillWidth: true
                                    Layout.preferredWidth: mark.fromCentre === 0 ? 1.4 : 1
                                    horizontalAlignment: Text.AlignHCenter
                                    text: switch (mark.fromCentre) {
                                        case -tunerPanel.steps:
                                            return i18nc("fifty cents flat", "♭ −50")
                                        case -tunerPanel.steps / 2: return "−25"
                                        case 0: return "0"
                                        case tunerPanel.steps / 2: return "+25"
                                        case tunerPanel.steps:
                                            return i18nc("fifty cents sharp", "+50 ♯")
                                        default: return ""
                                    }
                                    color: mark.fromCentre === 0
                                        ? Ink.arrived
                                        : mark.fromCentre === -tunerPanel.steps
                                            ? Ink.flatNearer
                                            : mark.fromCentre === tunerPanel.steps
                                                ? Ink.accentOnInk
                                                : Ink.quiet
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    font.features: ({ "tnum": 1 })
                                }
                            }
                        }
                    }
                }

                // ---- the strings of the part, and how loud the input is ----

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        text: i18n("Strings")
                        color: Ink.faint
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    Repeater {
                        model: tuner.stringNames

                        delegate: Rectangle {
                            id: chip

                            required property int index
                            required property string modelData

                            readonly property bool lit: tuner.heard && tuner.string === index

                            implicitWidth: chipLabel.implicitWidth
                                           + Kirigami.Units.largeSpacing * 2
                            implicitHeight: Kirigami.Units.gridUnit * 1.7
                            radius: Ink.radius
                            color: chip.lit ? Ink.accent : "transparent"
                            border.width: 1
                            border.color: chip.lit ? Ink.accent : Ink.edge
                            // A held reading fades rather than vanishing: the
                            // person reading this is looking at a machine head.
                            opacity: !chip.lit || tuner.fresh ? 1 : 0.55

                            QQC2.Label {
                                id: chipLabel

                                anchors.centerIn: parent
                                text: chip.modelData
                                color: chip.lit ? Ink.paper : Ink.faint
                                font.weight: chip.lit ? Font.DemiBold : Font.Normal
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    QQC2.Label {
                        visible: tuner.heard
                        text: i18nc("a frequency in hertz", "%1 Hz", tuner.hertz.toFixed(1))
                        color: Ink.quiet
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.features: ({ "tnum": 1 })
                    }

                    QQC2.Label {
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        text: i18nc("the input level", "level")
                        color: Ink.faint
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                        implicitHeight: Ink.groove
                        radius: height / 2
                        color: Ink.well
                        visible: tuner.running

                        Rectangle {
                            width: parent.width * tuner.level
                            height: parent.height
                            radius: parent.radius
                            color: Ink.accentOnInk

                            Behavior on width {
                                NumberAnimation { duration: 80 }
                            }
                        }
                    }
                }
            }
        }

        /**
         * The effects deck: what the part goes through on its way out.
         *
         * Out of the mixer, where it used to be, and into a band of its own.
         * A chain is a row of boxes with a cable between them and it was being
         * drawn as a column of rows two hundred and ninety pixels wide -- six
         * knobs of an amplifier stacked vertically, each with its name in a
         * column beside it, which is a list of numbers rather than a front
         * panel. The mixer keeps the one line that belongs to a mixer: which
         * part is going through what.
         *
         * A band across the bottom for the same reason the tuner is one: it
         * wants to be wide. The signal runs left to right, from the instrument
         * through each plugin to the pair of ports at the end, because that is
         * the order the sound goes in and the order somebody adding a pedal is
         * thinking in.
         *
         * Drawn on ink rather than paper. Everything here is a piece of
         * equipment rather than a piece of the document, and the page is the
         * brightest thing in this window on purpose.
         */
        Rectangle {
            id: deckPanel

            Layout.fillWidth: true
            /**
             * As tall as the tallest thing on it, and no taller.
             *
             * A plugin has as many knobs as it has -- guitarix's amplifier has
             * eight and a cabinet has three -- so a fixed height is either a
             * band of empty ink or an amplifier with its bottom row cut off.
             * Capped at two fifths of the window, because the score is what
             * the window is for.
             */
            Layout.preferredHeight: Math.min(
                root.height * 0.45,
                deckHeader.implicitHeight + deckRow.implicitHeight
                    + Kirigami.Units.largeSpacing * 3)
            visible: panels.effects && session.hasScore
            color: Ink.ink

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                color: Ink.line
            }

            ColumnLayout {
                id: deckColumn

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing * 2
                anchors.rightMargin: Kirigami.Units.largeSpacing * 2
                anchors.topMargin: Kirigami.Units.largeSpacing
                anchors.bottomMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    id: deckHeader

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    QQC2.Label {
                        text: i18nc("the heading of the effects panel", "EFFECTS")
                        color: Ink.faint
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.letterSpacing: 2
                    }

                    // Whose chain it is, and how long it lasts. Said here
                    // because a rig is not written into the score -- it names
                    // plugins and paths that are facts about one machine --
                    // and somebody who spent an evening on a sound should find
                    // that out before they close the window rather than after.
                    //
                    // It is kept in a file beside the score, which means a
                    // score that has never been saved has nowhere to keep one
                    // yet. That is the one case where an evening's work can
                    // still be lost, so it is the case that says so.
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: session.filePath.length > 0
                            ? i18n("%1 \u00b7 kept beside the score", session.trackNameHere)
                            : i18n("%1 \u00b7 session only until the score is saved",
                                   session.trackNameHere)
                        color: Ink.quiet
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    ChromeToggle {
                        text: i18n("Add effect")
                        checkable: false
                        enabled: session.availableEffects.length > 0
                        onClicked: deckMenu.popup()

                        QQC2.Menu {
                            id: deckMenu
                            Repeater {
                                model: session.availableEffects
                                delegate: QQC2.MenuItem {
                                    required property var modelData
                                    text: modelData.stereo
                                        ? modelData.name
                                        : i18n("%1 (mono)", modelData.name)
                                    onTriggered: session.addEffect(modelData.uri)
                                }
                            }
                        }
                    }

                    ChromeToggle {
                        text: i18n("Remove last")
                        checkable: false
                        enabled: (root.effectsRevision, session.effectsHere.length > 0)
                        onClicked: session.removeLastEffect()
                    }
                }

                /**
                 * The chain itself, left to right, and scrolling where it is
                 * longer than the window.
                 *
                 * Nothing is folded away or summarised. A pedalboard with the
                 * third pedal's knobs hidden behind a disclosure triangle is a
                 * pedalboard you cannot see, and seeing all of it at once is
                 * the entire reason this is a band rather than a list.
                 */
                Flickable {
                    id: deck

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: deckRow.implicitWidth
                    contentHeight: deckRow.implicitHeight
                    // Sideways by design; downwards only when the window is
                    // too short for the amplifier, which the cap above allows.
                    flickableDirection: Flickable.HorizontalAndVerticalFlick
                    boundsBehavior: Flickable.StopAtBounds

                    RowLayout {
                        id: deckRow
                        spacing: Kirigami.Units.largeSpacing

                        // Where the sound comes from: the part's own badge,
                        // and whether it is recordings or a programme.
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Kirigami.Units.gridUnit
                            spacing: Kirigami.Units.smallSpacing / 2

                            Kirigami.Icon {
                                Layout.alignment: Qt.AlignHCenter
                                source: session.trackIconHereOnInk
                                implicitWidth: Kirigami.Units.iconSizes.medium
                                implicitHeight: Kirigami.Units.iconSizes.medium
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: session.samplerHere.length > 0
                                    ? i18nc("played from recordings", "SFZ")
                                    : i18nc("played from a General MIDI patch", "GM")
                                color: Ink.faint
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.letterSpacing: 1
                            }
                        }

                        QQC2.Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Kirigami.Units.gridUnit * 2
                            text: "\u2192"
                            color: Ink.edge
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
                        }

                        Repeater {
                            model: session.chainHere

                            delegate: RowLayout {
                                id: stageCard
                                required property var modelData

                                Layout.alignment: Qt.AlignTop
                                spacing: Kirigami.Units.largeSpacing

                                Rectangle {
                                    // Top-aligned, so a three-knob cabinet
                                    // sits level with the amplifier beside it
                                    // rather than floating in the middle of
                                    // its own height.
                                    Layout.alignment: Qt.AlignTop
                                    implicitWidth: stageBody.implicitWidth
                                                   + Kirigami.Units.largeSpacing * 2
                                    implicitHeight: stageBody.implicitHeight
                                                    + Kirigami.Units.largeSpacing * 2
                                    radius: Ink.radius
                                    color: Ink.well
                                    border.width: 1
                                    border.color: Ink.line

                                    ColumnLayout {
                                        id: stageBody

                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: Kirigami.Units.largeSpacing
                                        spacing: Kirigami.Units.smallSpacing

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Kirigami.Units.largeSpacing

                                            QQC2.Label {
                                                Layout.fillWidth: true
                                                text: i18n("%1 \u00b7 %2",
                                                           stageCard.modelData.stage + 1,
                                                           stageCard.modelData.name)
                                                color: Ink.paper
                                                elide: Text.ElideRight
                                                font.weight: Font.DemiBold
                                            }

                                            /**
                                             * Somebody else's ears, as a
                                             * starting point.
                                             *
                                             * A chain at its defaults is an
                                             * amplifier nobody has turned up.
                                             * These are the guitarix factory
                                             * presets, carrying the part of
                                             * each that an amplifier can hold
                                             * -- the valve, the tone stack,
                                             * the cabinet and the levels. What
                                             * they cannot carry goes to the
                                             * status line, because a voicing
                                             * missing its reverb is not the
                                             * sound on the label and should
                                             * not pretend to be.
                                             */
                                            ChromeToggle {
                                                visible: session.voicings.length > 0
                                                text: i18n("Voicings\u2026")
                                                checkable: false
                                                implicitHeight: Ink.smallControl
                                                onClicked: voicingMenu.popup()

                                                QQC2.Menu {
                                                    id: voicingMenu
                                                    Repeater {
                                                        model: session.voicings
                                                        delegate: QQC2.MenuItem {
                                                            required property var modelData
                                                            text: i18n("%1  \u2014  %2",
                                                                       modelData.name,
                                                                       modelData.summary)
                                                            enabled: modelData.amplified
                                                            onTriggered: session.applyVoicing(
                                                                stageCard.modelData.stage,
                                                                modelData.name)
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        /**
                                         * The knobs, in a block rather than a
                                         * column.
                                         *
                                         * Wrapped into rows of four, which is
                                         * about the width of an amplifier: a
                                         * plugin with a dozen controls in one
                                         * line would push everything after it
                                         * off the window, and one with three
                                         * would leave a hole.
                                         */
                                        GridLayout {
                                            Layout.alignment: Qt.AlignHCenter
                                            columns: 4
                                            columnSpacing: Kirigami.Units.smallSpacing
                                            rowSpacing: 0

                                            Repeater {
                                                model: stageCard.modelData.controls.filter(
                                                    control => !control.toggled
                                                        && control.choices.length === 0)

                                                delegate: ColumnLayout {
                                                    id: knobCell
                                                    required property var modelData

                                                    spacing: 0

                                                    InkKnob {
                                                        Layout.alignment: Qt.AlignHCenter
                                                        from: knobCell.modelData.minimum
                                                        to: knobCell.modelData.maximum
                                                        stepSize: knobCell.modelData.integer
                                                            ? 1 : 0
                                                        value: knobCell.modelData.value
                                                        onMoved: session.setEffectControl(
                                                            stageCard.modelData.stage,
                                                            knobCell.modelData.index, value)

                                                        QQC2.ToolTip.text: i18n(
                                                            "%1 \u2014 %2",
                                                            knobCell.modelData.name,
                                                            value.toFixed(2))
                                                        QQC2.ToolTip.visible: hovered
                                                        QQC2.ToolTip.delay:
                                                            Kirigami.Units.toolTipDelay
                                                    }

                                                    // Elided rather than
                                                    // wrapped: "DISTORTION" is
                                                    // one word and a knob two
                                                    // labels tall would set
                                                    // every other knob on the
                                                    // panel lower to match.
                                                    // The full name and the
                                                    // value are on the
                                                    // tooltip.
                                                    QQC2.Label {
                                                        Layout.alignment: Qt.AlignHCenter
                                                        Layout.maximumWidth:
                                                            Kirigami.Units.gridUnit * 4
                                                        text: knobCell.modelData.name.toUpperCase()
                                                        color: Ink.faint
                                                        elide: Text.ElideRight
                                                        font.pointSize:
                                                            Kirigami.Theme.smallFont.pointSize
                                                    }
                                                }
                                            }
                                        }

                                        // The switches and the named choices,
                                        // which are not knobs and should not
                                        // be drawn as one: a valve model is a
                                        // list of names, and a slider from
                                        // nought to eleven labelled nothing is
                                        // a worse way to ask which one.
                                        Repeater {
                                            model: stageCard.modelData.controls.filter(
                                                control => control.toggled
                                                    || control.choices.length > 0)

                                            delegate: RowLayout {
                                                id: pickRow
                                                required property var modelData

                                                Layout.fillWidth: true
                                                spacing: Kirigami.Units.smallSpacing

                                                QQC2.Label {
                                                    Layout.preferredWidth:
                                                        Kirigami.Units.gridUnit * 3.6
                                                    text: pickRow.modelData.name
                                                    color: Ink.faint
                                                    elide: Text.ElideRight
                                                    font.pointSize:
                                                        Kirigami.Theme.smallFont.pointSize
                                                }

                                                ChromeToggle {
                                                    visible: pickRow.modelData.toggled
                                                    text: i18n("On")
                                                    implicitHeight: Ink.smallControl
                                                    checked: pickRow.modelData.value > 0.5
                                                    onToggled: session.setEffectControl(
                                                        stageCard.modelData.stage,
                                                        pickRow.modelData.index,
                                                        checked ? 1 : 0)
                                                }

                                                ChromeToggle {
                                                    id: choiceButton
                                                    Layout.fillWidth: true
                                                    visible: !pickRow.modelData.toggled
                                                    checkable: false
                                                    implicitHeight: Ink.smallControl
                                                    /**
                                                     * The name of the choice
                                                     * the value stands for --
                                                     * found by the value,
                                                     * never by its place in
                                                     * the list. A plugin may
                                                     * number its choices 0, 2,
                                                     * 5, and lilv reports them
                                                     * in no particular order,
                                                     * so position is not an
                                                     * answer to "which one is
                                                     * this".
                                                     */
                                                    text: {
                                                        const names = pickRow.modelData.choices;
                                                        const values =
                                                            pickRow.modelData.choiceValues;
                                                        if (!names || names.length === 0)
                                                            return "";
                                                        let best = 0;
                                                        for (let i = 1; i < names.length; ++i) {
                                                            if (Math.abs(values[i]
                                                                    - pickRow.modelData.value)
                                                                < Math.abs(values[best]
                                                                    - pickRow.modelData.value)) {
                                                                best = i;
                                                            }
                                                        }
                                                        return names[best];
                                                    }
                                                    onClicked: choiceMenu.popup()

                                                    QQC2.Menu {
                                                        id: choiceMenu
                                                        Repeater {
                                                            model: pickRow.modelData.choices
                                                            delegate: QQC2.MenuItem {
                                                                required property int index
                                                                required property string modelData
                                                                text: modelData
                                                                onTriggered:
                                                                    session.setEffectControl(
                                                                        stageCard.modelData.stage,
                                                                        pickRow.modelData.index,
                                                                        pickRow.modelData
                                                                            .choiceValues[index])
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                    }
                                }

                                QQC2.Label {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: Kirigami.Units.gridUnit * 2
                                    text: "\u2192"
                                    color: Ink.edge
                                    font.pointSize:
                                        Kirigami.Theme.defaultFont.pointSize * 1.4
                                }
                            }
                        }

                        // Where it comes out. Named rather than drawn, because
                        // what happens after the chain is the fader and the
                        // pair of ports, and both of those live elsewhere.
                        QQC2.Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Kirigami.Units.gridUnit * 2.1
                            text: session.ports
                                ? i18n("stem")
                                : i18n("mix")
                            color: Ink.faint
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.letterSpacing: 1
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // An empty deck says what to do with it rather than nothing.
                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    visible: (root.effectsRevision, session.chainHere.length === 0)
                    text: session.availableEffects.length > 0
                        ? i18n("Nothing on this part yet \u2014 the amplifier goes on here.")
                        : i18n("No LV2 effects were found on this machine.")
                    color: Ink.quiet
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        /**
         * Every bar of the piece, as a ruler.
         *
         * A score is read a line at a time and navigated a bar at a time, and
         * a piece is a length of time laid out left to right -- which is what
         * a ruler is a drawing of. Bar 96 is two thirds of the way along, and
         * that is a thing to aim at rather than a thing to count to.
         *
         * It replaces a grid of numbered boxes, which worked and cost too
         * much: a hundred and seventy-six boxes at a legible size is four rows
         * and a scrollbar, and the map of the music was taking a third of the
         * window away from the music. A ruler says the same in one line.
         *
         * The playhead rides above the line and the caret sits below it, so
         * the two never cover each other -- they are usually in different
         * places and occasionally in the same one, and that is exactly when
         * both need reading.
         */
        Rectangle {
            id: barPanel

            readonly property int inset: Kirigami.Units.largeSpacing * 2
            readonly property int cushion: Kirigami.Units.largeSpacing

            /**
             * The narrowest a bar may be and still be something to aim at.
             *
             * Below this the ruler scrolls instead of squeezing. A tick four
             * pixels wide is a tick nobody can click, and a map you cannot
             * point at is a picture.
             */
            readonly property int narrowest: Math.round(Kirigami.Units.gridUnit * 0.75)

            readonly property int usable: Math.max(narrowest, width - inset * 2)
            readonly property real step: session.barCount > 0
                ? Math.max(narrowest, usable / session.barCount)
                : narrowest
            readonly property real span: step * Math.max(1, session.barCount)

            /**
             * How often a bar prints its number.
             *
             * Whatever keeps two numbers from touching, from every bar on a
             * short piece to every sixty-fourth on a long one. Chosen from the
             * width rather than fixed, because a rule that printed every
             * fourth bar would print them on top of each other at eight pixels
             * a bar and leave a bare line at eighty.
             */
            readonly property int every: {
                const wanted = Kirigami.Units.gridUnit * 2.75
                const steps = [1, 2, 4, 8, 16, 32, 64]
                for (let index = 0; index < steps.length; ++index) {
                    if (steps[index] * step >= wanted) {
                        return steps[index]
                    }
                }
                return 64
            }

            readonly property int nameRow: Kirigami.Units.gridUnit * 1.1
            readonly property int tickRow: Kirigami.Units.gridUnit * 0.8
            readonly property int numberRow: Kirigami.Units.gridUnit * 1.2
            /** Kept whether or not there is a bar in it, so nothing jumps. */
            readonly property int scrollRow: Kirigami.Units.smallSpacing * 2

            Layout.fillWidth: true
            Layout.preferredHeight: cushion + Ink.smallControl + cushion
                                    + nameRow + tickRow + numberRow + scrollRow
                                    + cushion
            visible: panels.bars && session.hasScore
            color: Ink.panelDeep

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                color: Ink.rule
            }

            /**
             * What the bar the caret is in is called.
             *
             * Over the ruler rather than in the toolbar, because a section is
             * a name given to a bar and this panel is the one about bars --
             * the names it sets are printed along the ruler underneath it. The
             * toolbar had it first and could not hold it: three fields about
             * the caret's bar and every panel toggle is more than a window
             * this size has room for on one line.
             */
            RowLayout {
                id: sectionRow

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: barPanel.inset
                anchors.rightMargin: barPanel.inset
                anchors.topMargin: barPanel.cushion
                height: Ink.smallControl
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    text: i18n("Bar %1", session.caretBar + 1)
                    color: Ink.quiet
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.weight: Font.DemiBold
                }

                QQC2.TextField {
                    id: sectionField

                    Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                    Layout.alignment: Qt.AlignVCenter
                    color: session.sectionHere.length > 0 ? Ink.ink : Ink.quiet
                    text: session.sectionHere
                    placeholderText: i18n("name this section")

                    QQC2.ToolTip.text: i18n("Name the part of the music that starts here")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    background: Rectangle {
                        radius: Ink.radius
                        color: sectionField.activeFocus ? Ink.paper : "transparent"
                        border.width: 1
                        border.color: sectionField.activeFocus ? Ink.accent : Ink.rule
                    }

                    onAccepted: {
                        session.setSectionHere(text)
                        view.forceActiveFocus()
                    }
                    onActiveFocusChanged: if (!activeFocus) {
                        text = Qt.binding(() => session.sectionHere)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            Flickable {
                id: ruler

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: sectionRow.bottom
                anchors.bottom: rulerBar.top
                anchors.topMargin: barPanel.cushion
                anchors.leftMargin: barPanel.inset
                anchors.rightMargin: barPanel.inset

                clip: true
                contentWidth: barPanel.span
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                /** Puts a bar on screen without dragging the ruler about. */
                function reveal(bar) {
                    if (bar < 0 || contentWidth <= width) {
                        return
                    }
                    const at = (bar + 0.5) * barPanel.step
                    const edge = Kirigami.Units.gridUnit * 3
                    if (at < contentX + edge) {
                        contentX = Math.max(0, at - edge)
                    } else if (at > contentX + width - edge) {
                        contentX = Math.min(contentWidth - width, at - width + edge)
                    }
                }

                Item {
                    id: rulerBody

                    width: barPanel.span
                    height: ruler.height

                    /**
                     * A mark kept inside the ruler rather than centred over
                     * its bar wherever that falls.
                     *
                     * The first bar's middle is half a tick from the left
                     * edge, which is less than half a lozenge: centred
                     * honestly, the playhead at bar 1 is a shape with its left
                     * third cut off.
                     */
                    function place(bar, width) {
                        return Math.max(0, Math.min(rulerBody.width - width,
                                                    xOf(bar) - width / 2))
                    }

                    readonly property real lineY: barPanel.nameRow + barPanel.tickRow / 2

                    /** Where the middle of a bar falls along the ruler. */
                    function xOf(bar) {
                        return (bar + 0.5) * barPanel.step
                    }

                    // The line itself, which is what makes the ticks a ruler
                    // rather than a row of marks.
                    Rectangle {
                        y: rulerBody.lineY
                        width: rulerBody.width
                        height: 1
                        color: Ink.staff
                    }

                    Repeater {
                        model: session.barCount

                        delegate: Item {
                            id: barTick
                            required property int index

                            readonly property string section: session.sectionAt(index)
                            readonly property bool playing: index === session.currentBar
                            readonly property bool atCaret: index === session.caretBar
                            /** Numbered where the spacing allows, unless it says so itself. */
                            readonly property bool numbered:
                                index % barPanel.every === 0 && !playing && !atCaret

                            x: index * barPanel.step
                            width: barPanel.step
                            height: rulerBody.height

                            // Where the score names a section, its name sits
                            // over the bar it starts: a line of numbers with
                            // no words in it is a ruler rather than a map, and
                            // the reason anybody looks for bar 96 is that it
                            // is where the second chorus begins.
                            QQC2.Label {
                                visible: barTick.section.length > 0
                                x: 0
                                y: 0
                                height: barPanel.nameRow
                                text: barTick.section
                                color: Ink.ink
                                font.italic: true
                                verticalAlignment: Text.AlignVCenter
                            }

                            Rectangle {
                                x: barPanel.step / 2
                                y: rulerBody.lineY
                                     - (barTick.section.length > 0 ? barPanel.tickRow / 2
                                                                   : barPanel.tickRow / 4)
                                width: 1
                                height: barTick.section.length > 0
                                    ? barPanel.tickRow : barPanel.tickRow / 2
                                color: barTick.section.length > 0 ? Ink.quiet : Ink.staff
                            }

                            QQC2.Label {
                                visible: barTick.numbered
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: rulerBody.lineY + barPanel.tickRow / 2
                                height: barPanel.numberRow
                                text: barTick.index + 1
                                color: Ink.faint
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.features: ({ "tnum": 1 })
                                verticalAlignment: Text.AlignVCenter
                            }

                            // The whole column, so a bar is as easy to hit as
                            // the ruler is tall rather than as easy to hit as
                            // a one-pixel tick.
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    session.goToBar(barTick.index)
                                    // Jumping to a bar is usually the first
                                    // half of writing something in it.
                                    view.forceActiveFocus()
                                }
                            }
                        }
                    }

                    /**
                     * The bar being played, above the line.
                     *
                     * A lozenge rather than a line, because it carries its own
                     * number: a playhead that says where it is saves looking
                     * along the ruler to find out.
                     */
                    Rectangle {
                        id: playhead

                        visible: session.currentBar >= 0
                        x: rulerBody.place(session.currentBar, width)
                        y: rulerBody.lineY - height - 2
                        width: Math.max(height, playheadLabel.implicitWidth
                                                + Kirigami.Units.largeSpacing)
                        height: Kirigami.Units.gridUnit * 1.15
                        radius: height / 2
                        color: Ink.accent

                        QQC2.Label {
                            id: playheadLabel
                            anchors.centerIn: parent
                            text: session.currentBar + 1
                            color: Ink.paper
                            font.weight: Font.DemiBold
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.features: ({ "tnum": 1 })
                        }
                    }

                    /** Where the caret is, below the line and in the numbers' row. */
                    Rectangle {
                        id: caretMark

                        visible: session.caretBar >= 0
                        x: rulerBody.place(session.caretBar, width)
                        y: rulerBody.lineY + barPanel.tickRow / 2
                        width: Math.max(height, caretLabel.implicitWidth
                                                + Kirigami.Units.smallSpacing * 2)
                        height: barPanel.numberRow
                        radius: Ink.radius
                        color: session.caretBar === session.currentBar
                            ? Ink.accentTint : "transparent"
                        border.width: 1.6
                        border.color: Ink.accent

                        QQC2.Label {
                            id: caretLabel
                            anchors.centerIn: parent
                            text: session.caretBar + 1
                            color: Ink.accentDeep
                            font.weight: Font.DemiBold
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.features: ({ "tnum": 1 })
                        }
                    }
                }

                // Following the music, and following the caret: both put a bar
                // in view, and neither drags the ruler about while the other
                // is what somebody is watching.
                Connections {
                    target: session

                    function onPositionChanged() {
                        if (session.playing) {
                            ruler.reveal(session.currentBar)
                        }
                    }

                    function onCursorMoved() {
                        if (!session.playing) {
                            ruler.reveal(session.caretBar)
                        }
                    }
                }
            }

            /**
             * How much of the piece is on screen, and where.
             *
             * Drawn rather than attached to the Flickable, for two reasons:
             * an attached one is laid over the bottom of what it scrolls,
             * which here is the row the bar numbers are printed in, and it
             * comes out of the desktop's style in the desktop's colours --
             * which is the thing this window is deliberately not doing.
             */
            Rectangle {
                id: rulerBar

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: barPanel.inset
                anchors.rightMargin: barPanel.inset
                anchors.bottomMargin: barPanel.cushion
                height: Kirigami.Units.smallSpacing
                radius: height / 2
                visible: ruler.contentWidth > ruler.width + 1
                color: Ink.rule

                Rectangle {
                    x: parent.width * ruler.contentX / Math.max(1, ruler.contentWidth)
                    width: Math.max(parent.height * 3,
                                    parent.width * ruler.width
                                        / Math.max(1, ruler.contentWidth))
                    height: parent.height
                    radius: parent.radius
                    color: Ink.quiet
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -Kirigami.Units.smallSpacing
                    anchors.bottomMargin: -Kirigami.Units.smallSpacing
                    cursorShape: Qt.PointingHandCursor

                    function scrollTo(x) {
                        const fraction = Math.max(0, Math.min(1, x / width))
                        ruler.contentX = Math.max(
                            0, Math.min(ruler.contentWidth - ruler.width,
                                        fraction * ruler.contentWidth - ruler.width / 2))
                    }
                    onPressed: mouse => scrollTo(mouse.x)
                    onPositionChanged: mouse => { if (pressed) scrollTo(mouse.x) }
                }
            }
        }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Kirigami.Units.gridUnit * 1.9
        // A problem overrides the panel toggle. The bar is closeable and is
        // remembered closed, which is right for chatter and wrong for "there
        // is no SoundFont, so nothing you press will make a sound": that has
        // to reach somebody who closed this bar last week.
        visible: panels.status || session.problem !== ""
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

            // The problem when there is one, the chatter when there is not.
            // In the accent rather than in Ink.faint, because a reason the
            // program is silent should not be set in the colour reserved for
            // bar numbers and things nobody needs to read.
            QQC2.Label {
                Layout.fillWidth: true
                text: session.problem !== "" ? session.problem : session.status
                color: session.problem !== "" ? Ink.accentOnInk : Ink.faint
                font.weight: session.problem !== "" ? Font.DemiBold : Font.Normal
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
