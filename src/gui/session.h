// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "editor.h"
#include "gxpreset.h"
#include "rigfile.h"
#include "lv2chain.h"
#include "sfz.h"
#include "player.h"
#include "score.h"
#include "tablayout.h"
#include "timeline.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

#include <memory>

/**
 * One open score, and everything a window needs to ask about it.
 *
 * A facade rather than a model: the score, the layout, the played order and
 * the player already exist and are already tested, and this exposes them to
 * QML without any of them learning that QML is there. Nothing in `src/model`,
 * `src/playback` or `src/audio` includes a Qt Quick header, which is what
 * keeps them testable without a window.
 *
 * The playhead is **polled**, not pushed. `Player` deliberately emits nothing
 * from its audio thread, so a timer here reads the position a few times a
 * second and turns it into the signals QML wants. That is the correct
 * direction for this to flow: the audio thread must not know that a user
 * interface exists.
 */
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString title READ title NOTIFY scoreChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY scoreChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY scoreChanged)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY scoreChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    /**
     * What is wrong, if anything is: empty when the score can be played.
     *
     * Separate from `status` because the two have different lifetimes. A
     * status is chatter -- "Capo off", "Tempo 120 from bar 3" -- and the next
     * one replaces it. A problem is a condition: there is no SoundFont, or the
     * audio device would not open, and it is still true a minute later. It is
     * set when the player cannot be built and cleared when one is, so the
     * window can keep saying so for as long as it holds.
     *
     * The status bar is a panel a user can close, and it is remembered closed.
     * That is fine for chatter and wrong for this: a first run with no
     * SoundFont installed is silent, and a silent program that has also been
     * told not to show its status bar has no way left to say why. The window
     * shows the bar regardless while this is set.
     */
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

    /**
     * The tempo in force where the caret is, in crotchets a minute.
     *
     * What the bar would be played at, whether the bar writes a change of its
     * own or inherits one from an earlier bar. `tempoWrittenHere` is how a
     * window tells those two apart, which is worth showing: a number that
     * belongs to this bar and one it is merely living under look the same and
     * behave differently when they are edited.
     */
    Q_PROPERTY(double tempoHere READ tempoHere NOTIFY cursorMoved)
    Q_PROPERTY(bool tempoWrittenHere READ tempoWrittenHere NOTIFY cursorMoved)

    /**
     * What a musician would call that speed: "Andante", "Allegro".
     *
     * Beside the number rather than instead of it, because the two say
     * different things. 150 is a setting on a metronome; Allegro is what the
     * piece is, and it is the half a player recognises before reading the
     * digits. The boundaries are the conventional ones and are approximate in
     * every book that prints them, which is why this is a word and never a
     * thing to set the tempo from.
     */
    Q_PROPERTY(QString tempoTermHere READ tempoTermHere NOTIFY cursorMoved)

    /**
     * The current track's instrument: how it is tuned and where its capo is.
     *
     * The tuning reads as names rather than numbers -- "E2 A2 D3 G3 B3 E4" --
     * because that is how a guitarist says one, and it is written back the
     * same way.
     */
    Q_PROPERTY(QString tuningHere READ tuningHere NOTIFY cursorMoved)
    Q_PROPERTY(int capoHere READ capoHere NOTIFY cursorMoved)

    /** How many strings the current track has; none for a drum kit. */
    Q_PROPERTY(int stringsHere READ stringsHere NOTIFY cursorMoved)

    /** What the current part is called and what it is, for the editor of both. */
    Q_PROPERTY(QString trackNameHere READ trackNameHere NOTIFY cursorMoved)
    Q_PROPERTY(QString instrumentHere READ instrumentHere NOTIFY cursorMoved)

    /**
     * The sample library the current part is played from, and what there is.
     *
     * Kept for the session and not written into the score. Which recordings a
     * part is played through is a property of this machine -- where the files
     * are, which of them are installed -- and a `.fw` that named a path on
     * somebody's disk would be a `.fw` that opened wrong everywhere else.
     */
    Q_PROPERTY(QString samplerHere READ samplerHere NOTIFY samplersChanged)
    /** Every programme found, as {collection, name, path}, in collection order. */
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY samplersChanged)

    /** The collections they came in, which is what the menu is grouped by. */
    Q_PROPERTY(QStringList collections READ collections NOTIFY samplersChanged)

    /**
     * The effects on the current part, and everything installed to choose from.
     *
     * Kept for the session and not written into the score, the same as the
     * sample library and for the same reason: which plugins exist is a fact
     * about this machine.
     */
    Q_PROPERTY(QStringList effectsHere READ effectsHere NOTIFY effectsChanged)

    /**
     * The chain on the current part, with every knob on every plugin.
     *
     * Each entry is {name, index, controls}, and each control is what the
     * plugin says about itself: what it is called, what it may be, whether it
     * is a switch or a list, and where it is set now.
     */
    Q_PROPERTY(QVariantList chainHere READ chainHere NOTIFY effectsChanged)
    Q_PROPERTY(QVariantList availableEffects READ availableEffects NOTIFY effectsChanged)

    /**
     * The amplifier voicings guitarix ships, for a menu on each plugin.
     *
     * Constant because banks are files on the machine and this window does not
     * write them: what is installed when it opens is what it offers.
     */
    Q_PROPERTY(QVariantList voicings READ voicings CONSTANT)

    /** Everything a new part may be: names to show, and ids to ask for. */
    Q_PROPERTY(QStringList instrumentNames READ instrumentNames CONSTANT)
    Q_PROPERTY(QStringList instrumentIds READ instrumentIds CONSTANT)

    /** What the caret's bar is called, where the score calls it anything. */
    Q_PROPERTY(QString sectionHere READ sectionHere NOTIFY cursorMoved)

    /** The caret's bar's time signature, as "4/4", and whether it starts there. */
    Q_PROPERTY(QString timeHere READ timeHere NOTIFY cursorMoved)
    Q_PROPERTY(bool timeWrittenHere READ timeWrittenHere NOTIFY cursorMoved)

    /** Where the caret is, for the status bar: "Bar 4 · string 3 · quaver". */
    Q_PROPERTY(QString caretText READ caretText NOTIFY cursorMoved)

    /**
     * How many beats the caret's bar is counted in.
     *
     * The beat a musician counts, which is not always the one the denominator
     * names: 6/8 is two beats of three quavers and not six of one. The same
     * rule the metronome plays by, asked here so that a window can draw what
     * it is about to hear.
     */
    Q_PROPERTY(int beatsHere READ beatsHere NOTIFY cursorMoved)

    /**
     * Which of those beats is sounding, counted from nought; -1 when none is.
     *
     * The click made visible. A count-in is on the wishlist and this is not
     * one -- it says where in the bar the music has got to, which is the thing
     * a player glancing up from the neck wants and cannot get from a number of
     * seconds.
     */
    Q_PROPERTY(int beatNow READ beatNow NOTIFY positionChanged)

    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY scoreChanged)

    /** One icon name per track, by what the instrument is. */
    Q_PROPERTY(QStringList trackIcons READ trackIcons NOTIFY scoreChanged)

    /** The badge of the part on the page, in the tone the dark panels want. */
    Q_PROPERTY(QString trackIconHereOnInk READ trackIconHereOnInk NOTIFY currentTrackChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY scoreChanged)
    Q_PROPERTY(int currentTrack READ currentTrack WRITE setCurrentTrack NOTIFY currentTrackChanged)

    /**
     * A metronome on every beat, which is not a track and not in the file.
     *
     * Kept on across a rebuild of the player: somebody practising a bar has
     * asked for a click, not for a click until the next time they edit a note.
     */
    Q_PROPERTY(bool click READ isClickOn WRITE setClickOn NOTIFY clickChanged)
    Q_PROPERTY(double clickGain READ clickGain WRITE setClickGain NOTIFY clickChanged)

    /**
     * Every part as a pair of ports in the audio graph, for a DAW to record.
     *
     * Rebuilds the player, because it is a different way out rather than a
     * setting on the one there is -- and stops the transport first, since a
     * piece that carried on playing through having its output replaced would
     * be playing through a gap.
     */
    Q_PROPERTY(bool ports READ isPortsOn WRITE setPortsOn NOTIFY portsChanged)

    /** How many pairs are in the graph; zero when the ports are off. */
    Q_PROPERTY(int portCount READ portCount NOTIFY portsChanged)

    /**
     * Take the transport from the graph, so a DAW starts and locates it.
     *
     * Turning it on opens the ports, because it is the graph's transport that
     * is being followed and only the ported output can see one.
     */
    Q_PROPERTY(bool following READ isFollowing WRITE setFollowing NOTIFY portsChanged)

    /**
     * Whether the ports reached the speakers, and whether the graph is rolling.
     *
     * Both exist because both went wrong in use. Ports that nothing links are
     * a piece playing silently; following with nothing to follow is a piece
     * not playing at all. Neither says anything about itself unless asked, so
     * the window asks.
     */
    Q_PROPERTY(int portLinks READ portLinks NOTIFY playingChanged)
    Q_PROPERTY(bool graphRolling READ isGraphRolling NOTIFY playingChanged)

    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY scoreChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double length READ length NOTIFY scoreChanged)
    Q_PROPERTY(int currentBar READ currentBar NOTIFY positionChanged)

    /** How many bars the score has, and which one the caret is in. */
    Q_PROPERTY(int barCount READ barCount NOTIFY scoreChanged)

    /**
     * Whether the score names any of its sections at all.
     *
     * Asked once so that the bar strip can decide whether to keep a line free
     * for section names in every cell of its grid. Most scores name a few and
     * some name none, and reserving the room unconditionally would cost a row
     * of pixels per row of bars to hold nothing.
     */
    Q_PROPERTY(bool hasSections READ hasSections NOTIFY scoreChanged)
    Q_PROPERTY(int caretBar READ caretBar NOTIFY cursorMoved)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY historyChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY scoreChanged)
    Q_PROPERTY(bool savesInPlace READ savesInPlace NOTIFY scoreChanged)

