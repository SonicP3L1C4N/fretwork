// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

pragma Singleton

import QtQuick

/**
 * The colours of the window, which are the icon's.
 *
 * Near-black chrome, a page of paper inside it, and one magenta taken from the
 * fret marker on the app icon. Fixed rather than taken from the desktop:
 * Fretwork draws a document, and a document does not change colour because the
 * desktop did -- a score on a dark background is not a dark score, it is an
 * unreadable one. The chrome is dark so that the paper is the brightest thing
 * in the window, which is where the eye should be.
 *
 * One place for them so that the page, the mixer and the toolbar cannot drift
 * apart. `Tab::Palette` holds the same values for the drawing, which is the
 * one duplication here: C++ paints the score and QML paints everything round
 * it, and neither can read the other's constants.
 */
QtObject {
    readonly property color ink: "#201e1d"          //< chrome, and fret numbers
    readonly property color paper: "#f3f2f2"        //< the page, and text on ink

    readonly property color accent: "#d6006c"       //< the fret marker
    readonly property color accentHover: "#d82071"
    readonly property color accentDeep: "#aa0b56"   //< the accent as text on paper
    readonly property color accentOnInk: "#ff90b1"  //< and as text on ink
    readonly property color accentTint: "#ffdee6"   //< behind the bar being played

    readonly property color panel: "#f8f4f4"        //< the mixer
    readonly property color panelDeep: "#eae7e7"    //< panels below the score
    readonly property color rule: "#d7d3d3"         //< a slider with nothing in it
    readonly property color staff: "#bab6b6"
    readonly property color faint: "#9b9797"        //< bar numbers, and quiet text
    readonly property color quiet: "#7d7979"
    readonly property color edge: "#605d5d"         //< borders on ink
    readonly property color line: "#444141"
    readonly property color well: "#2d2b2b"         //< a field sunk into ink

    // The tuner, and only the tuner.
    //
    // Everywhere else in this window one accent is enough, because everywhere
    // else the question is "which of these is the one" and an accent answers
    // it. A tuner is asked a different question. "Twelve cents" says how far
    // and not which way, and a hand already on a machine head needs which way
    // before it needs how far -- so direction is a colour here, cold for flat
    // and the window's own accent for sharp, each brightening as the string
    // comes in.
    //
    // Arriving is a third colour rather than the brightest step of the other
    // two, because arriving is not "very slightly flat": it is the thing being
    // aimed at, and it should not look like the last of a series it is the end
    // of. Amber against ink, and the only warm colour in the program.
    readonly property color flat: "#2b62c4"         //< as flat as this reads
    readonly property color flatNear: "#4f83d9"
    readonly property color flatNearer: "#7ea6e8"
    readonly property color flatNearest: "#b1c9f2"  //< the step before the mark
    readonly property color arrived: "#f0a400"      //< within three cents, and held

    // The sharp side is the accent the window already owns, so its ends are
    // `accent` and `accentOnInk` and only the two steps between them are new.
    // Written out rather than computed: Qt.lighter walks a hue as well as a
    // brightness, and a ramp that drifts orange on the way up would meet the
    // amber coming the other way.
    readonly property color sharpNear: "#e63d84"
    readonly property color sharpNearer: "#f56a9c"

    // Equipment, which is drawn rather than styled.
    //
    // The effects deck draws two things that are objects instead of controls:
    // a cassette carrying the voicing a stage was set from, and a speaker
    // cabinet. Three greys the chrome had no use for -- the moulded shell of a
    // thing, the dark of a hole in it, and the brown of a tape spool, which is
    // the only place in the program that is neither ink, paper nor accent.
    // They belong to the drawings and to nothing else.
    readonly property color shell: "#33302f"        //< the case of a thing
    readonly property color recess: "#1a1918"       //< a screw head, a speaker
    readonly property color spool: "#3a2a22"        //< tape, wound on a reel

    // The design is built on a small number of sizes; naming them is what
    // stops the fourth button from being two pixels wider than the other three.
    readonly property int radius: 2
    readonly property int control: 34               //< a toolbar button, square
    readonly property int smallControl: 26          //< S and M in the mixer
    readonly property int groove: 4                 //< every slider
    readonly property int grip: 14                  //< every slider's handle
    readonly property int mixerWidth: 290
    readonly property int tracksWidth: 240         //< the list of parts, opposite it

    /**
     * The typeface the chrome is set in, and the score is not.
     *
     * Named here rather than asked for at each label because it is applied
     * once, to the application font, in `main.cpp` -- this is what the window
     * uses on the few occasions it has to say the name out loud, and the one
     * place to change if it is ever swapped.
     */
    readonly property string chromeFamily: "Source Serif 4"
}
