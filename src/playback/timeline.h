// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "noises.h"
#include "score.h"

/**
 * The score as played, rather than as written.
 *
 * Two ideas keep this a separate step from both the document and the engine.
 *
 * The first is that repeats, alternate endings and da capos are a property of
 * the notation, not of the music: they are flattened here, once, into a list of
 * bars in the order they are heard. Nothing downstream ever reasons about a
 * jump, and in particular the audio thread never does.
 *
 * The second is that a channel is per string. MIDI pitch bend applies to a
 * whole channel, so a guitar whose six strings share one channel cannot bend a
 * note while another rings -- every held note bends with it. Guitar Pro solves
 * this the same way and its file format says so out loud, in a track flag
 * called UseOneChannelPerString. Channels here are numbered inside the track's
 * own synth, because the design gives each track a synth of its own; sixteen
 * channels is a limit of a MIDI cable, and this is not one.
 */
namespace Timeline
{
/**
 * A point on a bend curve: how far into the note, and how far from the written
 * pitch in cents. 100 is a semitone, 200 a whole tone -- which is what gpif
 * means by its hundredths of a semitone, at the same scale.
 */
struct BendPoint {
    Rational at;        //< quarters from the start of the note
    int cents = 0;
};

/** One sounding note, in quarters from the start of the performance. */
struct NoteEvent {
    Rational start;
    Rational end;
    int pitch = 0;
    int velocity = 0;
    int channel = 0;    //< inside this track's own synth, not a global MIDI bus
    int string = -1;
    int fret = 0;

    /**
     * A dead note: struck with the fretting hand resting on the string.
     *
     * Carried here as well as expressed as a short quiet note, because a
     * library with a recording of one plays that instead, and a synthesiser
     * with no such recording goes on doing what it did.
     */
    bool muted = false;

    /**
     * Empty for the great majority of notes. Where it is not, the first point
     * is where the bend starts and the last is where it ends, and whatever
     * plays this is expected to move continuously between them.
     */
    QList<BendPoint> bend;

    /**
     * Struck by the fretting hand rather than picked -- a hammer-on, a pull-off
     * or a legato slide. A sampler with legato articulations would play a
     * different sample; until P4 there is one, and it is quieter.
     */
    bool legato = false;

    /**
     * What the page says about how it is played, carried through unchanged.
     *
     * Not used to decide anything here -- palm muting and letting ring are
     * drawn and they are printed, and what a synthesiser does about them is
     * its own business. They are on the event because anything written *out*
     * of this program needs the timing and the technique together, and this is
     * the only place both are already true at once.
     */
    bool palmMuted = false;
    bool letRing = false;
    bool accent = false;

    /**
     * Whether the hand is shaking the note.
     *
     * Carried as well as played, because the bend curve it becomes is a
     * decision about how to sound it and not a record of what was written --
     * a pack that read the curve back would say a note bent thirty cents
     * sharp and thirty flat, which is a description of a wobble and not the
     * word for one.
     */
    bool vibrato = false;

    /** Whether this strike is one of a tremolo-picked run. */
    bool tremolo = false;
};

struct TempoEvent {
    Rational at;
    double quarterBpm = 120;
};

/** What a synth is actually told, once a note has become a performance. */
enum class MessageKind {
    NoteOn,
    NoteOff,
    PitchBend,      //< data1 carries the whole 14-bit value, 8192 being centre
    BendRange,      //< data1 is semitones; whatever plays this sets it up its own way
};

/**
 * One instruction to one channel of one track's synth.
 *
 * The same list drives the MIDI writer and the audio renderer, which is the
 * point: two implementations of "when does the bend move" would disagree
 * eventually, and the one nobody listened to would be the wrong one.
 */
struct Message {
    Rational at;
    MessageKind kind = MessageKind::NoteOn;
    int channel = 0;    //< inside this track's own synth
    int data1 = 0;      //< pitch, or bend value, or semitones
    int data2 = 0;      //< velocity
};

/**
 * Turns a position in quarters into a position in seconds.
 *
 * Built once from the tempo map and asked many times, because a renderer needs
 * this for every note and the tempo map is a list to be walked.
 */
class Clock
{
public:
    Clock(const Score &score, const QList<int> &order);