public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    Q_INVOKABLE bool open(const QString &path);

    /** Starts again with one guitar and one empty bar. */
    Q_INVOKABLE void newScore();

    // ---- parts ----

    Q_INVOKABLE void addTrack(const QString &instrumentId);
    Q_INVOKABLE void removeTrack(int track);
    Q_INVOKABLE void renameTrack(int track, const QString &name);
    Q_INVOKABLE void setTrackInstrument(int track, const QString &instrumentId);

    /** Moves a part up or down the list by `by` places, bars and all. */
    Q_INVOKABLE void moveTrack(int track, int by);

    /** The library the current part uses, by name; empty for a General MIDI part. */
    QString samplerHere() const;
    QVariantList libraries() const;
    QStringList collections() const;

    /** Plays the current part from `path`, or from a programme when empty. */
    Q_INVOKABLE void setSamplerHere(const QString &path);

    /** Looks again at where sample libraries live. */
    Q_INVOKABLE void rescanLibraries();

    /** What the current part's chain loaded, in order, by name. */
    QStringList effectsHere() const;
    QVariantList chainHere() const;

    /**
     * Turns a knob on the part on the page, and remembers where it was left.
     *
     * Remembered because the player is rebuilt whenever a note is edited, and
     * an amplifier that reset itself every time somebody typed a fret would
     * be an amplifier nobody could use.
     */
    Q_INVOKABLE void setEffectControl(int stage, int index, double value);

    /**
     * What a part is played through, in a few words: "GxAmp \u00b7 GxCabinet".
     *
     * For the mixer, which shows every part at once and has room for a line
     * rather than a chain. Empty where a part has no effects on it, so that
     * the strips of the parts that do are the ones that say anything.
     */
    Q_INVOKABLE QString effectsSummary(int track) const;

    /**
     * The rig a window was opened with, applied in one go.
     *
     * `fretwork FILE.gp --sfz 0=… --lv2 0=…` opens the window with that part
     * already sampled and amplified, instead of opening it dry and making
     * somebody rebuild by hand what they just typed on a command line.
     */
    Q_INVOKABLE void applyRig(const QVariantMap &samplers, const QVariantMap &effects);

    /**
     * The SoundFont named on the command line, for the window as well.
     *
     * `--soundfont` reached `--render` and `--play` and stopped there, so
     * opening a window with it did nothing and said nothing. Empty means the
     * usual search, which is what a window opened without one does.
     */
    Q_INVOKABLE void useSoundFont(const QString &file);
    QVariantList availableEffects() const;

    QVariantList voicings() const;

    /**
     * Sets one plugin on the current part to a named guitarix voicing.
     *
     * Several knobs at once and nothing else -- the chain is not rebuilt, so
     * the part does not stop. What the plugin would not take goes to the
     * status line rather than nowhere: a voicing that quietly lost its
     * cabinet, or its level, is not the voicing on the label, and the person
     * listening is the one who needs to know.
     */
    Q_INVOKABLE void applyVoicing(int stage, const QString &name);

    /** Puts a plugin on the end of the current part's chain. */
    Q_INVOKABLE void addEffect(const QString &uri);

    /** Takes the last one off, which is the one a person just regretted. */
    Q_INVOKABLE void removeLastEffect();
    Q_INVOKABLE void clearEffects();

    QStringList instrumentNames() const;
    QStringList instrumentIds() const;
    QString instrumentHere() const;
    QString trackNameHere() const;

    /**
     * Writes to the file it was opened from, if that is one of ours.
     *
     * Returns false where there is nowhere to write to yet -- a score imported
     * from a Guitar Pro file has no Fretwork file behind it, and saving over
     * the original is not something to do quietly. The window turns that false
     * into a Save As.
     */
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);

    QString filePath() const;
    bool savesInPlace() const;

    QString title() const;
    QString artist() const;
    QString fileName() const;
    bool hasScore() const;
    QString status() const;
    QString problem() const;
    QString caretText() const;
    QString tempoTermHere() const;
    int beatsHere() const;
    int beatNow() const;

    QStringList trackNames() const;
    QStringList trackIcons() const;
    QString trackIconHereOnInk() const;
    int trackCount() const;
    int barCount() const;
    bool hasSections() const;
    int caretBar() const;
    int currentTrack() const;
    void setCurrentTrack(int track);

    bool isFollowing() const;
    void setFollowing(bool on);
    int portLinks() const;
    bool isGraphRolling() const;

    bool isPortsOn() const;
    void setPortsOn(bool on);
    int portCount() const;

    bool isClickOn() const;
    void setClickOn(bool on);
    double clickGain() const;
    void setClickGain(double gain);

    bool isPlaying() const;
    bool canPlay() const;
    double position() const;
    double length() const;
    int currentBar() const;

    // ---- transport ----

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);

    /**
     * Puts the caret at the start of a bar and the playhead with it.
     *
     * The first time through, where the bar is inside a repeat: a click on bar
     * 12 means the twelfth bar of the score, and which pass through it was
     * meant is not a question a bar number can answer.
     */
    Q_INVOKABLE void goToBar(int bar);

    /** The name the score gives a bar, where it names one: "Intro", "Chorus". */
    Q_INVOKABLE QString sectionAt(int bar) const;

    // ---- the mixer ----

    /**
     * The open pitches of a track's strings, with any capo already in them.
     *
     * For the tuner, which wants what the string will sound when it is
     * plucked. Empty for a drum kit, which has none, and for a track the
     * importer found no tuning for.
     */
    Q_INVOKABLE QVariantList stringPitches(int track) const;

    Q_INVOKABLE bool isMuted(int track) const;
    Q_INVOKABLE bool isSolo(int track) const;
    Q_INVOKABLE bool isAudible(int track) const;
    Q_INVOKABLE double gain(int track) const;
    Q_INVOKABLE void setMuted(int track, bool muted);
    Q_INVOKABLE void setSolo(int track, bool solo);
    Q_INVOKABLE void setGain(int track, double gain);

    // ---- editing ----

    Q_INVOKABLE void moveCursor(const QString &direction, bool extend = false);
    Q_INVOKABLE void typeDigit(int digit);
    Q_INVOKABLE void clearNote();

    double tempoHere() const;
    bool tempoWrittenHere() const;

    /** Sets the tempo from the caret's bar on. Says so where it will not. */
    Q_INVOKABLE void setTempoHere(double quarterBpm);

    /** Takes the caret's bar's own tempo change away, where it has one. */
    Q_INVOKABLE void clearTempoHere();

    QString tuningHere() const;
    int capoHere() const;
    int stringsHere() const;

    /** Retunes the current track. Frets stay where they are; pitches move. */
    Q_INVOKABLE void setTuningHere(const QString &names);
    Q_INVOKABLE void setCapoHere(int fret);

    QString sectionHere() const;

    /** Names the caret's bar, or takes its name off when given nothing. */
    Q_INVOKABLE void setSectionHere(const QString &name);

    QString timeHere() const;
    bool timeWrittenHere() const;

    /**
     * Sets the time signature from the caret's bar until the next change.
     *
     * Takes it written down -- "6/8" -- because that is how a musician says
     * one and how the field they type it into reads.
     */
    Q_INVOKABLE void setTimeHere(const QString &signature);

    /** Moves the note under the caret along its string, or the whole selection. */
    Q_INVOKABLE void transpose(int frets);

    /** Moves the note under the caret to the next string, keeping its pitch. */
    Q_INVOKABLE void moveNoteAcross(int strings);

    /**
     * Marks the note under the caret, or the whole selection: "dead", "ghost",
     * "palmMute" or "letRing".
     */
    Q_INVOKABLE void toggleMark(const QString &mark);

    /** 4 is a crotchet, 8 a quaver: the name musicians already use. */
    Q_INVOKABLE void setDuration(int denominator);
    Q_INVOKABLE void toggleDot();
    Q_INVOKABLE void scaleDuration(int steps);

    Q_INVOKABLE void insertBeat();
    Q_INVOKABLE void deleteBeat();

    /** An empty bar at the caret, across every track. */
    Q_INVOKABLE void insertBar();

    /** An empty bar on the end of the score, and the caret in it. */
    Q_INVOKABLE void appendBar();

    /** Takes the bar under the caret out, unless it is the only one. */
    Q_INVOKABLE void deleteBar();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /** Puts the caret where the score was clicked, in the view's own coordinates. */
    Q_INVOKABLE void placeCursorAt(qreal x, qreal y, bool extend = false);

    // ---- selection and the clipboard ----

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();

    bool hasSelection() const;
    Editing::Range selection() const;

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isModified() const;

    /** Where the caret is, for whatever draws it. */
    Cursor cursor() const;

    /** Seconds to hours:minutes:seconds, for a label. */
    Q_INVOKABLE QString clock(double seconds) const;

    /** The laid-out tablature, for whatever draws it. */
    const Tab::Layout &layout() const;

    /** Lays the current track out again at a new width, in points. */
    void relayout(qreal width);

