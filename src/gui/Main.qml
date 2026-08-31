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

    /** And the knobs and voicings, as typed: `0:0:Drive=0.8`, `0:0=Iron Man`. */
    property var initialKnobs: []
    property var initialVoicings: []

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

    /**
     * Which stage is on the bench.
     *
     * The chain is a strip of tiles and the knobs belong to one of them, so
     * something has to say which -- and it is a number here rather than a flag
     * on the stage, because a stage is a plain value out of `chainHere` and is
     * built again every time the chain changes. An index survives that;
     * anything written onto the value does not.
     *
     * Not clamped here. A chain that shortens under it leaves this pointing
     * past the end for as long as nobody looks, and `deckPanel.stage` is what
     * looks -- one place that reads it, one place that has to be right.
     */
    property int pickedStage: 0

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
        property bool scale: false
        property bool chords: false

        /**
         * Which side the parts list is on, and so which side the mixer is.
         *
         * The two are one setting because they are two ends of one row: the
         * panel saying which part you are looking at and the panel saying what
         * it sounds like belong on opposite sides of the score, and swapping
         * one is swapping both. Left by default, which is where a list of
         * things has been since before any of this.
         */
        property bool tracksLeft: true
    }

    /**
     * How big the page is drawn.
     *
     * Nought means nobody has said, which is not the same as 1.0 and is why it
     * is not simply defaulted to one: a page is a fixed width now, so on a
     * window twice as wide as A4 a first run at 100% is a column of music with
     * a desk either side of it and no indication that the thing can be
     * resized. Unset, the view fits the page to the window once; after that it
     * is whatever it was last left at, because the size somebody reads music
     * at is a property of their eyes and their music stand.
     */
    Settings {
        id: viewState
        category: "View"
        property real zoom: 0
    }

    /**
     * The controller, remembered.
     *
     * Somebody who plugs a surface in works with it plugged in, and choosing
     * it again every morning is the sort of thing that makes a feature not
     * worth having. Empty means none, and a port that has gone away since is
     * refused on the way in and says so, which is the same answer as any other
     * missing device.
     */
    Settings {
        id: surfaceState
        category: "Surface"
        property string port: ""
    }

    Session {
        id: session

        Component.onCompleted: {
            if (surfaceState.port.length > 0) {
                listenOnSurface(surfaceState.port)
            }
        }

        effectsShown: panels.effects
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
        // A different part is a different chain, and the bench should be
        // standing on the first thing on it rather than on whatever number
        // happened to be selected on the part before.
        function onCurrentTrackChanged() {
            root.pickedStage = 0
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
            // After the rig, because a knob names a control on a plugin that
            // the rig is what loaded.
            if (initialKnobs.length > 0 || initialVoicings.length > 0) {
                session.applyKnobs(initialKnobs, initialVoicings)
                root.effectsRevision++
            }
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

    /**
     * One key on the circle of fifths.
     *
     * Round, because the thing it is arranged in is round and a row of
     * rectangles laid out on a circle reads as neither. The relative minors
     * sit on an inner ring and are drawn smaller and in lower case, which is
     * how harmony has written the difference between a major and a minor since
     * long before any of this.
     */
    component KeySpot: QQC2.AbstractButton {
        id: keySpot

        property bool picked: false
        property bool small: false

        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitWidth: small ? Kirigami.Units.gridUnit * 1.8 : Kirigami.Units.gridUnit * 2.3
        implicitHeight: implicitWidth

        background: Rectangle {
            radius: width / 2
            color: keySpot.picked
                ? Ink.accent
                : (keySpot.hovered ? Ink.rule : Ink.panel)
            border.width: 1
            border.color: keySpot.picked ? Ink.accent : Ink.rule
        }

        contentItem: QQC2.Label {
            text: keySpot.text
            color: keySpot.picked ? Ink.paper : Ink.ink
            font.pointSize: keySpot.small
                ? Kirigami.Theme.smallFont.pointSize
                : Kirigami.Theme.defaultFont.pointSize
            font.weight: keySpot.picked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    /**
     * One chord of a key, and the button that writes it.
     *
     * The degree above the name, because the degree is the part that is true
     * of every key and the name is the part somebody has to read to play it.
     * A chord this instrument cannot hold anywhere is shown and disabled
     * rather than hidden: a gap where a `V` should be is a puzzle, and "this
     * bass cannot hold that" is an answer.
     */
    component ChordButton: QQC2.AbstractButton {
        id: chordButton

        property bool borrowed: false

        enabled: modelData.playable
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitWidth: Kirigami.Units.gridUnit * 3.6
        implicitHeight: Kirigami.Units.gridUnit * 2.6
        opacity: enabled ? 1 : 0.4

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: enabled
            ? i18n("Write %1 at the caret", modelData.name)
            : i18n("%1 cannot be played on this instrument", modelData.name)

        onClicked: session.insertChord(modelData.root, modelData.quality)

        background: Rectangle {
            radius: Ink.radius
            color: chordButton.hovered && chordButton.enabled ? Ink.rule : Ink.panel
            border.width: 1
            border.color: chordButton.borrowed ? Ink.rule : Ink.quiet
        }

        contentItem: ColumnLayout {
            spacing: 0
            QQC2.Label {
                Layout.fillWidth: true
                text: modelData.degree
                color: Ink.quiet
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                horizontalAlignment: Text.AlignHCenter
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: modelData.name
                color: Ink.ink
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
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
     * A control's value, written out.
     *
     * Two decimals for everything was hiding the thing that matters, which is
     * not the value but its scale: `20.00` was distortion four fifths of the
     * way up a range of one to a hundred and, on the knob beside it, a cabinet
     * halfway up a range of one to twenty. Both read as the same number.
     *
     * So the figures follow the span. A control that runs across a hundred
     * has nothing to say in its hundredths, and one that runs from 0.01 to 1
     * has nothing else to say. The unit goes on the end where the plugin
     * declared one, which is seldom: of the plugins on this machine most
     * declare none at all, guitarix's amplifier among them, and inventing a
     * "dB" for a port that never claimed one would be this window asserting
     * something it was not told.
     */
    function reading(control) {
        if (control.integer) {
            return Math.round(control.value) + (control.unit ? " " + control.unit : "")
        }
        const span = control.maximum - control.minimum
        const places = span >= 100 ? 0 : (span >= 10 ? 1 : 2)
        return control.value.toFixed(places) + (control.unit ? " " + control.unit : "")
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

    /**
     * The tape a voicing came on.
     *
     * A voicing is knob positions and nothing else, so the knobs beside this
     * already are the sound. What they are not is a name: a chain reopened
     * tomorrow is a row of numbers unless something says what it was built
     * from, and a label somebody wrote on a cassette is exactly the kind of
     * thing that says it.
     *
     * Drawn at a fixed 260 by 164 and scaled, so every measurement below is
     * the one from the design and none of them drift when the card resizes.
     */
    component VoicingTape: Item {
        id: tape

        property string name

        readonly property real unit: width / 260

        implicitWidth: Kirigami.Units.gridUnit * 13
        implicitHeight: implicitWidth * 164 / 260

        Rectangle {
            anchors.fill: parent
            radius: 10 * tape.unit
            color: Ink.shell
            border.width: 1
            border.color: Ink.line
        }

        // The four screws that hold a shell together.
        Repeater {
            model: [[14, 14], [246, 14], [14, 150], [246, 150]]

            delegate: Rectangle {
                required property var modelData

                x: (modelData[0] - 2.5) * tape.unit
                y: (modelData[1] - 2.5) * tape.unit
                width: 5 * tape.unit
                height: width
                radius: width / 2
                color: Ink.recess
            }
        }

        // The paper label, which is the whole reason a cassette can be named.
        Rectangle {
            x: 22 * tape.unit
            y: 14 * tape.unit
            width: 216 * tape.unit
            height: 92 * tape.unit
            radius: 5 * tape.unit
            color: Ink.paper

            QQC2.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 12 * tape.unit
                text: i18nc("printed on the label of a voicing tape",
                            "GUITARIX VOICING")
                color: Ink.quiet
                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                font.letterSpacing: 2.2 * tape.unit
            }

            QQC2.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.margins: 8 * tape.unit
                y: 30 * tape.unit
                width: parent.width - 16 * tape.unit
                text: tape.name
                color: Ink.ink
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.italic: true
                font.weight: Font.DemiBold
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.05
            }

            // The window, and the two reels behind it: one nearly full and one
            // nearly empty, which is what a tape part way through looks like.
            Rectangle {
                x: 18 * tape.unit
                y: 50 * tape.unit
                width: 180 * tape.unit
                height: 34 * tape.unit
                radius: height / 2
                color: Ink.ink

                Repeater {
                    model: [[68, 14], [156, 9]]

                    delegate: Item {
                        required property var modelData

                        x: modelData[0] * tape.unit
                        y: 17 * tape.unit

                        Rectangle {
                            x: -modelData[1] * tape.unit
                            y: -modelData[1] * tape.unit
                            width: modelData[1] * 2 * tape.unit
                            height: width
                            radius: width / 2
                            color: Ink.spool
                        }
                        Rectangle {
                            x: -7 * tape.unit
                            y: -7 * tape.unit
                            width: 14 * tape.unit
                            height: width
                            radius: width / 2
                            color: Ink.paper
                        }
                        Rectangle {
                            x: -2.6 * tape.unit
                            y: -2.6 * tape.unit
                            width: 5.2 * tape.unit
                            height: width
                            radius: width / 2
                            color: Ink.ink
                        }
                    }
                }
            }

            // The stripe along the bottom of the label, in the accent: the one
            // thing on the tape that says this is Fretwork's and not a prop.
            Rectangle {
                x: 0
                y: 85 * tape.unit
                width: parent.width
                height: 7 * tape.unit
                color: Ink.accent
            }
        }

        // The lower half of the shell, narrower than the top, the way the
        // bottom of a cassette is.
        Canvas {
            anchors.fill: parent
            onPaint: {
                const context = getContext("2d")
                const u = tape.unit
                context.reset()
                context.beginPath()
                context.moveTo(78 * u, 162 * u)
                context.lineTo(88 * u, 122 * u)
                context.lineTo(172 * u, 122 * u)
                context.lineTo(182 * u, 162 * u)
                context.closePath()
                context.fillStyle = Ink.well
                context.fill()
                context.strokeStyle = Ink.line
                context.lineWidth = 1
                context.stroke()
            }
            Component.onCompleted: requestPaint()
            onWidthChanged: requestPaint()
        }

        Repeater {
            model: [104, 156]

            delegate: Rectangle {
                required property int modelData

                x: (modelData - 4) * tape.unit
                y: 142 * tape.unit
                width: 8 * tape.unit
                height: width
                radius: width / 2
                color: Ink.recess
            }
        }
    }

    /**
     * A four by twelve, drawn because a cabinet is a box with speakers in it
     * and a row of knobs says none of that.
     *
     * The same fixed geometry and scaling as the tape, from the same design.
     */
    component SpeakerCabinet: Item {
        id: cabinet

        readonly property real unit: width / 120

        implicitWidth: Kirigami.Units.gridUnit * 6
        implicitHeight: implicitWidth * 110 / 120

        Rectangle {
            x: 3 * cabinet.unit
            y: 3 * cabinet.unit
            width: 114 * cabinet.unit
            height: 104 * cabinet.unit
            radius: 6 * cabinet.unit
            color: Ink.shell
            border.width: 1
            border.color: Ink.line
        }

        // The grille cloth, sunk into the front.
        Rectangle {
            x: 12 * cabinet.unit
            y: 12 * cabinet.unit
            width: 96 * cabinet.unit
            height: 86 * cabinet.unit
            radius: 3 * cabinet.unit
            color: Ink.ink
            border.width: 1
            border.color: Ink.line
        }

        Repeater {
            model: [[37, 34], [83, 34], [37, 76], [83, 76]]

            delegate: Item {
                required property var modelData

                x: modelData[0] * cabinet.unit
                y: modelData[1] * cabinet.unit

                // The cone, the surround, and the dust cap.
                Rectangle {
                    x: -18 * cabinet.unit
                    y: -18 * cabinet.unit
                    width: 36 * cabinet.unit
                    height: width
                    radius: width / 2
                    color: Ink.recess
                    border.width: 1
                    border.color: Ink.edge
                }
                Rectangle {
                    x: -11 * cabinet.unit
                    y: -11 * cabinet.unit
                    width: 22 * cabinet.unit
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Ink.line
                }
                Rectangle {
                    x: -4.5 * cabinet.unit
                    y: -4.5 * cabinet.unit
                    width: 9 * cabinet.unit
                    height: width
                    radius: width / 2
                    color: Ink.line
                }
            }
        }
    }

    /**
     * One plugin on the board: where in the signal it sits, what it is, and
     * whether its knobs are the ones showing.
     *
     * Small on purpose. A tile carries the two things somebody scanning a
     * chain wants and nothing else, because the whole strip has to fit across
     * the window for the chain to be readable at a glance. What it does not
     * carry is the plugin's controls: those are on the bench below, once, at
     * the width of the window, rather than nine times over in nine cards.
     *
     * The accent says which one, as it does everywhere else here. A border
     * alone was not enough at this size -- one pixel of magenta on ink reads
     * as a slightly different grey -- so the bottom edge is a bar of it,
     * which is the tile pointing at the bench.
     */
    component StageTile: QQC2.AbstractButton {
        id: stageTile

        /** Whose knobs are on the bench. */
        property bool current: false

        /** The line under the name: what it was set from, or how it is run. */
        property string caption: ""

        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        padding: Kirigami.Units.largeSpacing
        // Room under the text for the bar, so the accent is an edge of the
        // tile rather than an underline through its second line.
        bottomPadding: Kirigami.Units.largeSpacing + 3
        implicitWidth: Math.max(Kirigami.Units.gridUnit * 9,
                                tileBody.implicitWidth + leftPadding + rightPadding)

        background: Rectangle {
            radius: Ink.radius
            color: stageTile.current
                ? Ink.well
                : (stageTile.hovered ? Qt.rgba(0.95, 0.95, 0.95, 0.08) : "transparent")
            border.width: 1
            border.color: stageTile.current ? Ink.accent : Ink.line

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 1
                height: 3
                visible: stageTile.current
                color: Ink.accent
            }
        }

        contentItem: ColumnLayout {
            id: tileBody
            spacing: 0

            QQC2.Label {
                Layout.fillWidth: true
                text: stageTile.text
                color: stageTile.current ? Ink.paper : Ink.staff
                elide: Text.ElideRight
                font.weight: Font.DemiBold
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: stageTile.caption
                color: stageTile.current ? Ink.faint : Ink.quiet
                elide: Text.ElideRight
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }

    /** S and M: small, square, and lit when they are doing something. */
    /**
     * A control on one of the paper panels that opens a menu: what instrument
     * a part is, and which recordings it is played from.
     *
     * An `AbstractButton` and not a `QQC2.Button`, which is not a matter of
     * taste. The desktop style paints a Button through QStyle -- its text
     * included -- from the *background* delegate, so giving one a `contentItem`
     * to elide a long name with does not replace the label, it adds a second
     * one. The programme name was being drawn twice, in two fonts, both
     * centred: a ghost to the left of the first letter, a ghost to the right of
     * the last, and a smear in the middle where the two copies crossed. It read
     * as a blurred font, and it was two of them.
     *
     * Using the window's own button also settles the other half of the
     * complaint, which is that these two were the only controls in the panel
     * wearing the desktop's clothes rather than the program's.
     */
    component PanelButton: QQC2.AbstractButton {
        id: panelButton

        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitHeight: Ink.smallControl

        QQC2.ToolTip.visible: hovered && panelButton.QQC2.ToolTip.text !== ""
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        background: Rectangle {
            radius: Ink.radius
            color: panelButton.hovered ? Qt.rgba(0.13, 0.12, 0.11, 0.08) : "transparent"
            border.width: 1
            border.color: Qt.rgba(0.13, 0.12, 0.11, 0.16)
        }

        // A programme name is as long as somebody named it, and the panel is as
        // wide as it is.
        contentItem: QQC2.Label {
            text: panelButton.text
            color: Ink.ink
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

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

            // The neck over the page, with the key of the piece marked on it.
            // Off by default: somebody opening a tab to read it is reading the
            // tab, and this covers the bottom of it.
            ChromeToggle {
                text: i18n("Scale")
                enabled: session.hasScore && session.soundingKey !== ""
                checked: panels.scale
                onToggled: panels.scale = checked
            }

            // The circle, and the chords of whatever it is turned to.
            ChromeToggle {
                text: i18n("Chords")
                enabled: session.hasScore
                checked: panels.chords
                onToggled: panels.chords = checked
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

            // Which way round the two of them go. Beside the switches that say
            // whether a panel is there at all, because "is the mixer showing"
            // and "which side is it on" are the same question asked twice.
            ChromeToggle {
                text: "\u21c4"
                checkable: false
                enabled: session.hasScore && (panels.tracks || panels.mixer)
                onClicked: panels.tracksLeft = !panels.tracksLeft

                QQC2.ToolTip.text: panels.tracksLeft
                    ? i18n("Put the parts on the right")
                    : i18n("Put the parts on the left")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
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

            /**
             * Read right to left when somebody wants the parts on the right.
             *
             * Mirroring rather than moving. The three things in this row --
             * the parts, the score, the mixer -- are laid out in the order
             * they are written, and QML has no way to write them in a
             * different order at run time short of pulling each panel out into
             * a component of its own and loading them into ordered slots.
             * That is a large change to a large file, and the ids these panels
             * reach across each other for would all have to be untangled
             * first, which is a refactor with a swap hidden inside it rather
             * than a swap.
             *
             * `childrenInherit` is false on purpose: this reverses the order of
             * the row and nothing else. A mixer whose faders ran right to left
             * because its panel had moved would be a different fault from the
             * one being fixed.
             */
            LayoutMirroring.enabled: !panels.tracksLeft
            LayoutMirroring.childrenInherit: false

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
                        PanelButton {
                            Layout.fillWidth: true
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
                    PanelButton {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        visible: session.stringsHere > 0 || session.instrumentHere.length > 0
                        text: session.samplerHere.length > 0
                            ? i18n("Samples: %1", session.samplerHere)
                            : i18n("Samples: General MIDI")
                        onClicked: samplesMenu.popup()

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
                    fretboardShown: panels.scale

                    Component.onCompleted: {
                        if (viewState.zoom > 0) {
                            zoom = viewState.zoom
                        }
                    }
                    // Only until somebody has an opinion of their own, and
                    // then never again: a view that re-fitted itself on every
                    // resize would take the reader's zoom away every time they
                    // moved the window.
                    onWidthChanged: {
                        if (viewState.zoom <= 0 && width > 0) {
                            // Capped, so that a first run on a wide monitor
                            // still shows a page with desk either side of it.
                            // Filling the window edge to edge is a thing to
                            // ask for -- the percentage is the button -- and a
                            // poor thing to open on, because a page with no
                            // desk around it is not visibly a page.
                            zoom = Math.min(zoomToFit(), 1.5)
                        }
                    }

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
        /**
         * The circle of fifths, as a control rather than as a diagram.
         *
         * Turning it picks a key; the seven chords of that key are the row
         * under it, and pressing one writes it into the score at the caret.
         * The circle is the arrangement musicians already have in their heads
         * -- neighbours on it are the keys that sound like neighbours -- so it
         * is worth the trigonometry to draw it round rather than as a list of
         * twelve.
         *
         * It opens on whatever key the piece sounds like, since that is the
         * one somebody writing into this piece almost certainly wants.
         */
        Rectangle {
            id: chordsPanel

            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 15
            visible: panels.chords && session.hasScore
            color: Ink.panelDeep

            // Kept on the session rather than here, because the neck drawn
            // over the score reads it too: a window offering the chords of G
            // major while showing the scale of C minor is a window arguing
            // with itself. Unset it is whatever the piece sounds like.
            readonly property int accidentals: session.workingAccidentals()
            readonly property bool minor: session.workingMinor()

            RowLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing * 2
                spacing: Kirigami.Units.gridUnit * 2

                // ---- the circle ----
                Item {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 13
                    Layout.fillHeight: true

                    Repeater {
                        // Twelve positions, each carrying the major key on the
                        // outside and its relative minor within -- which is
                        // the pair that shares a signature, and the one thing
                        // about the circle worth drawing twice.
                        model: 12
                        Item {
                            anchors.fill: parent
                            // Twelve o'clock is no accidentals, and clockwise
                            // adds sharps, which is how every circle of fifths
                            // ever printed is arranged.
                            readonly property int accidentals: index > 6 ? index - 12 : index
                            readonly property real angle: index * Math.PI / 6 - Math.PI / 2
                            readonly property real outer: Math.min(parent.width, parent.height) / 2 - 16
                            readonly property real inner: outer - Kirigami.Units.gridUnit * 2.1

                            KeySpot {
                                x: parent.width / 2 + Math.cos(parent.angle) * parent.outer - width / 2
                                y: parent.height / 2 + Math.sin(parent.angle) * parent.outer - height / 2
                                text: session.keyName(parent.accidentals, false).split(" ")[0]
                                picked: !chordsPanel.minor && chordsPanel.accidentals === parent.accidentals
                                onPressed: session.setWorkingKey(parent.accidentals, false)
                            }

                            KeySpot {
                                x: parent.width / 2 + Math.cos(parent.angle) * parent.inner - width / 2
                                y: parent.height / 2 + Math.sin(parent.angle) * parent.inner - height / 2
                                small: true
                                text: session.keyName(parent.accidentals, true).split(" ")[0].toLowerCase()
                                picked: chordsPanel.minor && chordsPanel.accidentals === parent.accidentals
                                onPressed: session.setWorkingKey(parent.accidentals, true)
                            }
                        }
                    }
                }

                // ---- what that key is made of ----
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: session.keyName(chordsPanel.accidentals, chordsPanel.minor)
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.5
                        font.weight: Font.DemiBold
                        color: Ink.ink
                    }

                    QQC2.Label {
                        text: i18n("Writes at the caret, near the hand where there is one")
                        color: Ink.quiet
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing
                        Repeater {
                            model: session.chordsOf(chordsPanel.accidentals, chordsPanel.minor)
                            ChordButton {}
                        }
                    }

                    QQC2.Label {
                        Layout.topMargin: Kirigami.Units.largeSpacing
                        text: i18n("Borrowed from %1",
                                   session.parallelKeyName(chordsPanel.accidentals, chordsPanel.minor))
                        color: Ink.quiet
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Repeater {
                            model: session.borrowedFrom(chordsPanel.accidentals, chordsPanel.minor)
                            ChordButton { borrowed: true }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

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

            /** Room at the far edge for an overlay scrollbar to lie in. */
            readonly property int barRoom: Kirigami.Units.gridUnit

            /**
             * Which stage the bench is standing on, or -1 for an empty chain.
             *
             * The one place `root.pickedStage` is read, and the place it is
             * made safe: taking a plugin off shortens the chain under a number
             * that was right a moment earlier, and clamping where it is used
             * means every reader gets a stage that exists rather than each of
             * them remembering to check.
             */
            readonly property int stage: {
                root.effectsRevision
                const count = session.chainHere.length
                return count === 0
                    ? -1
                    : Math.max(0, Math.min(root.pickedStage, count - 1))
            }

            /** And the stage itself, which is what the bench is drawn from. */
            readonly property var benched: {
                root.effectsRevision
                return deckPanel.stage < 0
                    ? null
                    : session.chainHere[deckPanel.stage]
            }

            /**
             * What the bench has a picture of, asked here rather than of the
             * drawings themselves.
             *
             * `visible` is not a property an item can be asked about from
             * outside: what comes back is the effective visibility, which is
             * the item's own answer and every parent's together. A column that
             * showed itself when its cassette was visible was therefore a
             * column that never showed itself -- hidden, so its cassette read
             * as hidden, so it stayed hidden. The conditions belong up here,
             * where nothing is asking a drawing whether it is on screen.
             */
            readonly property string voicingOnBench: {
                root.effectsRevision
                return deckPanel.benched
                    ? session.voicingOn(deckPanel.benched.stage)
                    : ""
            }

            /**
             * A cabinet, by URI rather than by name -- a name is what somebody
             * called a plugin and a URI is what it is.
             */
            readonly property bool cabinetOnBench: deckPanel.benched !== null
                && String(deckPanel.benched.uri).toLowerCase().includes("cab")

            /**
             * As far as the bench may go, which is not the same as how tall
             * it is.
             *
             * A ceiling rather than a height. What the old band got wrong was
             * not that it fitted its contents -- it is that its contents were
             * every plugin at once, so a chain with an amplifier in it cost
             * the score half the window whether or not the amplifier was the
             * one being turned. One plugin at a time is small enough that
             * fitting it is affordable again.
             *
             * So a cabinet's three knobs are three knobs tall and an
             * amplifier's nine are two rows, and the band moves between them.
             * The ceiling is what stops that mattering: two rows of knobs is
             * as far as it goes, a plugin with more scrolls inside the bench,
             * and the score cannot be squeezed further by a plugin nobody has
             * heard of. Fixing the height instead would hold the band still
             * at the price of a hand's depth of empty ink under every small
             * plugin, which is a band of nothing to look at in the panel that
             * is meant to be equipment.
             */
            readonly property int benchCap: Kirigami.Units.gridUnit * 8.8

            Layout.fillWidth: true
            /**
             * Counted rather than guessed: the two margins `deckColumn` puts
             * above and below itself, and one gap for each seam between the
             * things stacked in it. Four of them are visible at once -- the
             * header, the board, the bench and the footer -- and the bench and
             * the empty-deck line are never both showing, so the count does
             * not change with the chain.
             */
            readonly property int deckGaps: Kirigami.Units.largeSpacing
                * (deckNotes.visible ? 5 : 4)

            /**
             * Everything on the band, which is now a figure the plugins have
             * no say in.
             *
             * It used to be as tall as the tallest card on it, capped at over
             * half the window, because a card was a plugin's entire front
             * panel and guitarix's amplifier is nine knobs, two lists and two
             * switches. Splitting the chain from the controls takes the plugin
             * out of the arithmetic: the board is one row of tiles whatever is
             * on it, the bench is one plugin and never more than `benchCap`,
             * and the deck is those two plus the header and the footer.
             *
             * The cap stays. A desktop font at three times the size scales
             * every gridUnit in the sum, and a fixed height fixed at more than
             * the window is not a height.
             */
            readonly property int deckNeed:
                deckHeader.implicitHeight
                    + boardRow.implicitHeight + deckPanel.barRoom
                    + (benchBlock.visible ? benchBlock.implicitHeight
                                          : emptyDeck.implicitHeight)
                    + (deckNotes.visible ? deckNotes.implicitHeight : 0)
                    + deckPanel.deckGaps

            Layout.preferredHeight: Math.min(root.height * 0.55, deckPanel.deckNeed)
            /**
             * And the height it asks for is the height it keeps.
             *
             * A preferred height on its own is a suggestion, and on a window
             * short enough for the column to run out of room the deck was the
             * thing that yielded -- squeezed to about half what it had asked
             * for, which showed as an amplifier with its bottom row of knobs
             * sliced through. The cap above was never reached; the band never
             * got what the cap allowed.
             *
             * It is the right thing to yield last rather than first. The deck
             * is shut by default and somebody looking at it has just opened
             * it, whereas the score above it is legible at any height and goes
             * on being a score when it is short.
             *
             * What it must not be is the preferred height above, though that
             * is the obvious thing to write. A minimum is what the window may
             * not be made smaller than, so a minimum reading `root.height`
             * makes the window's least size depend on the window's size: it
             * settles somewhere large and the window will not shrink past it
             * afterwards. This one asks only what is on the band, and stops at
             * a figure of its own so that a plugin with thirty knobs cannot
             * set a floor under the whole window. Past that the deck yields
             * again and the bars say what has gone under the fold.
             */
            Layout.minimumHeight: Math.min(deckPanel.deckNeed,
                                           Kirigami.Units.gridUnit * 24)
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

                    /**
                     * A controller, driving the knobs of whatever is on this
                     * bench.
                     *
                     * Here rather than in a panel of its own because this is
                     * what the eight encoders address: what a hand finds under
                     * the third encoder is what the eye finds third along this
                     * row, and a control for that belongs beside it.
                     */
                    QQC2.Label {
                        text: session.surfaceListening
                            ? session.surfaceStatus
                            : i18n("Surface")
                        color: session.surfaceListening ? Ink.accentDeep : Ink.quiet
                        elide: Text.ElideRight
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 14
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    ChromeToggle {
                        text: session.surfaceListening ? i18n("Listening") : i18n("MIDI...")
                        checkable: false
                        checked: session.surfaceListening
                        onClicked: {
                            if (session.surfaceListening) {
                                session.stopListeningOnSurface()
                                surfaceState.port = ""
                            } else {
                                surfaceMenu.ports = session.midiPorts()
                                surfaceMenu.popup()
                            }
                        }

                        QQC2.Menu {
                            id: surfaceMenu
                            property var ports: []

                            QQC2.MenuItem {
                                // Said rather than left as an empty menu: a
                                // machine with nothing plugged in and a
                                // machine built without PipeWire look the same
                                // from here, and neither is a bug.
                                text: i18n("No MIDI ports found")
                                enabled: false
                                visible: surfaceMenu.ports.length === 0
                                height: visible ? implicitHeight : 0
                            }

                            Instantiator {
                                model: surfaceMenu.ports
                                delegate: QQC2.MenuItem {
                                    required property string modelData
                                    text: modelData
                                    onTriggered: {
                                    session.listenOnSurface(modelData)
                                    surfaceState.port = modelData
                                }
                                }
                                onObjectAdded: (index, object) => surfaceMenu.insertItem(index + 1, object)
                                onObjectRemoved: (index, object) => surfaceMenu.removeItem(object)
                            }
                        }
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
                                    // Onto the end of the chain and onto the
                                    // bench. Somebody who has just put a
                                    // pedal on wants its knobs, and the one
                                    // thing they cannot want is the knobs of
                                    // whatever they were looking at before.
                                    onTriggered: {
                                        session.addEffect(modelData.uri)
                                        root.pickedStage = session.chainHere.length - 1
                                    }
                                }
                            }
                        }
                    }

                    /**
                     * The rigs kept under a name, and the way to keep one.
                     *
                     * The rig beside the score is what stops an evening's work
                     * being lost, and it is tied to one transcription. A sound
                     * is not: an amplifier somebody spent that evening on
                     * belongs to them rather than to the song it was made for,
                     * and the only way to have it on the next one was to build
                     * it again from memory.
                     *
                     * One part's chain rather than the whole score's, because
                     * a rig is put on a part. Which part it was made on is an
                     * accident of where somebody happened to be standing.
                     */
                    ChromeToggle {
                        text: i18n("Rigs\u2026")
                        checkable: false
                        onClicked: rigMenu.popup()

                        QQC2.Menu {
                            id: rigMenu

                            QQC2.MenuItem {
                                text: i18n("Keep this chain as\u2026")
                                enabled: (root.effectsRevision,
                                          session.effectsHere.length > 0)
                                onTriggered: {
                                    rigName.text = ""
                                    rigName.visible = true
                                    rigName.forceActiveFocus()
                                }
                            }

                            QQC2.MenuSeparator { visible: session.rigNames.length > 0 }

                            Repeater {
                                model: session.rigNames
                                delegate: QQC2.MenuItem {
                                    required property string modelData
                                    text: modelData
                                    onTriggered: session.applyNamedRig(modelData)
                                }
                            }
                        }
                    }

                    /**
                     * Where the name is typed, in the band rather than over it.
                     *
                     * A modal box for one short word would be the only modal
                     * box in this window, and the parts list already asks for
                     * a name the same way: a field that appears where the
                     * thing being named is, and goes when the work is done.
                     * Escape leaves without keeping anything, because a person
                     * who opened this by mistake should not have to name
                     * something to get out of it.
                     */
                    QQC2.TextField {
                        id: rigName

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                        visible: false
                        color: Ink.paper
                        placeholderText: i18n("Name this rig")

                        background: Rectangle {
                            radius: Ink.radius
                            color: "transparent"
                            border.width: 1
                            border.color: rigName.activeFocus ? Ink.accent : Ink.line
                        }

                        onAccepted: {
                            if (session.saveRigAs(text).length > 0) {
                                visible = false
                                view.forceActiveFocus()
                            }
                        }
                        Keys.onEscapePressed: {
                            visible = false
                            view.forceActiveFocus()
                        }
                    }

                    ChromeToggle {
                        text: i18n("Clear")
                        checkable: false
                        enabled: (root.effectsRevision, session.effectsHere.length > 0)
                        onClicked: {
                            session.clearEffects()
                            root.pickedStage = 0
                        }
                    }
                }

                /**
                 * The board: every plugin on the part, in signal order, and
                 * which of them the bench is standing on.
                 *
                 * The chain is the thing that is never folded away. What a
                 * part goes through on its way out is one line, and it is the
                 * question this panel exists to answer, so it is always drawn
                 * whole -- the instrument at one end, the ports at the other,
                 * and the plugins between them in the order the sound goes in.
                 *
                 * What is folded away is the knobs of the plugins nobody is
                 * turning. Drawing all of them at once made the band as tall
                 * as whichever plugin was largest and as wide as the whole
                 * chain, so a pedalboard of four cost half the window and
                 * scrolled in both directions -- and a card is not more
                 * legible for being one of six on a strip you cannot see the
                 * end of.
                 */
                Flickable {
                    id: board

                    Layout.fillWidth: true
                    Layout.preferredHeight: boardRow.implicitHeight + deckPanel.barRoom
                    clip: true
                    // Room for the bar at the bottom edge, so an overlay bar
                    // lies over ink rather than over the tiles it is there to
                    // help somebody reach.
                    contentWidth: boardRow.implicitWidth + deckPanel.barRoom
                    contentHeight: height
                    // Sideways only. The board is one row however long the
                    // chain is, and a row cannot overflow downwards.
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds

                    QQC2.ScrollBar.horizontal: QQC2.ScrollBar {
                        policy: QQC2.ScrollBar.AsNeeded
                    }

                    RowLayout {
                        id: boardRow

                        spacing: Kirigami.Units.largeSpacing

                        // Where the sound comes from: the part's own badge,
                        // and whether it is recordings or a programme.
                        ColumnLayout {
                            Layout.alignment: Qt.AlignVCenter
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
                            Layout.alignment: Qt.AlignVCenter
                            text: "→"
                            color: Ink.edge
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
                        }

                        Repeater {
                            model: session.chainHere

                            delegate: RowLayout {
                                id: boardLink

                                required property var modelData
                                required property int index

                                Layout.alignment: Qt.AlignVCenter
                                spacing: Kirigami.Units.largeSpacing

                                StageTile {
                                    current: boardLink.index === deckPanel.stage
                                    text: i18n("%1 · %2",
                                               boardLink.modelData.stage + 1,
                                               boardLink.modelData.name)
                                    // What it was set from if it was set from
                                    // anything, and otherwise how it is being
                                    // run. A mono plugin in a stereo chain is
                                    // instantiated twice, one per side, and
                                    // somebody reading the board should not
                                    // have to know that to understand what
                                    // they are looking at.
                                    caption: {
                                        root.effectsRevision
                                        const voicing = session.voicingOn(
                                            boardLink.modelData.stage)
                                        if (voicing.length > 0) {
                                            return voicing
                                        }
                                        return boardLink.modelData.stereo
                                            ? i18n("stereo")
                                            : i18n("mono, run twice")
                                    }
                                    onClicked: root.pickedStage = boardLink.index
                                }

                                /**
                                 * The cable between two pedals, which is also
                                 * the way to swap them.
                                 *
                                 * Reorder used to be a pair of arrows in every
                                 * card's header: two controls per plugin to
                                 * say one thing, and saying it from the wrong
                                 * end. Moving a stage one place is exactly
                                 * swapping it with its neighbour, and the
                                 * neighbour is what the gap between them is
                                 * made of -- so one handle per seam rather
                                 * than two per card, sitting where the sound
                                 * goes.
                                 *
                                 * The bench follows what it was standing on.
                                 * Swapping the stage under the knobs and
                                 * leaving the number where it was would change
                                 * which plugin is being edited without saying
                                 * so, which is the one thing a reorder must
                                 * not do.
                                 */
                                ChromeToggle {
                                    visible: (root.effectsRevision,
                                              boardLink.index
                                                  < session.chainHere.length - 1)
                                    text: "⇄"
                                    checkable: false
                                    implicitWidth: Ink.smallControl
                                    implicitHeight: Ink.smallControl
                                    onClicked: {
                                        if (root.pickedStage === boardLink.index) {
                                            root.pickedStage = boardLink.index + 1
                                        } else if (root.pickedStage
                                                       === boardLink.index + 1) {
                                            root.pickedStage = boardLink.index
                                        }
                                        session.moveEffect(boardLink.modelData.stage, 1)
                                    }

                                    QQC2.ToolTip.text: i18n("Swap these two")
                                    QQC2.ToolTip.visible: hovered
                                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                }
                            }
                        }

                        QQC2.Label {
                            Layout.alignment: Qt.AlignVCenter
                            text: "→"
                            color: Ink.edge
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
                        }

                        // Where it comes out. Named rather than drawn, because
                        // what happens after the chain is the fader and the
                        // pair of ports, and both of those live elsewhere.
                        //
                        // Two words when the ports are open and one when they
                        // are not, because they are two different endings: a
                        // part that leaves this program on a pair of ports of
                        // its own is the thing the program is for, and a part
                        // going into the mix with the others is not that. The
                        // sides are named beneath, since a pair is the whole
                        // difference and "stem out" alone does not say it.
                        ColumnLayout {
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 0

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: session.ports
                                    ? i18nc("the end of the chain, on ports of its own",
                                            "stem out")
                                    : i18nc("the end of the chain, into the mix", "mix")
                                color: Ink.faint
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.letterSpacing: 1
                            }

                            // The graph names them by the part rather than by
                            // number -- `01_Guitar_I_FL` -- so the sides are
                            // what can be said here without saying it wrongly.
                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                visible: session.ports
                                text: i18nc("the two sides of a stereo pair", "FL / FR")
                                color: Ink.quiet
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                        }
                    }
                }

                /**
                 * The bench: the knobs of the one stage the board is pointing
                 * at, across the whole window.
                 *
                 * Width is what pays for the arrangement. The same amplifier
                 * that needed a cassette stacked over two rows of knobs over
                 * three rows of switches -- because a card in a row of cards
                 * is about as wide as the mixer -- puts all three side by side
                 * here and comes out a third of the height. Nothing on it is
                 * smaller than it was; there is simply room.
                 */
                ColumnLayout {
                    id: benchBlock

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing
                    visible: deckPanel.benched !== null

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        color: Ink.line
                    }

                    RowLayout {
                        id: benchHead

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        QQC2.Label {
                            text: deckPanel.benched
                                ? i18n("%1 · %2",
                                       deckPanel.benched.stage + 1,
                                       deckPanel.benched.name)
                                : ""
                            color: Ink.paper
                            font.weight: Font.DemiBold
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: !deckPanel.benched
                                ? ""
                                : (deckPanel.benched.stereo
                                    ? i18n("stereo")
                                    : i18n("mono, run twice"))
                            color: Ink.quiet
                            elide: Text.ElideRight
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }

                        /**
                         * Somebody else's ears, as a starting point.
                         *
                         * A chain at its defaults is an amplifier nobody has
                         * turned up. These are the guitarix factory presets,
                         * carrying the part of each that an amplifier can hold
                         * -- the valve, the tone stack, the cabinet and the
                         * levels. What they cannot carry goes to the footer,
                         * because a voicing missing its reverb is not the
                         * sound on the label and should not pretend to be.
                         *
                         * Here rather than beside the cassette it puts on the
                         * bench, which is where it would sit if a cassette
                         * were always there to sit beside. A stage with no
                         * voicing on it yet has none -- and that is precisely
                         * the stage somebody is looking for this button on.
                         */
                        ChromeToggle {
                            visible: session.voicings.length > 0
                            text: i18n("Voicings…")
                            checkable: false
                            implicitHeight: Ink.smallControl
                            onClicked: voicingMenu.popup()

                            QQC2.Menu {
                                id: voicingMenu

                                Repeater {
                                    model: session.voicings

                                    delegate: QQC2.MenuItem {
                                        required property var modelData

                                        text: i18n("%1  —  %2",
                                                   modelData.name, modelData.summary)
                                        enabled: modelData.amplified
                                        onTriggered: {
                                            if (deckPanel.benched) {
                                                session.applyVoicing(
                                                    deckPanel.benched.stage,
                                                    modelData.name)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Taking this one off, from the panel that is this
                        // one, rather than taking the end off the chain until
                        // this one is the end.
                        ChromeToggle {
                            text: "✕"
                            checkable: false
                            implicitWidth: Ink.smallControl
                            implicitHeight: Ink.smallControl
                            onClicked: {
                                if (deckPanel.benched) {
                                    session.removeEffect(deckPanel.benched.stage)
                                }
                            }

                            QQC2.ToolTip.text: deckPanel.benched
                                ? i18n("Take %1 off", deckPanel.benched.name)
                                : ""
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        }
                    }

                    /**
                     * The front panel itself: as tall as the plugin on it, up
                     * to what the deck allows.
                     *
                     * Past that it scrolls rather than grows. Every plugin on
                     * this machine fits, and the one that does not is a plugin
                     * with thirty controls -- which should cost the score
                     * nothing, because the score is what the window is for.
                     */
                    Flickable {
                        id: benchBody

                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(deckPanel.benchCap,
                                                         benchRow.implicitHeight)
                        clip: true
                        contentWidth: width
                        contentHeight: benchRow.implicitHeight
                        flickableDirection: Flickable.VerticalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                            policy: QQC2.ScrollBar.AsNeeded
                        }

                        RowLayout {
                            id: benchRow

                            // Room at the right for an overlay bar, as the
                            // board leaves room at the bottom for one.
                            width: benchBody.width - deckPanel.barRoom
                            spacing: Kirigami.Units.largeSpacing * 2

                            /**
                             * The picture, which is the stage itself rather
                             * than one of its controls.
                             *
                             * Only ever one of the two: a bench carrying both
                             * a cassette and a cabinet would be two pictures
                             * of one stage.
                             */
                            ColumnLayout {
                                Layout.alignment: Qt.AlignTop
                                spacing: Kirigami.Units.smallSpacing
                                visible: deckPanel.voicingOnBench.length > 0
                                    || deckPanel.cabinetOnBench

                                VoicingTape {
                                    Layout.alignment: Qt.AlignHCenter
                                    // Smaller than the drawing's own figure.
                                    // A cassette is a label and the label is
                                    // one line of text; the extra two gridUnit
                                    // were buying nothing, and were part of
                                    // why the band had to be as tall as it was.
                                    implicitWidth: Kirigami.Units.gridUnit * 11
                                    visible: deckPanel.voicingOnBench.length > 0
                                    name: deckPanel.voicingOnBench
                                }

                                SpeakerCabinet {
                                    Layout.alignment: Qt.AlignHCenter
                                    visible: deckPanel.voicingOnBench.length === 0
                                        && deckPanel.cabinetOnBench
                                }
                            }

                            /**
                             * The knobs, in a block rather than a column.
                             *
                             * Wrapped into rows of five, which is about the
                             * width of an amplifier: a plugin with a dozen
                             * controls in one line would push the switches off
                             * the window, and one with three would leave a
                             * hole. Five rather than four because guitarix's
                             * amplifier has nine, and nine over four is three
                             * rows -- one taller than the bench is.
                             */
                            GridLayout {
                                Layout.alignment: Qt.AlignTop
                                columns: 5
                                columnSpacing: Kirigami.Units.smallSpacing
                                rowSpacing: 0

                                Repeater {
                                    model: deckPanel.benched
                                        ? deckPanel.benched.controls.filter(
                                            control => !control.toggled
                                                && control.choices.length === 0)
                                        : []

                                    delegate: ColumnLayout {
                                        id: knobCell

                                        required property var modelData

                                        /**
                                         * Every cell the same width, so the
                                         * rows are columns.
                                         *
                                         * A cell sized to its own label put
                                         * BASS under the middle of MASTERGAIN
                                         * and the row beneath a row of five
                                         * drifting left of it, which reads as
                                         * knobs scattered on a panel rather
                                         * than as a panel. The width is the
                                         * widest label worth keeping, because
                                         * it is the labels and never the knobs
                                         * that decide it.
                                         */
                                        Layout.preferredWidth:
                                            Kirigami.Units.gridUnit * 4.5
                                        spacing: 0

                                        InkKnob {
                                            Layout.alignment: Qt.AlignHCenter
                                            from: knobCell.modelData.minimum
                                            to: knobCell.modelData.maximum
                                            stepSize: knobCell.modelData.integer ? 1 : 0
                                            value: knobCell.modelData.value
                                            onMoved: {
                                                if (deckPanel.benched) {
                                                    session.setEffectControl(
                                                        deckPanel.benched.stage,
                                                        knobCell.modelData.index, value)
                                                }
                                            }

                                            QQC2.ToolTip.text: i18n(
                                                "%1 — %2 of %3 to %4",
                                                knobCell.modelData.name,
                                                root.reading(knobCell.modelData),
                                                knobCell.modelData.minimum,
                                                knobCell.modelData.maximum)
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                        }

                                        // Elided rather than wrapped:
                                        // "DISTORTION" is one word and a knob
                                        // two labels tall would set every
                                        // other knob on the panel lower to
                                        // match. The full name is on the
                                        // tooltip.
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignHCenter
                                            text: knobCell.modelData.name.toUpperCase()
                                            color: Ink.faint
                                            elide: Text.ElideRight
                                            font.pointSize:
                                                Kirigami.Theme.smallFont.pointSize
                                        }

                                        /**
                                         * Where the knob is, in figures.
                                         *
                                         * A pointer says roughly, and roughly
                                         * is what a hand wants while it is
                                         * turning. A number is what the same
                                         * person wants afterwards, to see
                                         * whether two stages are set the same
                                         * -- and it should not cost hovering
                                         * over each one in turn to find out.
                                         */
                                        QQC2.Label {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: root.reading(knobCell.modelData)
                                            color: Ink.quiet
                                            font.pointSize:
                                                Kirigami.Theme.smallFont.pointSize
                                            font.features: ({ "tnum": 1 })
                                        }
                                    }
                                }
                            }

                            /**
                             * The switches and the named choices, which are
                             * not knobs and should not be drawn as one: a
                             * valve model is a list of names, and a slider
                             * from nought to eleven labelled nothing is a
                             * worse way to ask which one.
                             *
                             * One under the other now, in a column of their
                             * own beside the knobs. Two to a line was what a
                             * card as wide as the mixer could hold; a bench as
                             * wide as the window can hold the lot in one
                             * column, which is the shape a list of settings
                             * wants to be read in.
                             */
                            ColumnLayout {
                                Layout.alignment: Qt.AlignTop
                                Layout.fillWidth: true
                                // Capped, or a wide window turns a switch into
                                // a button the width of a hand.
                                Layout.maximumWidth: Kirigami.Units.gridUnit * 22
                                spacing: Kirigami.Units.smallSpacing

                                Repeater {
                                    model: deckPanel.benched
                                        ? deckPanel.benched.controls.filter(
                                            control => control.toggled
                                                || control.choices.length > 0)
                                        : []

                                    delegate: RowLayout {
                                        id: pickRow

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        // "Tonestack Model" is what the plugin
                                        // calls it and it did not fit, so the
                                        // panel showed "Tonestack ..." and the
                                        // reader learned nothing the word
                                        // "Tonestack" had not already told
                                        // them. Wide enough for the longest
                                        // name guitarix's amplifier uses,
                                        // which is the one that was being cut.
                                        QQC2.Label {
                                            Layout.preferredWidth:
                                                Kirigami.Units.gridUnit * 5.6
                                            text: pickRow.modelData.name
                                            color: Ink.faint
                                            elide: Text.ElideRight
                                            font.pointSize:
                                                Kirigami.Theme.smallFont.pointSize
                                        }

                                        // The state it is in, not the state it
                                        // goes to. "BYPASS · On" was the same
                                        // three characters whichever way the
                                        // switch was set, lit or unlit, and on
                                        // a control called BYPASS the reader
                                        // has to work out both what the button
                                        // means and what a bypass that is on
                                        // does to the sound. Saying "Off" when
                                        // it is off answers the first
                                        // question, and the second was never
                                        // this window's to invent.
                                        ChromeToggle {
                                            visible: pickRow.modelData.toggled
                                            text: checked ? i18n("On") : i18n("Off")
                                            implicitHeight: Ink.smallControl
                                            checked: pickRow.modelData.value > 0.5
                                            onToggled: {
                                                if (deckPanel.benched) {
                                                    session.setEffectControl(
                                                        deckPanel.benched.stage,
                                                        pickRow.modelData.index,
                                                        checked ? 1 : 0)
                                                }
                                            }
                                        }

                                        ChromeToggle {
                                            id: choiceButton

                                            Layout.fillWidth: true
                                            visible: !pickRow.modelData.toggled
                                            checkable: false
                                            implicitHeight: Ink.smallControl
                                            /**
                                             * The name of the choice the value
                                             * stands for -- found by the
                                             * value, never by its place in the
                                             * list. A plugin may number its
                                             * choices 0, 2, 5, and lilv
                                             * reports them in no particular
                                             * order, so position is not an
                                             * answer to "which one is this".
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
                                                        onTriggered: {
                                                            if (deckPanel.benched) {
                                                                session.setEffectControl(
                                                                    deckPanel.benched.stage,
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
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                // An empty deck says what to do with it rather than nothing.
                QQC2.Label {
                    id: emptyDeck

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    visible: deckPanel.benched === null
                    text: session.availableEffects.length > 0
                        ? i18n("Nothing on this part yet \u2014 the amplifier goes on here.")
                        : i18n("No LV2 effects were found on this machine.")
                    color: Ink.quiet
                    horizontalAlignment: Text.AlignHCenter
                }

                /**
                 * What a voicing carried, and what it left behind.
                 *
                 * The honesty this program is built on, in the one place it is
                 * easiest to be dishonest: a preset named after a record, most
                 * of which arrived, is not that sound and should not be
                 * presented as though it were. The status bar says this once
                 * when a voicing is applied and is then wanted for something
                 * else; the deck keeps saying it for as long as the tape is on
                 * the card.
                 */
                RowLayout {
                    id: deckNotes

                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.largeSpacing
                    visible: deckFooter.carried.length > 0

                    QQC2.Label {
                        id: deckFooter

                        /** Every stage that came off a tape, named. */
                        readonly property var carried: {
                            root.effectsRevision
                            const named = []
                            const chain = session.chainHere
                            for (let index = 0; index < chain.length; ++index) {
                                const voicing = session.voicingOn(chain[index].stage)
                                if (voicing.length > 0) {
                                    named.push(voicing)
                                }
                            }
                            return named
                        }

                        /** And everything those voicings could not reproduce. */
                        readonly property var declined: {
                            root.effectsRevision
                            const missing = []
                            const chain = session.chainHere
                            for (let index = 0; index < chain.length; ++index) {
                                for (const part of
                                         session.voicingDeclinedOn(chain[index].stage)) {
                                    if (!missing.includes(part)) {
                                        missing.push(part)
                                    }
                                }
                            }
                            return missing
                        }

                        text: i18np("Voicing \u201c%2\u201d carried this chain.",
                                    "Voicings %2 carried this chain.",
                                    deckFooter.carried.length,
                                    deckFooter.carried.join(i18nc(
                                        "between two voicing names", "\u201d and \u201c")))
                        color: Ink.accentOnInk
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: deckFooter.declined.length > 0
                        // Printed as they arrive. `Gx::Fitting` writes whole
                        // sentences -- "Not carried: bassEnhancer, eq." -- so
                        // a heading in front of them would say it twice.
                        text: deckFooter.declined.join(" ")
                        color: Ink.quiet
                        // Wrapped rather than elided, and capped at two lines.
                        // With the status bar no longer repeating it this is
                        // the only copy, and a list of what a voicing could
                        // not reproduce that stops at "stereoverb, graphi..."
                        // is the half of the sentence that does not matter.
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
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
                // Far enough apart to read as a scale rather than as a row of
                // numbers. Two and three quarters of a grid unit only keeps
                // them from touching, which is a different and lower bar --
                // the design asks for ticks and whitespace, and at its own
                // density of a bar every forty-six pixels that means every
                // fourth bar carries a number and the rest carry a tick.
                const wanted = Kirigami.Units.gridUnit * 7
                const steps = [1, 2, 4, 8, 16, 32, 64]
                for (let index = 0; index < steps.length; ++index) {
                    if (steps[index] * step >= wanted) {
                        return steps[index]
                    }
                }
                return 64
            }

            /**
             * Room for the section names, and none at all where there are
             * none.
             *
             * A score nobody has named a section in is a ruler rather than a
             * map, and a band of empty space over it is the room a map would
             * have wanted. Asked of the whole score rather than bar by bar, so
             * that the row cannot appear halfway along when the first named
             * bar scrolls into view.
             */
            readonly property int nameRow:
                session.hasSections ? Kirigami.Units.gridUnit * 1.1 : 0
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
                                // One colour for all of them. A section start
                                // is already twice the height of its
                                // neighbours, and saying it a second time in
                                // ink would be the ruler insisting.
                                color: Ink.faint
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
                        // Riding the line, not floating over it: the last
                        // fifth of the lozenge is below it, so the ruler runs
                        // through the playhead rather than stopping short of
                        // it and leaving the mark adrift in the space above.
                        y: rulerBody.lineY - height * 0.8
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

            // Which sheet of how many, and how big it is being drawn. At this
            // end of the status bar because that is where every program that
            // shows somebody a document has put it for thirty years.
            QQC2.Label {
                text: i18np("Page %2 of %1", "Page %2 of %1", view.pageCount,
                            view.currentPage + 1)
                color: Ink.faint
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                visible: session.hasScore && view.pageCount > 1
            }

            Row {
                spacing: Kirigami.Units.smallSpacing
                visible: session.hasScore

                component ZoomStep: QQC2.Label {
                    color: area.containsMouse ? Ink.paper : Ink.faint
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.weight: Font.DemiBold
                    width: Kirigami.Units.gridUnit
                    horizontalAlignment: Text.AlignHCenter
                    property alias hovered: area.containsMouse
                    signal pressed()
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: parent.pressed()
                    }
                }

                ZoomStep {
                    text: "−"
                    onPressed: {
                        view.zoom = view.zoom / 1.25
                        viewState.zoom = view.zoom
                    }
                }

                // The number is the control that puts it back: a reader who
                // has zoomed somewhere odd wants one press to get to a page
                // that fits, and the thing they are looking at is the number.
                QQC2.Label {
                    text: Math.round(view.zoom * 100) + "%"
                    color: fitArea.containsMouse ? Ink.paper : Ink.faint
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    width: Kirigami.Units.gridUnit * 2.5
                    horizontalAlignment: Text.AlignHCenter
                    QQC2.ToolTip.visible: fitArea.containsMouse
                    QQC2.ToolTip.text: i18n("Fit the page to the window")
                    MouseArea {
                        id: fitArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            view.zoom = view.zoomToFit()
                            viewState.zoom = view.zoom
                        }
                    }
                }

                ZoomStep {
                    text: "+"
                    onPressed: {
                        view.zoom = view.zoom * 1.25
                        viewState.zoom = view.zoom
                    }
                }
            }
        }
    }
    }
}