    double secondsAt(const Rational &quarters) const;
    double totalSeconds() const;

private:
    QList<TempoEvent> m_tempos;
    QList<double> m_secondsAtTempo;     //< when each tempo section begins
    Rational m_length;
};

/**
 * The master bar indices in the order they are heard.
 *
 * Simple start/end repeats are expanded. Alternate endings are not yet, and a
 * score using them is reported rather than quietly played wrongly -- see
 * hasAlternateEndings().
 */
QList<int> playedOrder(const Score &score, bool expandRepeats = true);

/** Whether flattening this score is currently approximate. */
bool hasAlternateEndings(const Score &score);

/** Every note of one track, already tied together and sorted by start. */
QList<NoteEvent> notesFor(const Score &score, int trackIndex, const QList<int> &order);

/**
 * The same notes as instructions, with their bends drawn as curves.
 *
 * Sorted by time, and by kind where two land together: a channel is told its
 * bend range before it is asked to play, and returned to centre after a bent
 * note rather than during the next one.
 *
 * `noises` is what the instrument playing this can make besides notes, and is
 * empty for everything that is not a sample library with the recordings in it.
 * Where it is not empty, this is the layer that decides *when* -- a position
 * shift, a dead note, a part coming to a stop -- because the score is the only
 * thing that knows, and a sampler that worked it out for itself would be a
 * sampler that knew what a guitar was. The result is a note number like any
 * other by the time it leaves here.
 *
 * Deliberately not given to the MIDI writer, which calls this without one: a
 * `.mid` carrying note 90 on a guitar channel is a file that plays a squeak as
 * an F#6 in every other program that opens it.
 */
QList<Message> messagesFor(const Score &score, int trackIndex, const QList<int> &order,
                           const Noises::Map &noises = {});

/**
 * The noises a performance asks for, as notes of their own.
 *
 * Separated from `messagesFor` so that a test can say which noise landed where
 * without reading it out of a stream of bend messages, and so that the rules
 * -- how far a hand has to move to be heard moving, how much silence is a stop
 * rather than a rest -- are in one place to be argued with.
 */
QList<NoteEvent> noisesFor(const Score &score, int trackIndex, const QList<int> &order,
                           const Noises::Map &noises);

/**
 * A click on every beat of the performance, which is a part no file contains.
 *
 * The metronome is built as a track rather than as a fixture, because that is
 * what it is: a list of messages and an instrument to play them on, handed to
 * the same `TrackSynth` as everything else. Nothing in the engine had to learn
 * a new idea to get one, which is the useful thing this proves about the
 * design rather than about the feature.
 *
 * The beat is the one a musician counts, which is not always the one the
 * denominator names. 6/8 is two beats of three quavers and not six of one, so
 * the rule is compound time where the numerator is a multiple of three and
 * larger than it -- 6/8, 9/8 and 12/8 -- and simple time everywhere else,
 * 3/8 included, which is counted in quavers by everybody who plays it.
 */
QList<Message> clickFor(const Score &score, const QList<int> &order);

/** How long one beat of `bar` lasts, in quarters. */
Rational beatOf(const MasterBar &bar);

/**
 * The part the click is played by: percussion, and not in the score.
 *
 * Percussion because a wood block is one, and because a percussion channel
 * cannot be given the wrong programme by a track that happens to share a
 * number with it.
 */
Track clickTrack();

/**
 * The tempo in force at a notated bar, in crotchets a minute.
 *
 * The last change written at or before it, which is what a reader looking at
 * that bar would take the tempo to be. Notated rather than played, because
 * the page and the editor both ask about a bar as it is written down.
 */
double tempoAtBar(const Score &score, int bar);

/** The tempo changes, placed on the played timeline rather than the notated one. */
QList<TempoEvent> tempoMap(const Score &score, const QList<int> &order);

/** How long the performance is, in quarters. */
Rational length(const Score &score, const QList<int> &order);

/** How long the performance is, in seconds, tempo changes included. */
double seconds(const Score &score, const QList<int> &order);

/**
 * Which bar is sounding at `seconds`, as an index into `order`.
 *
 * Two answers are wanted here and they are different: `order[result]` is the
 * bar as the score writes it, which is what a reader wants highlighted, while
 * `result` itself is the pass through it, which is what a playhead is really
 * on. Returns -1 before the beginning and after the end.
 */
int barAt(const Score &score, const QList<int> &order, const Clock &clock, double seconds);

/**
 * When a pass through the played order begins, in quarters.
 *
 * Indexed by pass rather than by bar, because a bar inside a repeat happens
 * more than once and "when does bar 12 start" has as many answers as there are
 * times through it.
 */
Rational quartersAtPass(const Score &score, const QList<int> &order, int pass);

/**
 * The same moment in seconds, which is what a transport is measured in.
 *
 * The other direction of `barAt`. Built on `quartersAtPass` rather than
 * walking the bars a second time: two ways of adding up where a bar begins
 * would eventually disagree, and the one nobody was watching would be wrong.
 */
double secondsAtPass(const Score &score, const QList<int> &order, const Clock &clock,
                     int pass);
}
