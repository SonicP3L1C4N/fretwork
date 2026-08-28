<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The eight crashes of 27 August

Eight coredumps in one afternoon, every one of them an LV2 run, recorded
nowhere but a cache directory that gets cleaned. This is the record, written on
**2026-08-29** from the dumps themselves before they were lost, and it is kept
as a document rather than as a closed issue because the diagnosis is about
what a host owes a plugin and that argument outlives the bug.

## What was seen

| Time | Signal | Command |
|---|---|---|
| 13:22:32 | SIGSEGV | `--render`, `gx_amp` + `gx_cabinet` on one part |
| 13:23:05 | SIGSEGV | as above |
| 13:23:36 | SIGSEGV | `--render`, `gx_amp` alone |
| 13:23:37 | SIGSEGV | `--render`, `gx_cabinet` alone |
| 15:23:37 | SIGABRT | `--render --dry`, `gx_amp`, with a voicing |
| 20:57:00 | SIGABRT | the window, `gx_amp` + `gx_cabinet` |
| 20:57:28 | SIGSEGV | as above |
| 20:59:08 | SIGSEGV | as above |

They look like two bugs and they are one. The afternoon four die on the main
thread inside the plugin:

```
#0  gx_amp.so + 0x57ba
#1  lilv_instance_run
#2  Render::stems
```

and one of those four is worth reading twice, because its program counter is
`0x3246` — not a bad address in the plugin but a jump through a pointer that
was no longer a function. The evening four die in worker threads, several at
once, and they name the library:

```
#0  fftwf_mkapiplan            (libfftw3f.so.3)
#3  fftwf_plan_dft_r2c_1d
#4  Convlevel::configure       (libzita-convolver.so.4)
#5  Convproc::configure
#6  gx_cabinet.so
#9  serve                      (fretwork)
```

Three other threads in the same dump are inside `fftwf_mkplan_d` at that
instant. Two dumps abort out of `_int_free_merge_chunk` — glibc finding a heap
it cannot believe — and one of those has a thread parked in `futex_wait`
*inside* `_int_free_chunk`, which is a lock taken over a corrupted arena.

**Fretwork links no FFTW.** It arrives inside a guitarix plugin, which reaches
it through zita-convolver, and its heap is this program's heap.

## What was wrong

FFTW documents that its planner is the part of the library that is not
reentrant: `fftw_execute` may be called from several threads, and every routine
that creates or destroys a plan may not. Guitarix builds its cabinet by
configuring a convolver, the convolver plans, and the planning happens in the
plugin's `work` callback — which is correct of it, because that is precisely
the errand the worker extension exists to carry.

`Worker` gave every instance a thread of its own. That is what the extension is
shaped for and what other hosts do, and here it is a mistake, because of nine
lines in `process`:

```cpp
lilv_instance_run(stage.left, uint32_t(frames));
lilv_instance_run(stage.right, uint32_t(frames));
```

A mono plugin is instantiated twice, one instance per side, and those two lines
schedule both sides' errands one after the other with nothing in between. Two
planners overlap every time a chain plays its first block. A chain of two
plugins is four, and the next stage's runs start before the last stage's
workers have finished.

## How it was measured

Counting rather than reasoning, because the crash itself is a coin toss —
sixteen workers over eighteen renders on 29 August produced no crash at all,
which says how thin the window is and nothing whatever about whether it is
there.

A counter around the `work` call, incremented on entry and decremented on exit,
recording its own high-water mark. One render of four parts with a two-plugin
chain on each:

```
     22 [worker] concurrent=1 peak=3
      4 [worker] concurrent=2 peak=3
      1 [worker] concurrent=3 peak=3
```

Five of thirty errands ran while another was in flight, and at the worst moment
three threads were inside a planner that may hold one. That is the bug, and it
is present on every run whether or not it crashes on that run.

## What was done

One mutex in `serve`, static, held across the plugin's `work` callback, so that
one errand runs at a time in the process no matter which instance asked for it.

The reason it is process-wide rather than per chain is that the sharing is not
the chain's. Two chains on two parts are two guitarix cabinets reaching the
same copy of FFTW, and a lock per chain would leave exactly the crash that was
seen at 20:57. The reason it is not narrower still — a lock around FFTW rather
than around the errand — is that the host cannot see FFTW: it is a
dependency of somebody else's plugin, arriving through a library this program
does not link and cannot name.

Which is the general form of it, and the part worth keeping:

> A host cannot know what a plugin's errand touches. The specification says
> `work` is called off the audio thread; it does not promise the plugin that no
> other `work` is running, and a plugin written against a library with global
> state has no way to defend itself. The host is the only place the
> serialisation can go.

The price is paid entirely off the audio thread. An errand exists because it is
too slow for the callback and the callback is not waiting for it, so
serialising errands costs setup time and never a block. The stems rendered
after the change are byte-for-byte the stems rendered before it.

## What is still open

- **The four afternoon dumps are inferred, not proven.** Their stacks end in
  the plugin's `run` rather than in a planner, and the reading here is that
  they are the same corruption surfacing one call later — the `0x3246` program
  counter is hard to read any other way. They cannot be reproduced now, so
  this stays a reading.
- **Nothing in the test suite guards it.** The observation needs a plugin with
  a worker installed, so any test of it skips where guitarix is absent, which
  is most machines and all of CI.
- **This is one library, found by crashing.** Every other non-reentrant
  dependency inside every other plugin is now covered by the same mutex, which
  is the argument for having made it process-wide, and none of them are known
  by name.
