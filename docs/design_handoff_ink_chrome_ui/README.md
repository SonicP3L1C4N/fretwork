# Handoff: Fretwork ink-chrome UI (design 5a)

## Overview
A visual treatment for Fretwork's main window, derived from the app icon's own colouring: near-black ink chrome (title bar, toolbar, status bar), the score on paper, and the icon's magenta fret-marker as the single accent. Layout is the shipped P2 window (tablature left, mixer right, transport top) with two additions: a docked **Stems** render panel and toolbar toggles that show/hide the Mixer, Stems and Status panels independently.

## About the Design Files
The files in this bundle are **design references created in HTML** — a prototype showing intended look and behavior, not production code. Fretwork's UI is Qt 6 / KDE Frameworks 6 with QML (see `src/gui/session.h` — the `Session` facade already exposes everything these panels need). The task is to **recreate this design in QML/Qt**, using the existing `Session` properties and invokables; nothing here requires new engine work.

## Fidelity
**High-fidelity.** Colors, spacing and type below are exact. One deliberate substitution: the mockup uses Source Serif 4 for all chrome text (a design-system choice from the mockup environment). For the real app, either ship Source Serif 4 (SIL OFL) for headings/labels, or keep the system font and apply only the color/layout treatment — decide by taste; the color treatment is the point of this design.

## Screens / Views

### Combined window, current direction (see 10a-combined-window.png)
Design 10a supersedes 5a's layout while keeping its ink-chrome palette: title bar → toolbar → content row (tracks rail left · score centre · effects deck + mixer right) → bars ruler → status bar. It adds the tracks rail (instrument badge icons from `icons/instrument-*.svg`), the cassette-deck effects panel (GxCabinet voicing; binds `chainHere` / `setEffectControl` / `applyVoicing`), the serif bars ruler with magenta lozenge playhead, and the click visualiser (one note glyph per counted beat, downbeat taller, sounding beat magenta). Token values and states below still apply.

### Main window, 5a baseline (see 5a-ink-chrome-full-window.png)
Vertical stack: title bar → toolbar → content row (score canvas + mixer panel) → stems panel → status bar.

**Title bar** — ink `#201e1d`, text `#f3f2f2`. App icon 18px left; centered title `Title — Artist — Fretwork` at 14px semibold; window controls right in `#9b9797`. Padding 10px 16px.

**Toolbar** — ink `#201e1d`, 1px top border `#444141`, padding 10px 16px, 10px gaps. Left to right:
- Open, Save: 34×34px icon buttons, transparent fill, 1px border `#605d5d`, icon stroke `#f3f2f2`, radius 2px.
- Undo, Redo: same, borderless; disabled at 45% opacity. Bind to `canUndo`/`canRedo`; tooltip from `undoText`/`redoText`.
- Play: 34×34px filled `#d6006c`, glyph `#f3f2f2`. Stop: outlined like Open.
- Elapsed time (13px, tabular figures), seek slider (4px track `#444141`, filled portion and 14px round handle `#d6006c`), total length. Bind `position`/`length`/`seek()`.
- Track selector: 150px combo, fill `#2d2b2b`, text `#f3f2f2`, border `#605d5d`. Binds `trackNames`/`currentTrack`.
- Panel toggles "Mixer", "Stems", "Status": text buttons 13px; ON = filled `#d6006c` on `#f3f2f2` text; OFF = outlined `#605d5d`, text `#f3f2f2`.

**Score canvas** — unchanged from the current build except accent color: background paper `#f3f2f2`, staff lines `#bab6b6`, bar lines `#605d5d`, fret numbers `#201e1d`, bar numbers `#9b9797`, stems/beams `#444141`. Playing bar highlight: `#ffdee6` (was light blue); fret numbers inside it `#aa0b56`. Caret: 1.6px `#d6006c` rounded rect around the fret. Padding 20px 24px.

**Mixer panel** (right, 290px fixed) — background `#f8f4f4`, padding 20px 22px. Heading "Mixer" 20px semibold. Per track (20px vertical gap):
- Row: track name 14px (soloed track: semibold `#aa0b56`), then S and M buttons 26×26px, 12px text. Active S = filled `#d6006c`; active M = filled `#201e1d` text `#f3f2f2`; inactive = 1px border `rgba(32,30,29,.16)`. Muted track row at 50% opacity.
- Gain slider: 4px track `#d7d3d3`, filled portion + 14px handle `#d6006c`. Binds `gain`/`setGain`, `isMuted`/`isSolo`.
- Footer note 12px at 55% text opacity: "One synth per track. Solo and mute take effect live."

**Stems panel** (docked, full width, below content row) — background `#eae7e7`, padding 16px 24px 20px. Header row: "Stems" 17px semibold; note 12px muted "one WAV per track, plus a mix · ~20× real time"; right-aligned progress label 12px `#aa0b56` ("Rendering 02-electric-bass.wav — 62%"). File list: one inline row, 26px gaps, 13px tabular — done files show peak (`00-guitar-i.wav · 0.25`), the writing file in `#aa0b56`, queued files in `#7d7979`, `mix.wav` semibold. Cancel button right (outlined). Progress bar: 4px, fill `#d6006c` on `#d7d3d3`.

**Status bar** — ink `#201e1d`, text `#f3f2f2`, 13px, padding 9px 20px. Left: `Bar N · string N · duration` (bar number semibold). Middle, `#9b9797`: playhead note. Right, `#ff90b1`: undo text.

## Interactions & Behavior
- Mixer/Stems/Status toggles show/hide their panels independently; score canvas takes the freed width/height. Persist visibility in app config (KConfig).
- S/M take effect live during playback (already true in the engine).
- Seek slider drags call `seek(seconds)`; playhead polling updates it (already how `Session` works).
- Stems panel appears when a render starts (or via its toggle), shows per-file completion and peaks as the renderer reports them, Cancel aborts.
- Hover states: filled magenta controls darken to `#d82071`; outlined ink-chrome controls fill `rgba(243,242,242,.12)`. Focus: 2px outline `#d6006c`, offset 2px.

## State Management
All required state exists on `Session` (playing, position, length, currentBar, trackNames, mixer, undo/redo). New: three booleans for panel visibility (config-backed), and a render-progress model for the stems panel (file name, state, peak, overall %).

## Design Tokens
Ink `#201e1d` · Paper `#f3f2f2` · Magenta accent `#d6006c` (hover `#d82071`, deep text `#aa0b56`, on-dark `#ff90b1`, tint `#ffdee6`)
Neutrals: `#f8f4f4` `#eae7e7` `#d7d3d3` `#bab6b6` `#9b9797` `#7d7979` `#605d5d` `#444141` `#2d2b2b`
Spacing: 4px sliders, 10px control gaps, 16–24px panel padding, 20px mixer row gap · Radius 2px · Type: 12/13/14 chrome, 17/20 panel headings, semibold 600; tabular figures for all times.
The old cyan (`#0088b0` family) is fully replaced by magenta in this treatment.

## Assets
- App icon: `icons/sc-apps-io.github.sonicp3l1c4n.fretwork.svg` (already in the repo; its palette is the source of these tokens).
- No other assets. Toolbar glyphs are simple strokes (or use the desktop icon theme).

## Files
- `10a-combined-window.png` — the combined window render, current direction (2×).
- `5a-ink-chrome-full-window.png` — the full window render (2×).
- `fretwork-mockups.html` — the full exploration document (design 5a is the top section; 3a/4a–4c below show the layout and palette studies it came from). It is an HTML prototype; open in a browser.