Q_SIGNALS:
    void scoreChanged();
    void statusChanged();
    void problemChanged();
    void currentTrackChanged();
    void playingChanged();
    void clickChanged();
    void portsChanged();
    void samplersChanged();
    void effectsChanged();
    void positionChanged();
    void layoutChanged();
    void mixerChanged();
    void historyChanged();
    void cursorMoved();

private:
    void rebuildLayout();
    void rebuildPlayer();
    void setStatus(const QString &status);
    void setProblem(const QString &problem);

    /**
     * Keeping the rig, which is the point of `Rig` -- see `rigfile.h` for why
     * it lives beside the score rather than inside it.
     *
     * `rememberRig` is called wherever the rig changes and only schedules the
     * write, because a knob being dragged changes it sixty times a second and
     * a file written that often is a file being written while it is read.
     * `writeRig` is what the timer runs, and the destructor, so quitting mid-
     * drag does not lose the last turn of a knob.
     */
    void rememberRig();
    void writeRig();
    void restoreRig();

    /** The rig as it stands, with knobs named by symbol rather than by port. */
    Rig::Document currentRig() const;

    Editor m_editor;
    QList<int> m_order;
    bool m_playerStale = false;
    std::unique_ptr<Timeline::Clock> m_clock;
    std::unique_ptr<Player> m_player;

    Tab::Layout m_layout;
    qreal m_width = 900;
    int m_currentTrack = 0;
    bool m_click = false;
    bool m_ports = false;
    bool m_following = false;
    QHash<int, QString> m_samplers;
    QHash<int, QStringList> m_effects;
    QList<Gx::Voicing> m_voicings;
    QHash<int, QHash<int, QHash<quint32, float>>> m_knobs;
    QList<Lv2::Description> m_plugins;
    QList<Sfz::Library> m_libraries;
    double m_clickGain = 1.0;
    int m_currentBar = -1;
    bool m_wasPlaying = false;

    QString m_fileName;
    QString m_filePath;
    QString m_status;
    QString m_problem;
    QString m_soundFont;
    QTimer m_ticker;
    QTimer m_rigWriter;
};
