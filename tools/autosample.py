#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""Auto-sampler: drive an instrument in a DAW over MIDI, record what it plays back, cut it into
sample files. The DAW is the instrument; this is the tape machine and the razor blade.

  autosample.py devices                 list CoreAudio input devices (ffmpeg avfoundation)
  autosample.py doctor                  check MIDI + audio + a 1 s capture, print the wiring
  autosample.py probe --note A4         record N seconds and say what is in the audio
  autosample.py ports / port / miditest  MIDI ports: list, hold one open, loopback test
  autosample.py presets                 what to record per instrument, and what it costs
  autosample.py session SET [flags]     the sampling run: writes assets/samples-own/SET/*.wav
  autosample.py mid SET OUT.mid         the session as one MIDI file + plan.json (the DAW path)
  autosample.py render SET BIG.wav      slice that file's flattened export back into the set
  autosample.py pack SET [--out P]      that session -> one user slot (.hdr/.bin, roots chosen to fit)
  autosample.py selftest                the whole pipeline against an internal synth, no devices

Signal path (no third-party Python: CoreMIDI through ctypes, audio through ffmpeg):

    autosample.py -- note-on/off --> [CoreMIDI virtual source] --> DAW instrument track
                                                                        |
    autosample.py <-- s16le 48 kHz <-- [ffmpeg avfoundation] <-- BlackHole 2ch <-- DAW master

Setup, once:
  1. brew install blackhole-2ch   (or the 16ch installer; 2ch is enough, the engine is mono)
  2. In Audio MIDI Setup make a Multi-Output Device = your speakers + BlackHole 2ch, or put
     BlackHole on the DAW's master as a second output; either way the DAW's master must reach
     BlackHole *and* you must still hear it.
  3. The DAW track's instrument gets MIDI input from "StudioKEYS Autosampler" (this script's
     virtual source -- no IAC Driver, no setup). The source exists only while the script runs, so
     run `autosample.py port` first, enable the port in the DAW, then run the session. A DAW that
     will not show a virtual source at all: `--midi-out 'IAC Driver Bus 1'` sends to a port that
     already exists instead.
  4. The instrument must be dry: no reverb, no delay, no compressor, no limiter, no master
     normalization. The builder fits a decay curve to what you record; a reverb tail is not a
     piano, and a limiter is not a fortissimo.

What it records, per note per velocity layer: `preroll` of lead-in (so the attack is never
missed -- the DAW's latency does not have to be known or compensated), `hold` seconds of note,
then `tail` seconds of release. It then finds the onset itself, trims to it, and writes the WAV
at the capture rate. The file name is the only metadata gen_samples.py needs:

    {idx:02d}_{SET}_{NOTE}_v{layer}.wav      e.g. 03_MYSTRINGS_A3_v2.wav

NOTE is scientific pitch with **C4 = 60** (sampleio.note_from_name, the web editor's convention),
so a folder of these votes its own octave convention in gen_samples.py and gets the right roots.

The files land in `assets/samples-own/SET/` (`--out` moves it): `session.json` plus
`NN_SET_NOTE_vL.wav`, NN being the note's position in the whole plan. A default run keeps every
take that already exists (`keep (exists)`), so an interrupted session resumes where it stopped.

  redo one note     session SET --only A4 --overwrite     (same file name: only that take changes)
  redo one velocity session SET --only A4 --vel 40 --overwrite
  redo everything   session SET --overwrite               (or delete the folder)
  a rejected take   is on disk as `..._BAD.wav`; no builder reads it, and a rerun replaces it

## The DAW path (no real time, no BlackHole)

`mid SET OUT.mid` writes the whole session as one MIDI file on a tick grid -- one note every whole
number of beats at 120 BPM, so the arrangement shows the notes on the bar lines -- plus
`plan.json`, which is what `render` reads. Drop the file in the DAW, freeze the track, flatten the
clip, export one WAV, and `render SET THAT.wav` cuts it back into the same `NN_SET_NOTE_vL.wav`
names (and the same `session.json`). The transcription is offline, exact, and repeatable, it keeps
the patch's own velocity curve and any plugin delay compensation, and it costs no attention.

The render is cut by grid *and* by measured attack: the tick says where the note should be, the
onset search inside a +-20 % period window says where it actually is (a flattened clip can start a
few ms -- or a bar -- away from the file's tick 0), and the median residual is printed as evidence
that the file really is this render (a wrong `--bpm` shows up as a drift of whole periods).

`--input FILE` plays a file instead of a device (real time, no MIDI is sent), which is how the
capture path is checked without a DAW: one note per run, since the file's own timeline becomes the
session's.

`session.json` beside them carries the exact MIDI note, velocity, rate, measured peak, DC offset
and tuning, so a builder can trust the manifest instead of re-detecting anything. Tuning is
measured as the energy centroid of the partials within +-45 ct of the note, and `detune_ct` is
how far they spread around it (above the window's own leakage, measured on a reference tone): a
steady note reads ~1 ct, a 3-string piano unison 3-6, a string/horn ensemble or a vibrato/chorus
10-30. That spread is not mistuning -- it is the patch, recorded forever, and it transposes with
any zone stretch.

Velocity layers are *timbre*, not gain: play them at the velocities you ask for (default 40 / 84
/ 118) and the instrument must actually respond to velocity. Three layers of the same ff sample
is one layer recorded three times. They are also not playable yet: SAMPLE's zone table is
`(root, lo, hi)` with no velocity dimension (`smp_zone_t` in eng_sample.c, and the user-slot
header the web editor writes), so the v2/v3 takes are a bank for a velocity-aware build. Recording
them now is right -- re-recording a 15-minute session later is not -- and `session` prints the
figure so nothing is a surprise.

`presets` holds the musical decisions (ranges, grids, hold, and the widest stretch each instrument
tolerates); `session NAME --preset strings` is the whole command line. `pack` then does the
opposite of recording: it picks how many of those roots can fit a 36 KiB user slot, how long each
one may be, and reports the stretch in semitones. A slot is 3.3 s of audio, so it is a sketch that
plays -- the instrument itself goes into the image via gen_samples.py.
"""
import argparse
import ctypes
import json
import math
import os
import re
import struct
import subprocess
import sys
import threading
import time
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
OWN = SRC / "assets" / "samples-own"
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def note_name(m):
    """MIDI note -> "C4" (C4 = 60)"""
    return f"{NOTE_NAMES[m % 12]}{m // 12 - 1}"


def note_from_name(s, default=60):
    """'C4' / 'F#3' / 'Bb2' -> MIDI note (C4 = 60)"""
    return sio.note_from_name(s, default)


def _grid(lo, hi, step):
    """every `step` semitones from lo to hi, inclusive of hi if it lands on the grid"""
    return list(range(lo, hi + 1, step))


# Instrument presets: the musical decisions, so a session is one word instead of six flags.
#   roots  where to record. Dense on purpose: recording costs disk and time, the *build* is where
#          flash decides how many of them survive (pack, or gen_samples' zone thinning).
#   vels   velocity layers (timbre, not gain -- the patch must actually respond)
#   hold   seconds held down. A piano needs its decay; a bowed/brass note needs a long enough
#          sustain for a loop point to exist at all.
#   zone   the widest stretch that still sounds like the instrument: a steady tone tolerates 4-5
#          st, a patch with vibrato/ensemble spread does not (stretching transposes the spread).
#   dry    what must be off before recording, because the builder fits a curve to what it gets.
PRESETS = {
    "piano": dict(
        roots=_grid(21, 108, 3), vels=(32, 80, 118), hold=7.0, tail=2.0, zone=3,
        desc="A0..C8, every 3 semitones, 3 layers, 7 s hold",
        dry="reverb/chorus OFF, no master limiter, no velocity->volume compression; "
            "sustain pedal down for the whole hold so the decay is the real one"),
    "strings": dict(
        roots=_grid(43, 79, 3), vels=(45, 90, 118), hold=3.5, tail=1.5, zone=2,
        desc="G2..G5 section, every 3 semitones, 3 layers, 3.5 s hold",
        dry="no reverb, no delay, no chorus (the section spread must come from the patch, "
            "not from an FX unit: an FX tail is not a sample and flattens the loop)"),
    "cello": dict(
        roots=_grid(36, 67, 3), vels=(45, 95, 118), hold=3.5, tail=1.5, zone=2,
        desc="C2..F#4 cello section, every 3 semitones, 3 layers",
        dry="as strings: dry, no room, steady bow, no vibrato if you want a section bed"),
    "violin": dict(
        roots=_grid(55, 91, 3), vels=(45, 95, 118), hold=3.5, tail=1.5, zone=2,
        desc="G3..G6 violins, every 3 semitones, 3 layers",
        dry="as strings: dry, no room; vibrato only if it is the sound you want baked in"),
    "horn": dict(
        roots=_grid(54, 78, 4), vels=(50, 95, 118), hold=3.5, tail=1.5, zone=2,
        desc="F#3..F#5 sounding (the top of the horn), every 4 semitones, 3 layers",
        dry="dry, no hall; one player or the section patch, but not both"),
    "brass": dict(
        roots=_grid(34, 62, 4), vels=(50, 95, 118), hold=3.5, tail=1.5, zone=2,
        desc="Bb1..D4 sounding: the brass section for the octaves below the horn",
        dry="dry, no hall; a section patch is fine here (it is an ensemble by definition)"),
}


def preset_notes(name):
    """preset name (or a comma list) -> roots: "horn+brass" records the overlapping seam once"""
    roots = []
    for part in (name or "").split("+"):
        part = part.strip().lower()
        if part in PRESETS:
            roots += PRESETS[part]["roots"]
        elif part:
            raise SystemExit(f"unknown preset '{part}': {', '.join(sorted(PRESETS))}")
    return sorted(set(roots))


def db(x, floor=-120.0):
    import math
    return max(floor, 20.0 * math.log10(x)) if x > 0 else floor


# ---------------------------------------------------------------- WAV out ---
def write_wav(path, samples, sr):
    """int16 mono -> 16-bit PCM WAV (identical to what capture produced: no requantisation)"""
    a = array("h", samples)
    if sys.byteorder != "little":
        a.byteswap()
    import wave
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(a.tobytes())


# ---------------------------------------------------------------- MIDI out ---
_CF = None


def _cf():
    """CoreFoundation, once: CFString <-> Python str"""
    global _CF
    if _CF is None:
        cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        cf.CFStringCreateWithCString.restype = ctypes.c_void_p
        cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
        cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
        cf.CFStringGetCString.restype = ctypes.c_bool
        cf.CFRelease.argtypes = [ctypes.c_void_p]
        _CF = cf
    return _CF


def _cfstr(s):
    return _cf().CFStringCreateWithCString(None, s.encode(), 0x08000100)


def _cfstr_get(ref):
    buf = ctypes.create_string_buffer(512)
    return buf.value.decode("utf-8", "replace") if _cf().CFStringGetCString(ref, buf, 512, 0x08000100) else ""


class MidiOut:
    """MIDI out, two ways in: a virtual source we own, or an existing destination.

    * virtual (default): MIDISourceCreate makes an endpoint that *this process* pushes packets
      into with MIDIReceived. It shows up in a DAW as an input port. It exists **only while this
      process runs** -- that is the whole reason `autosample.py port` exists.
    * destination (`--midi-out NAME`): send to a port somebody else already owns, IAC Driver Bus 1
      being the usual one, or a hardware interface. Nothing to create, nothing to enable.
    """

    UTF8 = 0x08000100

    def __init__(self, name="StudioKEYS Autosampler", dest=None):
        self.cf = _cf()
        self.lib = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
        lib = self.lib
        lib.MIDIClientCreate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.POINTER(ctypes.c_uint32)]
        lib.MIDIClientCreate.restype = ctypes.c_int32
        lib.MIDISourceCreate.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        lib.MIDISourceCreate.restype = ctypes.c_int32
        lib.MIDIOutputPortCreate.argtypes = [ctypes.c_uint32, ctypes.c_void_p,
                                             ctypes.POINTER(ctypes.c_uint32)]
        lib.MIDIOutputPortCreate.restype = ctypes.c_int32
        lib.MIDIPacketListInit.argtypes = [ctypes.c_void_p]
        lib.MIDIPacketListInit.restype = ctypes.c_void_p
        lib.MIDIPacketListAdd.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                          ctypes.c_uint64, ctypes.c_size_t, ctypes.POINTER(ctypes.c_ubyte)]
        lib.MIDIPacketListAdd.restype = ctypes.c_void_p
        lib.MIDIReceived.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
        lib.MIDIReceived.restype = ctypes.c_int32
        lib.MIDISend.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        lib.MIDISend.restype = ctypes.c_int32
        lib.MIDIClientDispose.argtypes = [ctypes.c_uint32]
        lib.MIDIEndpointDispose.argtypes = [ctypes.c_uint32]
        lib.MIDIPortDispose.argtypes = [ctypes.c_uint32]
        lib.MIDIGetNumberOfSources.restype = ctypes.c_ulong
        lib.MIDIGetNumberOfDestinations.restype = ctypes.c_ulong
        lib.MIDIGetSource.argtypes = [ctypes.c_ulong]
        lib.MIDIGetSource.restype = ctypes.c_uint32
        lib.MIDIGetDestination.argtypes = [ctypes.c_ulong]
        lib.MIDIGetDestination.restype = ctypes.c_uint32
        lib.MIDIObjectGetStringProperty.argtypes = [ctypes.c_uint32, ctypes.c_void_p,
                                                    ctypes.POINTER(ctypes.c_void_p)]
        lib.MIDIObjectGetStringProperty.restype = ctypes.c_int32

        self.name = name
        self.virtual = dest is None
        self.client = ctypes.c_uint32(0)
        self.src = ctypes.c_uint32(0)
        self.port = ctypes.c_uint32(0)
        self.dest = ctypes.c_uint32(0)
        self.pkt = ctypes.create_string_buffer(1024)
        self.cfname = _cfstr(name)
        st = lib.MIDIClientCreate(self.cfname, None, None, ctypes.byref(self.client))
        if st:
            self.cf.CFRelease(self.cfname)
            raise RuntimeError(f"MIDIClientCreate failed ({st}); CoreMIDI is unreachable from this "
                               f"process")
        if self.virtual:
            st = lib.MIDISourceCreate(self.client, self.cfname, ctypes.byref(self.src))
            if st:
                self.close()
                raise RuntimeError(f"MIDISourceCreate({name!r}) failed ({st})")
        else:
            ref = self.find(dest, "destination")
            if not ref:
                names = [n for _, n in self.destinations()]
                self.close()
                raise RuntimeError(f"no MIDI destination matching {dest!r}; the destinations are: "
                                   + (", ".join(names) or "none"))
            self.dest = ctypes.c_uint32(ref)
            st = lib.MIDIOutputPortCreate(self.client, self.cfname, ctypes.byref(self.port))
            if st:
                self.close()
                raise RuntimeError(f"MIDIOutputPortCreate failed ({st})")
        # where packet[0] really starts: the API's own answer, so no guessing at struct padding
        base = ctypes.addressof(self.pkt)
        self.first = self.lib.MIDIPacketListInit(base) - base

    # ---- enumeration -------------------------------------------------------
    @staticmethod
    def _name_of(ref):
        lib = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
        lib.MIDIObjectGetStringProperty.argtypes = [ctypes.c_uint32, ctypes.c_void_p,
                                                    ctypes.POINTER(ctypes.c_void_p)]
        pid = _cfstr("name")
        out = ctypes.c_void_p(0)
        st = lib.MIDIObjectGetStringProperty(ref, pid, ctypes.byref(out))
        _cf().CFRelease(pid)
        if st or not out.value:
            return ""
        s = _cfstr_get(out.value)
        _cf().CFRelease(out.value)
        return s

    @classmethod
    def sources(cls):
        """[(endpoint ref, name)] of every MIDI source in the system, ours included"""
        lib = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
        lib.MIDIGetNumberOfSources.restype = ctypes.c_ulong
        lib.MIDIGetSource.argtypes = [ctypes.c_ulong]
        lib.MIDIGetSource.restype = ctypes.c_uint32
        return [(lib.MIDIGetSource(i), cls._name_of(lib.MIDIGetSource(i)))
                for i in range(lib.MIDIGetNumberOfSources())]

    @classmethod
    def destinations(cls):
        lib = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
        lib.MIDIGetNumberOfDestinations.restype = ctypes.c_ulong
        lib.MIDIGetDestination.argtypes = [ctypes.c_ulong]
        lib.MIDIGetDestination.restype = ctypes.c_uint32
        return [(lib.MIDIGetDestination(i), cls._name_of(lib.MIDIGetDestination(i)))
                for i in range(lib.MIDIGetNumberOfDestinations())]

    @classmethod
    def find(cls, want, kind="source"):
        """resolve a name substring (case-insensitive) or an exact name -> endpoint ref or 0"""
        items = cls.sources() if kind == "source" else cls.destinations()
        for ref, n in items:
            if n == want:
                return ref
        for ref, n in items:
            if want.lower() in n.lower():
                return ref
        return 0

    # ---- send --------------------------------------------------------------
    def send(self, data):
        buf = (ctypes.c_ubyte * len(data))(*data)
        base = ctypes.addressof(self.pkt)
        p = self.lib.MIDIPacketListInit(base)
        p = self.lib.MIDIPacketListAdd(base, ctypes.sizeof(self.pkt), p, 0, len(data), buf)
        if not p:
            raise RuntimeError("MIDIPacketListAdd: packet list full")
        if self.virtual:
            self.lib.MIDIReceived(self.src, base)
        else:
            st = self.lib.MIDISend(self.port, self.dest, base)
            if st:
                raise RuntimeError(f"MIDISend failed ({st})")

    def note_on(self, ch, n, v):
        self.send([0x90 | ch, n & 127, max(1, min(127, v))])

    def note_off(self, ch, n):
        self.send([0x80 | ch, n & 127, 64])

    def cc(self, ch, cc, v):
        self.send([0xB0 | ch, cc & 127, max(0, min(127, v))])

    def panic(self, chans=range(4)):
        """every note off + dampers up: the run must never leave a note hanging in the DAW"""
        for ch in chans:
            self.cc(ch, 64, 0)          # sustain off first, or CC123 leaves them held
            self.cc(ch, 123, 0)         # all notes off
            self.cc(ch, 120, 0)         # all sound off
            self.cc(ch, 1, 0)           # modulation wheel, so the next note is not vibratoed

    def close(self):
        try:
            if self.src.value:
                self.lib.MIDIEndpointDispose(self.src)
                self.src = ctypes.c_uint32(0)
            if self.port.value:
                self.lib.MIDIPortDispose(self.port)
                self.port = ctypes.c_uint32(0)
            if self.client.value:
                self.lib.MIDIClientDispose(self.client)
                self.client = ctypes.c_uint32(0)
        finally:
            if getattr(self, "cfname", None):
                self.cf.CFRelease(self.cfname)
                self.cfname = None


def midi_ports():
    """everything CoreMIDI knows, with ours marked"""
    print("MIDI sources (what a DAW can receive from):")
    for _, n in MidiOut.sources():
        print(f"   {n}" + ("      <- this script's virtual port" if n == "StudioKEYS Autosampler" else ""))
    print("MIDI destinations (what this script can send to, --midi-out NAME):")
    for _, n in MidiOut.destinations():
        print(f"   {n}")
    print("an empty destinations list is normal: IAC Driver is off by default, and IAC is the")
    print("fallback when a DAW refuses to show a virtual source (Audio MIDI Setup -> MIDI Studio ->")
    print("IAC Driver -> Device is online, then it appears here as 'IAC Driver Bus 1')")


def midi_loopback():
    """prove the send path end to end, with no DAW: create a destination, send to it, read it back"""
    import time as _t
    lib = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
    got = []
    READ = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    out = MidiOut("StudioKEYS Loopback", dest=None)
    first = out.first

    def _read(pktlist, refcon, conn):
        base = int(pktlist)
        n = ctypes.c_uint32.from_address(base).value
        b = base
        for _ in range(n):
            ln = ctypes.c_uint16.from_address(b + first + 8).value
            got.append(bytes(ctypes.string_at(b + first + 10, ln)))
            b = b + first + 10 + ((ln + 3) & ~3)          # MIDIPacketNext: 4-byte aligned

    cb = READ(_read)
    lib.MIDIDestinationCreate.argtypes = [ctypes.c_uint32, ctypes.c_void_p, READ, ctypes.c_void_p,
                                         ctypes.POINTER(ctypes.c_uint32)]
    lib.MIDIDestinationCreate.restype = ctypes.c_int32
    name = f"StudioKEYS Loopback {os.getpid()}"
    nm = _cfstr(name)
    dst = ctypes.c_uint32(0)
    st = lib.MIDIDestinationCreate(out.client, nm, cb, None, ctypes.byref(dst))
    _cf().CFRelease(nm)
    if st:
        out.close()
        print(f"  MIDIDestinationCreate failed ({st})")
        return 1
    try:
        sender = MidiOut("StudioKEYS Loopback send", dest=name)
        sent = [bytes([0x90, 60, 100]), bytes([0x80, 60, 64]), bytes([0xB0, 64, 0])]
        for msg in sent:
            sender.send(msg)
        t0 = _t.monotonic()
        while sum(len(g) for g in got) < sum(len(m) for m in sent) and _t.monotonic() - t0 < 1.0:
            _t.sleep(0.01)
        sender.close()
        # CoreMIDI coalesces back-to-back sends into one packet (and a real port may use running
        # status), so compare the byte stream, not the packet count
        stream = b"".join(got)
        want = b"".join(sent)
        print(f"  sent {len(sent)} messages ({len(want)} B), received {len(got)} packet(s) / "
              f"{len(stream)} B: {stream.hex(' ')}" + ("  ok" if stream == want else "  MISMATCH"))
        return 0 if stream == want else 1
    finally:
        lib.MIDIEndpointDispose(dst)
        out.close()


# ---------------------------------------------------------------- audio in ---
class FfmpegIn:
    """CoreAudio input through ffmpeg's avfoundation input, as one continuous s16le stream.

    ffmpeg runs for the whole session and its stdout is drained by a thread into one buffer, so a
    note is captured by *frame index*, never by wall clock: the DAW's (and ffmpeg's) latency can be
    anything at all, the preroll simply has to be longer than it. `-ac 1` downmixes, `-ar SR` is
    the capture rate (the DAC of the Multi-Output device decides -- 48 kHz in practice).
    """

    def __init__(self, dev_index, sr=48000, preroll=0.25, verbose=False):
        self.sr, self.index, self.preroll = sr, dev_index, preroll
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.err = []
        self.proc = None
        self.stop_flag = False
        self.total = 0                       # frames received, monotonic
        self.base = 0                        # the absolute index of buf[0]
        self.verbose = verbose

    def start(self):
        import threading
        cmd = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
               "-f", "avfoundation", "-i", f":{self.index}"] + \
              ["-ac", "1", "-ar", str(self.sr), "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"]
        if self.verbose:
            print("  ffmpeg " + " ".join(cmd[3:]))
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()
        self.te = threading.Thread(target=self._err, daemon=True)
        self.te.start()
        t0 = time.monotonic()
        while self.total < int(self.preroll * self.sr):
            if time.monotonic() - t0 > 5.0:
                raise RuntimeError("no audio from ffmpeg after 5 s: " + (" ".join(self.err) or
                                   "device busy, wrong index, or the microphone permission was denied"))
            time.sleep(0.02)
        return self

    def _err(self):
        for line in iter(self.proc.stderr.readline, b""):
            self.err.append(line.decode("utf-8", "replace").strip())
            del self.err[:-8]

    def _reader(self):
        fd = self.proc.stdout.fileno()
        while not self.stop_flag:
            try:
                b = os.read(fd, 1 << 15)
            except OSError:
                break
            if not b:
                break
            b = b[:len(b) & ~1]                       # whole frames only
            with self.lock:
                self.buf += b
                self.total += len(b) // 2

    def frames(self):
        return self.total

    def wait(self, frames, timeout=8.0):
        """block until `frames` frames have arrived (never returns early: a short read would cut
        the tail off the note)"""
        t0 = time.monotonic()
        while self.total < frames:
            if self.stop_flag or (self.proc and self.proc.poll() is not None):
                raise RuntimeError("ffmpeg stopped mid-session: " + (" ".join(self.err) or "no message"))
            if time.monotonic() - t0 > timeout:
                raise RuntimeError(f"audio stalled at {self.total} of {frames} frames")
            time.sleep(0.005)
        return frames

    def slice(self, a, b):
        with self.lock:
            a, b = max(self.base, a), max(self.base, b)
            return bytes(self.buf[(a - self.base) * 2:(b - self.base) * 2])

    def drop_before(self, frame):
        """forget audio older than `frame` (absolute): the stream runs for the whole session and
        nothing re-reads a finished note, so a 90-note run with gaps would otherwise sit on ~100 MB
        of PCM by the end. Frame indices stay absolute, so `slice`/`wait` do not change."""
        with self.lock:
            n = min(max(0, int(frame) - self.base), len(self.buf) // 2)
            if n > 0:
                del self.buf[:n * 2]
                self.base += n

    def close(self):
        self.stop_flag = True
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()


class FileIn:
    """a WAV file as the capture device: it plays once, in real time, then it is silence.

    Not ffmpeg. `-re -i file -af apad | pipe` free-runs after EOF -- apad generates silence as
    fast as the reader drains it, so once the file ends ffmpeg hands over hours of padded audio in
    a fraction of a second and the frame index stops meaning wall-clock time. The next notes then
    get "captured" from silence that arrived early. Timing this clock here keeps the frame index
    honest: frames() is elapsed wall time x rate, slice() reads the file and pads with zeroes.

    It is a capture-path check (onset, trim, pitch, WAV, manifest), not a test of ffmpeg.
    """

    def __init__(self, path, sr=48000, preroll=0.25):
        self.sr, self.preroll = sr, preroll
        rate, x = sio.read_any_wav(Path(path))
        self.x = [int(round(v * 32767)) for v in sio.resample(x, rate, sr)]
        self.t0 = None

    def start(self):
        self.t0 = time.monotonic()
        return self

    def frames(self):
        return int((time.monotonic() - self.t0) * self.sr)

    def wait(self, frames, timeout=None):
        while self.frames() < frames:
            time.sleep(0.005)
        return frames

    def slice(self, a, b):
        a, b = max(0, a), max(0, b)
        out = [0] * max(0, b - a)
        for i in range(max(0, min(b, len(self.x)) - a)):
            out[i] = self.x[a + i]
        return array("h", out).tobytes()

    def close(self):
        pass


def audio_devices():
    """[(index, name)] of CoreAudio inputs, from avfoundation's own device table"""
    try:
        p = subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-f", "avfoundation",
                            "-list_devices", "true", "-i", ""], capture_output=True, timeout=20)
    except FileNotFoundError:
        return []
    out, audio, devs = p.stderr.decode("utf-8", "replace"), False, []
    for line in out.splitlines():
        if "AVFoundation audio devices" in line:
            audio = True
            continue
        if "AVFoundation video devices" in line:
            audio = False
            continue
        m = re.search(r"\[(\d+)\]\s+(.+)$", line)
        if audio and m:
            devs.append((int(m.group(1)), m.group(2).strip()))
    return devs


def audio_output_devices():
    """[(index, name)] of CoreAudio *outputs*, from ffmpeg's AudioToolbox muxer (its indices are
    its own list, not avfoundation's: resolve by name, never reuse a capture index)"""
    try:
        p = subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-re", "-f", "lavfi",
                            "-i", "anullsrc=r=48000:cl=mono", "-t", "0.05",
                            "-f", "audiotoolbox", "-list_devices", "true", "-"],
                           capture_output=True, timeout=20)
    except FileNotFoundError:
        return []
    out, on, devs = p.stderr.decode("utf-8", "replace"), False, []
    for line in out.splitlines():
        if "CoreAudio devices" in line:
            on = True
            continue
        if not on:
            continue
        m = re.search(r"\[(\d+)\]\s+(.+?),\s*(\S+)\s*$", line)
        if m:
            devs.append((int(m.group(1)), m.group(2).strip()))
    return devs


def pick(devs, want):
    """(index, name) of the first device whose name contains `want`, case-insensitive"""
    if want and want.isdigit() and any(i == int(want) for i, _ in devs):
        i = int(want)
        return i, next(n for k, n in devs if k == i)
    for i, n in devs:
        if want and want.lower() in n.lower():
            return i, n
    return None, ""


def loopback(cap_index, out_index, sr=48000, seconds=1.0):
    """play a 1 kHz tone into an output device and capture it from an input device, with no DAW
    anywhere: the only way to tell "this device passes audio" from "nothing is playing" """
    cap = FfmpegIn(cap_index, sr, preroll=0.15)
    tone = subprocess.Popen(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-re",
                             "-f", "lavfi", "-i",
                             f"sine=frequency=1000:sample_rate={sr}:duration={seconds + 0.8:.2f}",
                             "-f", "audiotoolbox", "-audio_device_index", str(out_index), "-"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        cap.start()
        cap.wait(int(sr * (seconds + 0.2)))
        x = array("h")
        x.frombytes(cap.slice(int(sr * 0.25), int(sr * 0.25) + int(sr * seconds * 0.5)))
        return max((abs(v) for v in x), default=0)
    finally:
        cap.close()
        tone.terminate()
        try:
            tone.wait(timeout=3)
        except subprocess.TimeoutExpired:
            tone.kill()


def find_device(want):
    """a device index by index number or by case-insensitive name substring; default BlackHole"""
    devs = audio_devices()
    if not devs:
        return None, devs
    if want is None:
        want = "blackhole"
    if want.isdigit():
        return (int(want), devs) if any(i == int(want) for i, _ in devs) else (None, devs)
    for i, n in devs:
        if want.lower() in n.lower():
            return i, devs
    return None, devs


# ---------------------------------------------------------------- analysis ---
def fft(re, im):
    """in-place iterative radix-2 Cooley-Tukey, len(re) a power of two (pure Python on purpose:
    this script has no dependencies, and 16 k points is 0.3 s -- a diagnostic, not a plugin)"""
    import math
    n = len(re)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    ln = 2
    while ln <= n:
        ang = -2.0 * math.pi / ln
        wl = [(math.cos(ang * k), math.sin(ang * k)) for k in range(ln // 2)]
        for i in range(0, n, ln):
            for k in range(ln // 2):
                wr, wi = wl[k]
                a, b = i + k, i + k + ln // 2
                xr, xi = re[b] * wr - im[b] * wi, re[b] * wi + im[b] * wr
                re[b], im[b] = re[a] - xr, im[a] - xi
                re[a], im[a] = re[a] + xr, im[a] + xi
        ln <<= 1


def spectrum_peaks(x, sr, n=16384, top=10, floor_db=-70.0):
    """[(hz, dBFS)] of the strongest spectral peaks of the loudest `n`-sample window"""
    import math
    n = min(n, 1 << (max(1, len(x)).bit_length() - 1))
    if n < 1024:
        return []
    step = (len(x) - n) // 8 or 1
    best, best_i = -1, 0
    for i in range(0, max(1, len(x) - n) + 1, step):
        e = sum(abs(v) for v in x[i:i + n])
        if e > best:
            best, best_i = e, i
    seg = x[best_i:best_i + n]
    re = [seg[i] / 32768.0 * (0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1))) for i in range(n)]
    im = [0.0] * n
    fft(re, im)
    mags = [(re[i] * re[i] + im[i] * im[i]) ** 0.5 for i in range(n // 2)]
    peak = max(mags) or 1e-12
    out = []
    for i in range(2, len(mags) - 1):
        if not (mags[i] > mags[i - 1] and mags[i] >= mags[i + 1]):
            continue
        a, b, c = (20.0 * math.log10(max(v, 1e-12) / peak) for v in (mags[i - 1], mags[i], mags[i + 1]))
        den = a - 2.0 * b + c
        d = 0.5 * (a - c) / den if den else 0.0
        hz = (i + d) * sr / n
        if hz < 25.0:
            continue
        dbv = b - 3.0                                       # Hann: the window loses ~6 dB of peak
        if dbv >= floor_db:
            out.append((hz, dbv))
    out.sort(key=lambda t: -t[1])
    return out[:top]


def envelope_db(x, sr, ms=50, floor=-80.0):
    """per-frame RMS in dBFS, for the ASCII strip in a probe"""
    import math
    n = max(1, int(sr * ms / 1000.0))
    out = []
    for i in range(0, len(x) - n + 1, n):
        e = (sum(v * v for v in x[i:i + n]) / n) ** 0.5 / 32768.0
        out.append(max(floor, 20.0 * math.log10(e)) if e > 0 else floor)
    return out


def strip(db_list, floor=-72.0):
    """[] of dB -> one-line ASCII bars (the shape is the diagnostic: a note decays, a metronome
    pulses, a drone is flat, nothing is a floor)"""
    ramp = " .:-=+*#@"
    out = ""
    for d in db_list:
        f = (d - floor) / (0.0 - floor)
        out += ramp[max(0, min(len(ramp) - 1, int(round(f * (len(ramp) - 1)))))]
    return out


# ---------------------------------------------------------------- pitch ---
def goertzel(x, f, sr, n, win=None):
    """|DFT| at an arbitrary frequency over the first n samples (Goertzel, one pass)"""
    import math
    k = 2.0 * math.pi * f / sr
    c = 2.0 * math.cos(k)
    s1 = s2 = 0.0
    if win is None:
        for i in range(n):
            s = x[i] + c * s1 - s2
            s2, s1 = s1, s
    else:
        for i in range(n):
            s = x[i] * win[i] + c * s1 - s2
            s2, s1 = s1, s
    return (s1 * s1 + s2 * s2 - c * s1 * s2) ** 0.5


def _cluster_raw(seg, sr, f, cts, win):
    """power centroid + power std within the cts grid, and the local maxima above the floor"""
    import math
    nn = len(seg)
    mags = [goertzel(seg, f * 2.0 ** (c / 1200.0), sr, nn, win) for c in cts]
    pw = [m * m for m in mags]
    tot = sum(pw) or 1e-30
    cent = sum(c * q for c, q in zip(cts, pw)) / tot
    spread = (sum(q * (c - cent) ** 2 for c, q in zip(cts, pw)) / tot) ** 0.5
    pk = max(mags) or 1e-12
    # the absolute level of the strongest point in the span, in dBFS: the peaks below are relative
    # to their own maximum (that is what makes "+-0 dB" the loudest lobe) and are useless for
    # "is there anything at f at all", which is the question a pitch check has to answer
    top = 20.0 * math.log10(max(pk / 32768.0, 1e-12))
    peaks = []
    for i in range(1, len(mags) - 1):
        if not (mags[i] > mags[i - 1] and mags[i] >= mags[i + 1]):
            continue
        a, b, c = (20.0 * math.log10(max(v, 1e-12) / pk) for v in (mags[i - 1], mags[i], mags[i + 1]))
        if b < -24.0:
            continue
        den = a - 2.0 * b + c
        d = 0.5 * (a - c) / den if den else 0.0
        peaks.append(((i - (len(mags) // 2) + d) * (cts[1] - cts[0]), b))
    return cent, spread, peaks, top


def cluster(seg, sr, f, span_ct=45.0, step_ct=5.0):
    """energy centroid of the partials near f -> (cents, spread ct, [(ct, dB)] peaks)

    Why a centroid and not "the strongest peak": a patch with a vibrato, a chorus or a detuned
    unison has *several* partials where a clean note has one, and which of them wins depends on
    where the analysis window lands. Peak picking then reports -7.5 ct on one take and +15.5 ct on
    the next for the same note, and neither is the pitch of the note. The energy centroid over the
    whole cluster is the perceived pitch (the mean of a detuned unison is exactly what the ear
    hears) and it does not care which partial is loudest, so takes stay comparable.

    `spread` is the power-weighted standard deviation about that centroid, with the window's own
    leakage removed: a clean note reads ~1 ct, a 3-string unison 3-6 ct, a vibrato or a chorus
    10-30 ct. The correction matters -- a 0.5 s Hann lobe is +-20 ct wide at 440 Hz (2/T Hz, and it
    only collapses when the tone lands exactly on a bin), so without it a perfectly steady bass
    note reads "20 ct spread" and the number means nothing. It is measured, not modelled: a pure
    tone at the same frequency, same length, goes through the identical grid, and the two spreads
    are combined in quadrature. That is the honest floor for this window and this pitch.
    """
    import math
    nn = len(seg)
    win = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (nn - 1)) for i in range(nn)]
    k = int(span_ct / step_ct)
    cts = [j * step_ct for j in range(-k, k + 1)]
    cent, spread, peaks, top = _cluster_raw(seg, sr, f, cts, win)
    ref = [16384.0 * math.sin(2.0 * math.pi * f * 2.0 ** (cent / 1200.0) * i / sr) for i in range(nn)]
    _c, floor, _p, _t = _cluster_raw(ref, sr, f, cts, win)
    spread = (spread * spread - floor * floor) ** 0.5 if spread > floor else 0.0
    return cent, spread, peaks, top


def check_pitch(x, sr, midi):
    """what came back, relative to the note that was asked for -> (cents, verdict, heard, detune)

    `detune` is the power-weighted spread of the partials around `cents`: 1-3 ct for a clean note,
    3-6 for a real 3-string piano unison, 10-30 for a vibrato / chorus / detuned stack. It is not an
    error -- but it is baked into the sample, and stretching a zone transposes it with the pitch.

    Three measurement choices carry this, and each one was a false verdict before it:

    *The window follows the note.* A fixed 0.2 s start and 0.5 s length is right for a violin and
    absurd for a piano's top octave, which is a 30 ms transient: C8 was measured over 95 % silence
    and reported as "no pitched content". Anchor on the loudest 10 ms block, step 30 ms into it,
    then stop where the envelope has fallen 26 dB.

    *The note is identified by its cluster, not a probe at the exact frequency.* Piano partials are
    inharmonic -- 10-30 ct sharp in the top octave -- so a Goertzel at exactly f reads -20 dB down
    and a correct take looks like a wrong note. cluster() sweeps a span wide enough to cover both
    that stretch and a detuned unison. The span is at least one FFT bin, because at 33 Hz a
    +-45 ct window is *narrower than the resolution* (126 ct) and finds nothing at all.

    *The octave question is asked the same way.* If there is no cluster at f and none at 2f either,
    the sweep over +-2 octaves says which note it actually was -- the difference between "wrong
    note" and "your track is transposed by a fifth / the arpeggiator is latched / that is the
    metronome". Autocorrelation could not answer it at all: any tone with even harmonics correlates
    just as well at T/2, which is why YIN needs its cumulative-mean normalisation.
    """
    import math
    f = 440.0 * 2.0 ** ((midi - 69) / 12.0)
    blk = max(1, int(0.01 * sr))
    blocks = [i for i in range(0, max(blk, len(x) - blk), blk)]
    if not blocks:
        return 0.0, "", None, 0.0
    loud = max(blocks, key=lambda i: max(abs(v) for v in x[i:i + blk]))
    start = min(loud + 3 * blk, max(0, len(x) - blk))
    if start > len(x) // 2:                                # a swell: the loudest block is the tail
        start = min(len(x) // 5, sr // 5)
    n_min = max(int(0.08 * sr), int(12.0 * sr / f))         # 12 periods: a bass note needs them
    # How long the note is, by energy quantile -- not by an envelope threshold. A piano bass note
    # beats: two mistuned strings cancel for 100 ms, the envelope dips 30 dB, and a "fallen below
    # -26 dB" test ends the window in the null (that is how D#1 v3 came back as "an octave below").
    # 90 % of the energy is a property of the note, not of its beating.
    sq = [sum(v * v for v in x[i:i + blk]) for i in range(start, min(len(x), start + int(0.6 * sr)), blk)]
    end = len(sq)
    if sq and sum(sq) > 0:
        tot, acc = sum(sq), 0
        for i, e in enumerate(sq):
            acc += e
            if acc >= 0.9 * tot:
                end = i + 1
                break
    n_cap = min(len(x) - start, max(int(0.5 * sr), n_min))
    n = min(max(end * blk, n_min), n_cap)
    if n < 512:
        return 0.0, "", None, 0.0
    seg = x[start:start + n]
    rms = (sum(v * v for v in seg) / max(1, len(seg))) ** 0.5 / 32768.0
    if rms < 0.002:
        return 0.0, "noise", None, 0.0                     # nothing pitched came back
    pk_db = db(max(abs(v) for v in seg) / 32768.0)
    span = max(45.0, 1.2 * (sr / float(n)) / f * 1200.0)   # at least one bin, or it finds nothing
    ct, spread, _found, top = cluster(seg, sr, f, span_ct=span)
    m1 = goertzel(seg, f, sr, n) / (rms * n * 32768.0)     # exact-frequency probe: no span needed,
    if top > pk_db - 35.0 or m1 >= 0.10:                   # which is what a bass note needs
        return ct, "", None, spread
    _c, _s, _p, half = cluster(seg, sr, f / 2.0, span_ct=span * 2.0)
    _c, _s, _p, dbl = cluster(seg, sr, f * 2.0, span_ct=span)
    if half > top + 6.0 and half > dbl:
        return 0.0, "half", midi - 12.0, 0.0
    if dbl > top + 6.0:
        return 0.0, "twice", midi + 12.0, 0.0
    # not the note: find the one it is. Two passes: 1-semitone grid over +-2 octaves, then a
    # 12.5 ct refinement around the winner
    nn = min(max(int(sr * 0.25), int(12.0 * sr / f)), len(x) - start, sr)
    if nn < 1024:
        return 0.0, "other", None, 0.0
    seg = x[start:start + nn]
    best, best_off = -1.0, 0
    for k in range(-24, 25):
        if k == 0:
            continue
        m = goertzel(seg, f * 2.0 ** (k / 12.0), sr, nn)
        if m > best:
            best, best_off = m, k
    c, width, _pk, _top = cluster(seg, sr, f * 2.0 ** (best_off / 12.0))
    return c, "other", midi + best_off + c / 100.0, width


def onset_strength(x, sr, frame_ms=5, win_ms=20):
    """half-wave-rectified rise of the log envelope -> ([strength per frame], hop in samples)

    Not the envelope itself: a bowed or blown note sustains at a near-constant level for its whole
    hold, so "where is the energy" has a *plateau* for an answer and any peak-picker on it lands
    wherever the patch happens to swell (that is exactly how a comb over mean energy picked a lag
    2.6 s into the string notes). The rise is sharp only at an attack.

    Two details are load-bearing. The log is taken over 20 ms -- five cycles of a G2 -- because a
    window shorter than the waveform's own period makes a mean absolute value oscillate with the
    phase. And the window is built from consecutive hop-sized blocks and then averaged, so the
    cost is O(n) for a 20-minute render instead of a sum per frame (207 M adds before).
    """
    hop = max(1, int(sr * frame_ms / 1000))
    nblk = max(1, int(round(win_ms / frame_ms)))
    blocks = [sum(abs(v) for v in x[i:i + hop]) / hop for i in range(0, len(x) - hop, hop)]
    env = []
    for k in range(nblk - 1, len(blocks)):
        env.append(sum(blocks[k - nblk + 1:k + 1]) / nblk)   # window ENDS at block k: causal
    if not env:
        return [], hop
    le = [math.log(v + 20.0) for v in env]                # +20 LSB: silence is a floor, not -inf
    return [0.0] + [max(0.0, le[i] - le[i - 1]) for i in range(1, len(le))], hop


def envelope_onsets(x, sr, rel=0.35, min_gap=0.25):
    """attack positions, in samples, from the onset-strength function -> [int]

    Only a tempo hint, for a render that does not line up: at `rel` of the strongest rise the
    tremolo and vibrato of a real patch stay under the threshold and the attacks do not, so the
    gaps between these are the note period -- or a multiple of it, where a note is silent.
    """
    oss, hop = onset_strength(x, sr)
    if not oss:
        return []
    thr = rel * (max(oss) or 1.0)
    lat = int(sr * 20 / 1000)                            # the window's own latency
    out, last = [], -10 ** 9
    for i in range(1, len(oss) - 1):
        if oss[i] >= thr and oss[i] >= oss[i - 1] and oss[i] >= oss[i + 1] \
                and (i - last) * hop > min_gap * sr:
            out.append(max(0, i * hop - lat))
            last = i
    return out


def comb_offset(x, sr, period_s, win_ms=20):
    """(lag in samples, score 0..1) that puts the note period on the attacks

    One note's attack is a weak anchor: if the patch does not reach the plan's lowest root, the
    first note is silent and "the first thing that makes a sound" is the *second* note -- every cut
    then lands a whole period early, which looks like a broken export. A comb over every note in
    the file cannot be fooled that way: the lag is scored by the mean onset strength at all N
    positions t + k*period, so the grid is found from the majority even when a few notes are silent.
    """
    oss, hop = onset_strength(x, sr)
    if not oss:
        return 0, 0.0
    per = period_s * sr / hop
    if per < 1:
        return 0, 0.0
    peak = max(oss) or 1.0
    best, best_t = -1.0, 0
    for t in range(int(min(per, len(oss)))):
        acc, k = 0.0, 0
        while True:
            j = int(t + k * per)
            if j >= len(oss):
                break
            acc += oss[j]
            k += 1
        if k:
            acc /= k
            if acc > best * (1.0 + 1e-9):                 # earliest wins a tie: the true lag, not +period
                best, best_t = acc, t
    # The window reports a note only once it is *inside* it, so the strength peaks one window after
    # the start: pull the lag back. Coarse on purpose -- `locate` re-derives the origin from the
    # measured starts, and this only has to be close enough not to wrap onto the next note. The
    # wrap is the subtle part: the search range is one period, so an onset 15 ms *before* the tick
    # comes back as period-15 and, clamped at zero, would read +7965 -- a whole note late, with
    # every cut landing on the next note. Fold it into +-half a period first.
    per = period_s * sr
    lag = best_t * hop - int(sr * win_ms / 1000)
    lag = (lag + per / 2.0) % per - per / 2.0
    return int(max(0.0, lag)), min(1.0, best / peak)


def find_start(x, sr, lo, hi, thr=0.01, frame_ms=20):
    """the physical start of the event in [lo, hi): its peak frame, walked back to `thr` of that

    `thr` is what makes this usable on a slow attack. `find_attack`'s 12 % is right for a live
    take, where the window may begin inside the previous note's release and the walk-back has to
    stop early; a rendered note sits in silence, and on a bowed swell 12 % of the peak is 30-70 ms
    *after* the bow started -- cutting there shaves the swell. At 1 % the walk-back reaches the
    first audible sample whatever the envelope's shape.
    """
    step = max(1, int(sr * frame_ms / 1000))
    hi = min(len(x), hi)
    lo = max(0, min(lo, hi - step))
    env = [sum(abs(v) for v in x[i:i + step]) / step for i in range(lo, hi - step, step)]
    if not env:
        return lo
    pk = max(env)
    if pk <= 0:
        return lo
    j = env.index(pk)
    while j > 0 and env[j - 1] > thr * pk:
        j -= 1
    o = lo + j * step
    if j > 0:                                             # then the frame boundary -> the sample
        t = thr * pk
        for k in range(max(0, lo + (j - 1) * step), o):
            if abs(x[k]) > t:
                return k
    return o


def find_attack(x, sr, lo, hi):
    """index of the attack of the loudest event in [lo, hi), walked back to where it starts

    Not "the first sample above 2% of the peak": by the second note the previous note's release
    tail is still above that threshold, and the cut would land on the wrong note. The new note's
    attack is the loudest thing in the window (the old one is decaying and every note of a layer is
    played at the same velocity), so: mean-absolute envelope in 1 ms frames, the peak frame, then
    walk back to the last frame below 12% of it.
    """
    step = max(1, sr // 1000)
    hi = min(len(x), hi)
    lo = max(0, min(lo, hi - step))
    env = [sum(abs(v) for v in x[i:i + step]) / step for i in range(lo, hi - step, step)]
    if not env:
        return lo
    pk = max(env)
    if pk <= 0:
        return lo
    j = env.index(pk)
    while j > 0 and env[j - 1] > 0.12 * pk:
        j -= 1
    o = lo + j * step
    # the frame walk gives the millisecond; the first sample above 3% of that frame's mean level is
    # the actual start -- a cut on the frame boundary can be 1 ms late, which on a piano is the
    # hammer's own transient (the thing a band-limited ADPCM copy can least afford to lose)
    if j > 0:
        thr = 0.03 * pk
        for k in range(lo + (j - 1) * step, o):
            if abs(x[k]) > thr:
                return k
    return o


# ---------------------------------------------------------------- session ---
class Plan:
    """the note list of a session: roots x velocity layers, in play order (low to high, soft to loud)"""

    def __init__(self, roots, vels, passes=1):
        self.roots, self.vels, self.passes = roots, vels, max(1, passes)

    def items(self):
        """(note, velocity, layer, pass). Consecutive takes of the same note are adjacent: an
        ensemble is built from several performances of one note, and they belong in one sitting."""
        return [(r, v, li + 1, pi + 1)
                for li, v in enumerate(self.vels) for r in self.roots for pi in range(self.passes)]


def take_facts(x, sr, note):
    """everything measured about one take, and what it means -- the warnings are the point

    Shared by the live session and by `render`, so a set matches whether it was recorded through
    BlackHole or sliced out of a flattened export. `octave` non-empty is a rejected take: the
    caller writes it as _BAD rather than into the bank.
    """
    pk = max((abs(v) for v in x), default=0)
    clipped = sum(1 for v in x if abs(v) >= 32700)
    cents, octave, heard, detune = check_pitch(x, sr, note)
    warn = []
    if clipped:
        warn.append(f"CLIPPED {clipped} samples (-inf dBFS): lower the instrument or the master")
    if abs(cents) > 12:
        warn.append(f"tuned {cents:+.0f} ct")
    if detune > 25.0:
        # an ensemble legitimately spreads 8-25 ct, so this is not a per-note warning; only
        # something this wide (a deep chorus, a doubled detuned layer) is worth stopping for
        warn.append(f"partials spread over {detune:.0f} ct: a vibrato/chorus/doubled detune in "
                    f"the patch, not a unison -- it is baked in and transposes with the pitch")
    if octave == "half":
        warn.append("played an octave BELOW the note asked for (key range or transpose)")
    elif octave == "twice":
        warn.append("played an octave ABOVE the note asked for (key range or transpose)")
    elif octave == "other" and heard is not None:
        st = heard - note
        warn.append(f"heard {note_name(int(round(heard)))} ({st:+.1f} st): another note is "
                    f"sounding -- track not on this MIDI port, a transpose, a latched "
                    f"arpeggiator, or the metronome")
    elif octave == "other":
        warn.append("not this note and not identifiable (broadband: clicks, noise, a metronome)")
    elif octave == "noise":
        warn.append("no pitched content: reverb/delay tail only, or the wrong track")
    return dict(peak=pk, clipped=clipped, cents=cents, octave=octave, heard=heard, detune=detune,
                dc=sum(x) / max(1, len(x)), warnings=warn)


def take_name(setname, idx, note, layer, take, npas):
    return (f"{idx:02d}_{setname}_{note_name(note)}_v{layer}"
            + (f"_p{take}" if npas > 1 else "") + ".wav")


def save_take(out, filename, note, vel, layer, take, x, sr, facts):
    """write one take under its name, and return its manifest entry

    A take that is not the note asked for is kept as evidence, under a name no builder's glob
    picks up and a rerun never skips: a wrong-pitch zone is worse than a missing one, and silently
    keeping it is how a bank gets poisoned. A take that passes deletes the rejected one it
    replaces -- that file was evidence about a failed attempt, not a record of the set.
    """
    name = filename[:-4] + "_BAD.wav" if facts["octave"] else filename
    write_wav(out / name, x, sr)
    if not facts["octave"]:
        stale = out / (filename[:-4] + "_BAD.wav")
        if stale.exists():
            stale.unlink()
    return dict(file=name, midi=note, name=note_name(note), velocity=vel, layer=layer, take=take,
                rate=sr, frames=len(x), seconds=round(len(x) / sr, 4),
                peak=round(facts["peak"] / 32768, 5), peak_dbfs=round(db(facts["peak"] / 32768), 2),
                dc=round(facts["dc"] / 32768, 5), cents=round(facts["cents"], 1),
                detune_ct=round(facts["detune"], 1), octave=facts["octave"],
                heard=(note_name(int(round(facts["heard"]))) if facts["heard"] is not None else ""),
                clipped=facts["clipped"], warnings=facts["warnings"]), name


class Runner:
    def __init__(self, cfg, midi, audio, outdir, log=print):
        self.c, self.midi, self.audio, self.out, self.log = cfg, midi, audio, outdir, log
        self.entries, self.clipped, self.failed = [], 0, 0

    def sleep(self, s):
        time.sleep(s)

    def capture(self, note, vel):
        """one note: mark, play, wait -> (samples, the note-on's index inside them)"""
        c = self.c
        sr = self.audio.sr
        mark = self.audio.frames()
        self.midi.note_on(c.channel, note, vel)
        try:
            self.sleep(c.hold)
        finally:
            self.midi.note_off(c.channel, note)
        self.sleep(c.tail)
        end = self.audio.wait(mark + int((c.hold + c.tail + 0.20) * sr))
        a = max(0, mark - int(c.preroll * sr))
        raw = self.audio.slice(a, end)
        x = array("h")
        x.frombytes(raw[:len(raw) & ~1])
        if sys.byteorder != "little":
            x.byteswap()
        drop = getattr(self.audio, "drop_before", None)
        if drop:
            drop(a)                       # this note is on disk in `x`; the stream is not a tape
        return x, mark - a                # the first note of a session has no lead-in to give

    def cut(self, x, on_pos):
        """trim to the attack (4 ms of margin so the transient is not shaved), cap the length"""
        sr = self.audio.sr
        lo = max(0, on_pos - int(0.05 * sr))            # the note cannot start before this
        o = find_attack(x, sr, lo, lo + int(0.65 * sr))
        o = max(0, o - int(0.004 * sr))
        cap = int((self.c.hold + self.c.tail + 0.10) * sr)
        return x[o:o + cap]

    def manifest(self):
        """after every note, not at the end: Ctrl-C must not lose the record of 30 minutes"""
        man = dict(set=self.c.set, generated=time.strftime("%Y-%m-%dT%H:%M:%S"),
                   capture_rate=self.audio.sr, c4_is_60=True,
                   hold=self.c.hold, tail=self.c.tail, preroll=self.c.preroll,
                   velocities=self.c.vels, roots=self.c.roots,
                   passes=getattr(self.c, "passes", 1), entries=self.entries)
        (self.out / "session.json").write_text(json.dumps(man, indent=1) + "\n")

    def run(self):
        c, plan = self.c, Plan(self.c.roots, self.c.vels, getattr(self.c, "passes", 1))
        items = plan.items()
        n = len(items)
        gap = max(0.0, float(getattr(c, "gap", 0.0)))
        only = getattr(c, "only", None)
        # the file index comes from the *full* plan, so recording one note with --only keeps its
        # name (and therefore overwrites it) instead of writing it as note 1 of a new set
        todo = [(k, it) for k, it in enumerate(items) if only is None or it[0] in only]
        self.log(f"{c.set}: {len(c.roots)} roots x {len(c.vels)} velocity layers = {n} notes, "
                 f"{c.hold:.2f}s hold + {c.tail:.2f}s tail + {gap:.1f}s gap -> {self.out}"
                 + (f"  [only {' '.join(note_name(r) for r in sorted(only))}]" if only else ""))
        t0 = time.monotonic()
        for pos, (k, (note, vel, layer, take)) in enumerate(todo):
            name = (f"{k:02d}_{c.set}_{note_name(note)}_v{layer}"
                    + (f"_p{take}" if plan.passes > 1 else "") + ".wav")
            path = self.out / name
            if c.skip_existing and path.exists():
                self.log(f"[{k + 1:3d}/{n}] {note_name(note):4s} v{layer}  keep (exists)")
                continue
            tag = (f"[{k + 1:3d}/{n}] {note_name(note):4s} v{layer} ({vel:3d})"
                   + (f" take {take}" if plan.passes > 1 else ""))
            if c.dry_run:
                self.log(f"{tag}  would write {name}")
                continue
            x, on_pos = self.capture(note, vel)
            x = self.cut(x, on_pos)
            pk = max((abs(v) for v in x), default=0)
            if not x or pk < 200:                            # -44 dBFS: silence, not a quiet note
                self.failed += 1
                self.log(f"{tag}  SILENT: nothing came back. Is the track armed, the monitor on, the "
                         f"instrument audible and BlackHole the device?")
                if self.failed >= 3:
                    raise SystemExit("3 silent notes in a row: stopping (the routing is wrong, not the notes)")
                continue
            facts = take_facts(x, self.audio.sr, note)
            warn, clipped, bad = facts["warnings"], facts["clipped"], bool(facts["octave"])
            entry, name = save_take(self.out, name, note, vel, layer, take, x, self.audio.sr, facts)
            self.entries.append(entry)
            self.manifest()
            warn = []
            if clipped:
                self.clipped += 1
                if self.clipped >= 3:
                    raise SystemExit("3 notes in a row are clipped: lower the track/master and rerun "
                                     "(the files already written are fine, the rest will be skipped)")
            self.log(f"{tag}  {len(x) / self.audio.sr:5.2f}s  peak {db(pk / 32768):6.1f} dBFS"
                     f"  {'  '.join(warn) if warn else 'ok'}"
                     + ("  [written as _BAD, not used]" if bad else ""))
            if gap and pos + 1 < len(todo):
                self.sleep(gap)           # the note-off tail plus this: the room (and the DAW's
                                          # voices) must be empty before the next attack, or the
                                          # onset detector sees the previous release as the start
        spread = sorted(e.get("detune_ct", 0.0) for e in self.entries if not e.get("octave"))
        if spread:
            med = spread[len(spread) // 2]
            what = ("steady" if med < 3 else "a real unison (3 strings or 2 detuned oscillators)"
                    if med < 8 else "an ensemble stack, or a vibrato/chorus in the patch")
            tail = ("" if med < 3 else " -- it is baked into the samples: a stretched zone transposes "
                                        "it with the pitch, a looped one freezes it")
            self.log(f"partials: median spread {med:.1f} ct, {what}{tail}")
        self.log(f"done in {time.monotonic() - t0:.1f}s: {len(self.entries)} written, "
                 f"{self.clipped} clipped, {self.failed} silent")


# ---------------------------------------------------------------- selftest ---
class SynthIn:
    """a fake 'DAW': an additive decaying tone that answers note events on a virtual frame clock.

    It exists so the whole pipeline (mark -> capture -> onset -> trim -> pitch -> WAV -> manifest)
    is exercised without a DAW, a BlackHole install, or any real time passing.
    """

    LAT = 0.020          # fake DAW latency, so a preroll bug shows up
    REL = 0.15

    def __init__(self, sr=48000):
        self.sr, self.buf, self.total = sr, bytearray(), 0
        self.voices = []           # (note, vel, on_frame, off_frame or None)
        self.phase = {}

    def frames(self):
        return self.total

    def wait(self, frames, timeout=None):
        self._render(max(0, frames - self.total))
        return frames

    def slice(self, a, b):
        return bytes(self.buf[a * 2:b * 2])

    def note_on(self, ch, n, v):
        self.voices.append([n, v, self.total + int(self.LAT * self.sr), None])

    def note_off(self, ch, n):
        for v in self.voices:
            if v[0] == n and v[3] is None:
                v[3] = self.total + int(self.LAT * self.sr)
                return

    def _render(self, frames):
        import math
        sr = self.sr
        out = array("h")
        for f in range(frames):
            t = self.total + f
            s = 0.0
            for note, vel, on, off in self.voices:
                if t < on:
                    continue
                life = (t - on) / sr
                amp = (vel / 127.0) ** 1.6 * 0.45
                env = amp * math.exp(-life / 1.2) * min(1.0, (t - on) / (0.004 * sr))
                if off is not None and t > off:
                    env *= max(0.0, 1.0 - (t - off) / (self.REL * sr))
                if env <= 0:
                    continue
                hz = 440.0 * 2.0 ** ((note - 69) / 12.0)
                ph = (t - on) * hz / sr
                s += env * (math.sin(2 * math.pi * ph) + 0.45 * math.sin(4 * math.pi * ph)
                            + 0.20 * math.sin(6 * math.pi * ph))
            out.append(max(-32768, min(32767, int(s * 20000))))
        self.total += frames
        self.buf += out.tobytes()

    def close(self):
        pass


class NullMidi:
    """no port, no notes: `--input FILE` samples a file, the file's timeline is the session's"""

    def note_on(self, ch, n, v):
        pass

    def note_off(self, ch, n):
        pass

    def cc(self, ch, cc, v):
        pass

    def panic(self, chans=range(4)):
        pass

    def close(self):
        pass


class SynthMidi:
    """the MIDI side of the selftest: events go to the synth instead of a port"""

    def __init__(self, synth):
        self.s, self.events = synth, []

    def note_on(self, ch, n, v):
        self.events.append(("on", n, v, self.s.frames()))
        self.s.note_on(ch, n, v)

    def note_off(self, ch, n):
        self.events.append(("off", n, 0, self.s.frames()))
        self.s.note_off(ch, n)

    def cc(self, ch, cc, v):
        pass

    def panic(self, chans=range(4)):
        pass

    def close(self):
        pass


def cluster_check(sr=48000):
    """does the partial cluster tell a clean note from a vibrato from a detuned unison?

    This is the measurement that decides whether a captured zone is steady. It has to be right:
    the tuning figure printed for every take comes out of it, and a patch that is *both* detuned
    and vibrato'd has no single "strongest peak" to report.
    """
    import math
    f, n = 440.0, int(sr * 0.40)
    clean = [32767 * 0.5 * math.sin(2 * math.pi * f * i / sr) for i in range(n)]
    # 5 Hz vibrato, +-15 ct peak deviation: phase modulation (beta = 0.76), sidebands at +-5 Hz
    beta = 2 * math.pi * f * (2 ** (15 / 1200.0) - 1) / 5.0
    vib = [32767 * 0.5 * math.sin(2 * math.pi * f * i / sr + beta * math.sin(2 * math.pi * 5.0 * i / sr))
           for i in range(n)]
    # two unisons 6 Hz apart: what a 3-string piano note or a doubled patch looks like, and its
    # pitch is the mean of the pair (443 Hz = +11.8 ct), not either string
    det = [32767 * 0.35 * (math.sin(2 * math.pi * f * i / sr) + math.sin(2 * math.pi * (f + 6.0) * i / sr))
           for i in range(n)]
    cases = [("clean", clean, 1, 3.0, 0.0), ("vibrato", vib, 2, 60.0, 0.0),
             ("detuned unison", det, 2, 27.0, 11.8)]
    ok = True
    for label, sig, want_n, want_w, want_ct in cases:
        ct, width, pk, _top = cluster(sig, sr, f)
        good = len(pk) >= want_n and width <= want_w and abs(ct - want_ct) < 6.0
        print(f"  cluster {label:14s} {len(pk)} peaks, spread {width:5.1f} ct, "
              f"centroid {ct:+5.1f} ct {'ok' if good else 'FAIL'}")
        ok = ok and good
    return ok


def selftest(args):
    import tempfile
    ok = True
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "SELFTEST"
        out.mkdir(parents=True)
        synth = SynthIn()
        cfg = argparse.Namespace(set="SELFTEST", roots=[48, 60, 72], vels=[40, 118], hold=0.30,
                                 tail=0.25, preroll=0.20, channel=0, skip_existing=False,
                                 dry_run=False, passes=1)
        r = Runner(cfg, SynthMidi(synth), synth, out, log=lambda *a, **k: None)
        orig_sleep = time.sleep
        time.sleep = lambda s: synth.wait(synth.frames() + int(s * synth.sr))   # no real time
        try:
            r.run()
        finally:
            time.sleep = orig_sleep
        ok = cluster_check(sr=48000) and ok
        files = sorted(p.name for p in out.glob("*.wav"))
        man = json.loads((out / "session.json").read_text()) if (out / "session.json").exists() else {}
        print(f"  {len(files)} files, {len(man.get('entries', []))} manifest entries")
        plan = [(n, li) for li in (1, 2) for n in (48, 60, 72)]      # the same order Runner.run uses
        want = sorted(f"{k:02d}_SELFTEST_{note_name(n)}_v{li}.wav" for k, (n, li) in enumerate(plan))
        if files != want:
            print("  FAIL names:", files, "wanted", want)
            ok = False
        for e in man.get("entries", []):
            if e["octave"] or abs(e["cents"]) > 25 or e["peak"] < 0.02:
                print("  FAIL", e["file"], e)
                ok = False
            if not 0.25 < e["seconds"] < 0.75:
                print("  FAIL length", e["file"], e["seconds"])
                ok = False
        sr, x = sio.read_any_wav(out / files[0]) if files else (0, [])
        if not files or len(x) < 1000:
            print("  FAIL: the WAV is empty or unreadable")
            ok = False
        ok = mid_check(td) and ok
        ok = render_check(td) and ok
        ok = pack_check(out) and ok
        print("  selftest:", "ok" if ok else "FAILED")
    return 0 if ok else 1


def read_smf(path):
    """a type-0 SMF -> (division, [(tick, status, args)]). Just enough to check what was written:
    the bytes are the one part of this tool that only a DAW would otherwise verify."""
    d = Path(path).read_bytes()
    if d[:4] != b"MThd":
        return 0, []
    div = struct.unpack_from(">H", d, 12)[0]
    i, end, t, status, out = 14, 0, 0, 0, []
    if d[14:18] != b"MTrk":
        return div, []
    end = 22 + struct.unpack_from(">I", d, 18)[0]
    i = 22
    while i < end:
        dt = 0
        while True:                                   # variable-length delta
            b = d[i]
            i += 1
            dt = (dt << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        t += dt
        b = d[i]
        if b & 0x80:                                  # status byte (no running status from us, but
            status = b                                # a DAW may re-save the file with it)
            i += 1
        if status == 0xFF:                            # meta: length, then data
            mt = d[i]
            i += 1
            n = 0
            while True:
                c = d[i]
                i += 1
                n = (n << 7) | (c & 0x7F)
                if not c & 0x80:
                    break
            i += n
            if mt == 0x2F:
                break
            continue
        n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
        out.append((t, status, bytes(d[i:i + n])))
        i += n
    return div, out


def mid_check(td):
    """the .mid bytes: every note at its tick, its velocity, its gate -- parsed back"""
    d = Path(td) / "MIDSET"
    d.mkdir(parents=True, exist_ok=True)
    cfg = argparse.Namespace(set="MIDSET", roots=[48, 60], vels=[40, 120], hold=2.0, tail=1.0,
                             gap=3.0, channel=0, passes=1)
    period, gate, notes, plan = write_mid(cfg, d / "MIDSET.mid", d / "plan.json", log=None)
    div, ev = read_smf(d / "MIDSET.mid")
    ons = [(t, e[0], e[1]) for t, st, e in ev if st & 0xF0 == 0x90 and e[1]]
    offs = [(t, e[0]) for t, st, e in ev if st & 0xF0 == 0x80]
    want = [(n["tick"], n["midi"], n["velocity"]) for n in plan["notes"]]
    ok = div == PPQ and ons == want and offs == [(t + gate, n) for t, n, _v in want]
    ok = ok and plan["ticks_per_note"] % PPQ == 0 and gate % (PPQ // 4) == 0
    print(f"  mid         {len(ons)} on / {len(offs)} off at {beats_of(period):.0f} beats, "
          f"gate {beats_of(gate):.2f}, division {div}  {'ok' if ok else 'FAIL'}")
    if not ok:
        print("    wanted", want[:3], "got", ons[:3])
    return ok


def render_check(td):
    """MIDI file -> a fake flatten -> slices: the whole DAW path, without a DAW

    The fake render is what a freeze/flatten produces if everything went right *except* that the
    clip did not start on the tick grid and the song was not at 120 BPM: notes on the plan's ticks
    at 105 BPM, the file starting 30 ms late, each note a decaying tone at its own pitch. Both
    errors must be found and corrected, not assumed away -- that is the whole reason `render`
    measures instead of trusting the plan.
    """
    import math
    d = Path(td) / "RNDR"
    d.mkdir(parents=True, exist_ok=True)
    cfg = argparse.Namespace(set="RNDR", roots=[57, 69, 81], vels=[60, 110], hold=1.5, tail=1.0,
                             gap=2.0, channel=0, passes=1)
    period, gate, _notes, plan = write_mid(cfg, d / "RNDR.mid", d / "plan.json", log=None)
    sr, bpm, off_s = 48000, 105.0, 0.030
    tps = plan["ppq"] * bpm / 60.0
    per = plan["ticks_per_note"] / tps
    hold = plan["gate_ticks"] / tps
    n = int((off_s + len(plan["notes"]) * per + plan["tail_seconds"] + 0.5) * sr)
    x = [0] * n
    for nd in plan["notes"]:
        f = 440.0 * 2.0 ** ((nd["midi"] - 69) / 12.0)
        amp = 0.08 + 0.30 * nd["velocity"] / 127.0
        s0 = int((off_s + nd["tick"] / tps) * sr)
        for i in range(int((hold + plan["tail_seconds"]) * sr)):
            if s0 + i >= n:
                break
            rel = 1.0 if i < hold * sr else math.exp(-(i - hold * sr) / sr * 9.0)
            x[s0 + i] += int(amp * 20000 * math.sin(2 * math.pi * f * i / sr)
                             * math.exp(-(i / sr) * 0.6) * rel)
    write_wav(d / "RNDR_out.wav", x, sr)
    out = Path(td) / "RNDR_slices"
    rc = cmd_render(argparse.Namespace(set="RNDR", input=str(d / "RNDR_out.wav"),
                                      plan=str(d / "plan.json"), dir=None, bpm=None,
                                      offset=None, only=None, out=str(out), dry_run=False,
                                      force=False))
    files = sorted(p.name for p in out.glob("*.wav"))
    want = sorted(take_name("RNDR", nd["i"], nd["midi"], nd["layer"], nd["take"], 1)
                  for nd in plan["notes"])
    man = json.loads((out / "session.json").read_text()) if (out / "session.json").exists() else {}
    ok = rc == 0 and files == want
    bad = [e for e in man.get("entries", []) if e["octave"] or abs(e["cents"]) > 20]
    ok = ok and not bad and abs(man.get("bpm", 0) - bpm) < 0.5
    ok = ok and abs(man.get("render_offset", 0) - off_s) < 0.01
    print(f"  render      {len(files)}/{len(want)} files, bpm {man.get('bpm', 0):.1f} (want {bpm:.0f}), "
          f"offset {man.get('render_offset', 0) * 1000:+.0f} ms (want +{off_s * 1000:.0f})  "
          f"{'ok' if ok else 'FAIL'}")
    if bad:
        print("  render      FAIL", bad[:2])
    return ok


def pack_check(session_dir):
    """the recorded session -> slot image path: the header must parse back, and not lie

    A slot image is validated by the firmware with the same rules sampleio writes, so this reads
    the header back the way `eng_sample.c smp_user_scan()` does (magic, version, zone count, every
    zone inside data_len, ls <= le < n) instead of trusting the writer.
    """
    import struct
    prefix = str(session_dir / "SLOT")
    args = argparse.Namespace(dir=str(session_dir), set="SELFTEST", name="SLTEST", layer=None,
                              velocity=100, roots=None, seconds=None, min_seconds=0.10,
                              budget=None, zone=12.0, slot=1, out=prefix)
    import contextlib                          # the report is the printer, so capture it
    import io
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_pack(args)
    lines = [ln for ln in buf.getvalue().splitlines() if ln.strip()]
    hdr = Path(prefix + ".hdr").read_bytes()
    data = Path(prefix + ".bin").read_bytes()
    magic, ver, nz = struct.unpack_from("<IHB", hdr, 0)
    data_len, crc = struct.unpack_from("<II", hdr, 16)
    ok = True
    if rc or magic != 0x504D5346 or ver != 1 or not 1 <= nz <= 16:
        print(f"  pack        FAIL header (rc {rc}, magic {magic:#x}, v{ver}, {nz} zones)")
        ok = False
    elif data_len != len(data):
        print(f"  pack        FAIL data_len {data_len} != {len(data)}")
        ok = False
    else:
        for i in range(nz):
            off, n, ls, le = struct.unpack_from("<4I", hdr, 32 + i * 28)
            rate = struct.unpack_from("<I", hdr, 32 + i * 28 + 16)[0]
            if off + (n + 1) // 2 > data_len or ls > le or le >= n or not rate:
                print(f"  pack        FAIL zone {i}: off {off} n {n} ls {ls} le {le}")
                ok = False
    print(f"  pack        {nz} zones, {len(data)} B, {len(lines)} lines of report  "
          f"{'ok' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- MIDI file ---
# The render path: write the whole session as one MIDI file, let the DAW's own freeze/flatten do
# the playing (it has the patch, the MIDI map, the velocity curve and exact plugin delay
# compensation), then slice the single exported WAV back into the same file names. Recording
# through BlackHole in real time costs a minute per note and cannot be trusted to a lunch break;
# a bounce is offline, exact, and repeatable.
PPQ = 480                                    # ticks per quarter note (Live, Logic, Cubase all read it)
GRID_TOL = 0.05                              # a rendered note that is 50 ms off its tick is not this render
PRE_ROLL = 0.008                             # cut this early: the grid is 5 ms-accurate, the swell is not
TEMPO = 500000                               # 120 BPM, the tempo the .mid asks for


def midi_grid(hold, tail, gap):
    """(note period, gate) in ticks -- the period rounded UP to a whole beat at 120 BPM

    Whole beats, not seconds: the note starts land on the DAW's own grid, so a note can be found
    by eye in the arrangement, the export's clip boundaries are musical, and the slicer's expected
    positions are exact integers rather than rounded seconds. The cost is up to one beat of extra
    silence, which is free.
    """
    tps = PPQ * 1e6 / TEMPO                  # ticks per second at 120 BPM
    period = math.ceil((hold + tail + gap) * tps / PPQ) * PPQ
    return period, int(round(hold * tps))


def _varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def smf(events, out, name="", division=PPQ, tempo=TEMPO):
    """type 0 SMF from [(tick, bytes)] -> OUT, with the tempo and a track name at tick 0"""
    body = bytearray(b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big"))
    if name:
        nm = name.encode("ascii", "replace")[:48]
        body += b"\x00\xff\x03" + _varlen(len(nm)) + nm
    prev = 0
    for tk, data in sorted(events, key=lambda e: e[0]):
        tk = max(tk, prev)                   # a zero-length note must not produce a negative delta
        body += _varlen(tk - prev) + bytes(data)
        prev = tk
    body += b"\x00\xff\x2f\x00"
    with open(out, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, division)
                + b"MTrk" + struct.pack(">I", len(body)) + body)


def write_mid(cfg, out, plan_path=None, log=print):
    """the session as one MIDI file (+ the plan `render` needs) -> (period, gate, notes)"""
    period, gate = midi_grid(cfg.hold, cfg.tail, cfg.gap)
    items = Plan(cfg.roots, cfg.vels, getattr(cfg, "passes", 1)).items()
    events, notes = [], []
    for k, (note, vel, layer, take) in enumerate(items):
        tick = k * period
        events.append((tick, bytes([0x90 | cfg.channel, note, vel])))
        events.append((tick + gate, bytes([0x80 | cfg.channel, note, 64])))
        notes.append(dict(i=k, midi=note, name=note_name(note), velocity=vel, layer=layer,
                          take=take, tick=tick))
    end = len(items) * period
    events.append((end, bytes([0xB0 | cfg.channel, 123, 0])))        # all notes off
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    smf(events, out, name=cfg.set)
    plan = dict(set=cfg.set, ppq=PPQ, bpm=60_000_000.0 / TEMPO, ticks_per_note=period,
                gate_ticks=gate, hold_seconds=cfg.hold, tail_seconds=cfg.tail,
                gap_seconds=cfg.gap, c4_is_60=True, channel=cfg.channel,
                passes=getattr(cfg, "passes", 1), notes=notes)
    if plan_path:
        Path(plan_path).parent.mkdir(parents=True, exist_ok=True)
        Path(plan_path).write_text(json.dumps(plan, indent=1) + "\n")
    if log:
        log(midi_recipe(out, plan))
    return period, gate, notes, plan


def beats_of(ticks, ppq=PPQ):
    return ticks / float(ppq)


def midi_recipe(mid_path, plan):
    """what to do in the DAW, in the order that does not lose the render

    Written out because every step has a cheap mistake that costs the whole bounce: the master
    limiter (Live's default) squashes the ff layer, Normalize on export ruins every relative
    velocity, and a warp mode on the flattened clip resamples what was just rendered.
    """
    secs = plan["ticks_per_note"] / (plan["ppq"] * plan["bpm"] / 60.0)
    mins = len(plan["notes"]) * secs / 60.0
    return f"""\
  {mid_path}
    {len(plan['notes'])} notes, one every {beats_of(plan['ticks_per_note']):.0f} beats
    ({secs:.1f} s at {plan['bpm']:.0f} BPM), {beats_of(plan['gate_ticks']):.2f} beats held,
    {beats_of((len(plan['notes']) + 1) * plan['ticks_per_note'], plan['ppq']):.0f} beats long
    ({beats_of((len(plan['notes']) + 1) * plan['ticks_per_note'], plan['ppq']) / 4:.0f} bars of 4/4),
    about {mins:.0f} min of audio. Put it on an empty MIDI track at 1.1.1.

  1. the tempo does not have to be 120: every position is in ticks, and `render --bpm` converts
     with whatever tempo you actually used. Keep it constant for the whole bounce.
  2. instrument track: the patch only, dry -- no reverb, no delay, no chorus, no compressor.
  3. MASTER: remove the Limiter (Live inserts one by default) and any master FX. A limiter turns
     the ff layer into the same waveform as the mf layer.
  4. Freeze the track, then right-click the frozen clip -> Flatten. Now it is real audio.
  5. File -> Export Audio/Video: WAV, 48 kHz, 24-bit (or 32-bit float), Normalize OFF,
     "Render as loop" OFF. Export the whole song from 1.1.1.
  6. `autosample.py render {plan['set']} THAT.wav --bpm {plan['bpm']:.0f}`"""


# ---------------------------------------------------------------- commands ---
def cmd_devices(args):
    devs = audio_devices()
    if not devs:
        print("no CoreAudio input devices (is ffmpeg installed? `brew install ffmpeg`)")
        return 1
    for i, n in devs:
        print(f"  [{i}] {n}" + ("   <- BlackHole" if "blackhole" in n.lower() else ""))
    return 0


def cmd_doctor(args):
    """tell the truth about each of the four links, and fail only on a broken install.

    A silent BlackHole is not a failure: with no DAW playing there is nothing in it. So when the
    capture is silent the doctor proves the capture path a second way -- a tone played into the
    device and read back out of it -- and reports which of the two situations it is.
    """
    import shutil
    bad, warn = 0, 0
    print("ffmpeg      :", shutil.which("ffmpeg") or "MISSING (brew install ffmpeg)")
    if not shutil.which("ffmpeg"):
        bad += 1

    devs = audio_devices()
    i, capname = pick(devs, args.device or "blackhole")
    if i is None:
        print(f"audio in    : no device matching {args.device or 'blackhole'!r}")
        print("              seen:", ", ".join(f"[{k}] {n}" for k, n in devs) or "none")
        print("              -> brew install blackhole-2ch, then route the DAW's master into it")
        bad += 1
    else:
        print(f"audio in    : [{i}] {capname}")

    if args.midi_out:
        try:
            m = MidiOut("StudioKEYS Autosampler (doctor)", dest=args.midi_out)
            m.panic()
            m.close()
            print(f"MIDI out    : ok -> {args.midi_out} (sends to a port that already exists)")
        except Exception as e:
            print("MIDI out    :", e)
            bad += 1
    else:
        names = [n for _, n in MidiOut.sources()]
        n = names.count("StudioKEYS Autosampler")
        if n == 1:
            print("MIDI source : present, held by another process (that port is what a DAW sees)")
        elif n > 1:
            print(f"MIDI source : {n} ports named 'StudioKEYS Autosampler' -- a DAW will list all of")
            print("              them. Stop every `autosample.py port`/`session` but one.")
            warn += 1
        else:
            try:
                m = MidiOut("StudioKEYS Autosampler")
                print("MIDI source : creatable (created and closed just now, so a DAW cannot see it:")
                print("              the endpoint exists only while the owning process runs)")
                m.panic()
                m.close()
            except Exception as e:
                print("MIDI source :", e)
                bad += 1

    if i is not None and not args.no_capture:
        print(f"capture     : {args.rate / 1000:.1f} kHz, 1.0 s ...", end="", flush=True)
        a = FfmpegIn(i, args.rate, preroll=0.2)
        try:
            t0 = time.monotonic()
            a.start()
            a.wait(int(args.rate * 1.0))
            x = array("h")
            x.frombytes(a.slice(0, int(args.rate * 1.0)))
            pk = max((abs(v) for v in x), default=0)
            print(f" {time.monotonic() - t0:.2f}s, peak {db(pk / 32768):.1f} dBFS")
        except Exception as e:
            print(" FAILED:", e)
            print("              the device could not be opened: another app has it exclusively, or")
            print("              the microphone permission was refused (System Settings -> Privacy)")
            pk = 0
            bad += 1
        finally:
            a.close()
        if bad == 0:
            if pk >= 200:
                print("              signal present: something is already routed into this device")
            else:
                if args.no_loopback:
                    print("              silent (--no-loopback: not testing the device itself). That is")
                    print("              expected with no DAW playing, but it also hides a dead route.")
                    warn += 1
                    out = None
                else:
                    print("              silent. That is expected with no session running, so testing")
                    print("              the device itself: a 1 kHz tone into it and back out ...",
                          end="", flush=True)
                    out = pick(audio_output_devices(), capname)
                if not args.no_loopback and (not out or out[0] is None):
                    print(" cannot (no output device of that name)")
                    print("              -> set it up and rerun, or accept the wiring below by hand")
                    warn += 1
                elif out and out[0] is not None:
                    try:
                        lp = loopback(i, out[0], args.rate)
                    except Exception as e:
                        print(f" failed: {e}")
                        warn += 1
                    else:
                        print(f" {db(lp / 32768):.1f} dBFS")
                        if lp >= 200:
                            print("              the capture path works. A silent session then means the")
                            print("              DAW's master is not reaching this device, or the DAW is")
                            print("              not playing -- not a capture problem.")
                        else:
                            print("              the device opens but passes no audio: a Multi-Output")
                            print("              device with a mismatched sample rate, or the DAW holds")
                            print("              the device exclusively")
                            bad += 1
    print("\nwiring:")
    print("  1. autosample.py port  (leave it running, enable the port in the DAW, then Ctrl-C)")
    print("  2. DAW track: instrument <- MIDI input 'StudioKEYS Autosampler'")
    print("  3. DAW master -> Multi-Output Device (speakers + BlackHole 2ch), or BlackHole as an extra output")
    print("  4. instrument dry: no reverb / delay / compressor / limiter, no master normalize")
    print("  5. autosample.py session MYSET --roots 'C2 G2 D3 A3 E4 B4' --vel 40 84 118")
    if bad:
        print(f"\n{bad} broken, {warn} warning(s).")
        return 1
    if warn and args.strict:
        print(f"\n{warn} warning(s), --strict makes that a failure.")
        return 1
    print(f"\nall four links are in order" + (f" ({warn} warning(s))" if warn else "") + ".")
    return 0


def cmd_probe(args):
    """record N seconds, then say what is in the audio: peak, envelope shape, spectral peaks.

    This is the command that answers "what is BlackHole actually carrying", which no amount of
    wiring advice can. With --note it also sends one note, so the MIDI path and the audio path are
    exercised in the same take and the answer is unambiguous.
    """
    devs = audio_devices()
    i, name = pick(devs, args.device or "blackhole")
    if i is None:
        print(f"no audio device matching {args.device or 'blackhole'!r}; "
              f"seen: " + (", ".join(f"[{k}] {n}" for k, n in devs) or "none"))
        return 1
    midi = None
    if args.note:
        try:
            midi = MidiOut(args.midi_name, dest=args.midi_out)
        except Exception as e:
            print(e)
            return 1
    print(f"probing [{i}] {name} for {args.seconds:.1f}s at {args.rate / 1000:.1f} kHz"
          + (f", sending {args.note} v{args.vel}" if args.note else ""))
    cap = FfmpegIn(i, args.rate, preroll=0.3)
    try:
        cap.start()
        mark = cap.frames()
        if midi:
            midi.note_on(args.channel, note_from_name(args.note), args.vel)
        time.sleep(args.seconds)
        if midi:
            midi.note_off(args.channel, note_from_name(args.note))
        cap.wait(mark + int(args.seconds * args.rate))          # the span we intend to keep
        raw = cap.slice(max(0, mark - int(0.3 * args.rate)), mark + int(args.seconds * args.rate))
    finally:
        if midi:
            midi.panic()
            midi.close()
        cap.close()
    x = array("h")
    x.frombytes(raw[:len(raw) & ~1])
    if sys.byteorder != "little":
        x.byteswap()
    if not x:
        print("nothing captured")
        return 1
    out = Path(args.out) if args.out else Path("/tmp/autosample-probe.wav")
    write_wav(out, x, args.rate)
    pk = max(abs(v) for v in x)
    rms = (sum(v * v for v in x) / len(x)) ** 0.5
    print(f"\nwrote {out} ({len(x) / args.rate:.2f}s, {len(x) * 2 / 1024:.0f} KiB)")
    print(f"peak {db(pk / 32768):6.1f} dBFS   rms {db(rms / 32768):6.1f} dBFS")
    env = envelope_db(x, args.rate)
    print("\nenvelope (50 ms/frame, -72..0 dBFS):")
    print("  |" + strip(env) + "|")
    print(f"  {len(env) * 0.05:.1f}s, floor frames {sum(1 for d in env if d <= -71.0)}/{len(env)}")
    peaks = spectrum_peaks(x, args.rate)
    print("\nstrongest partials:")
    if not peaks:
        print("   none above -70 dB (silence, or noise with no tonal peak)")
    for hz, dv in peaks:
        m = sio.hz_to_midi(hz)
        nm = note_name(int(round(m)))
        ct = (m - round(m)) * 100.0
        print(f"   {hz:8.2f} Hz  {dv:6.1f} dB   {nm:4s} {ct:+5.1f} ct")
    if args.note:
        f = 440.0 * 2.0 ** ((note_from_name(args.note) - 69) / 12.0)
        start = min(len(x) // 5, args.rate // 5)
        n = min(len(x) - start, max(int(args.rate * 0.50), int(12.0 * args.rate / f)))
        ct, spread, pr = cluster(x[start:start + n], args.rate, f)
        if not pr:
            print("\nfundamental: nothing above the noise floor near this note -- no cluster to measure")
            cents, verdict, heard, detune = check_pitch(x, args.rate, note_from_name(args.note))
        else:
            print(f"\nfundamental: centroid {ct:+5.1f} ct, spread {spread:4.1f} ct, "
                  f"{len(pr)} peak{'s' if len(pr) != 1 else ''} within +-45 ct")
            if spread < 3.0:
                print("  a steady note (the spread is the analysis floor, not the patch)")
            elif spread < 8.0:
                print("  a unison: 3 strings or 2 detuned oscillators, a few cents apart -- as recorded")
            else:
                beat = f * (2.0 ** (spread / 1200.0) - 1.0)
                print(f"  an ensemble, a vibrato or a chorus: partials spread ~{spread:.0f} ct (~{beat:.1f} Hz).")
                print("  that spread is baked into the sample: a stretched zone transposes it with the")
                print("  pitch, a looped zone freezes it. Fine for an ensemble patch, wrong for a solo.")
            cents, verdict, heard, detune = check_pitch(x, args.rate, note_from_name(args.note))
        said = {"": "the note asked for",
                "half": "an octave BELOW the note asked for",
                "twice": "an octave ABOVE the note asked for",
                "noise": "nothing pitched",
                "other": ("another note entirely" +
                          (f" ({note_name(int(round(heard)))}" if heard is not None else ""))}.get(verdict, verdict)
        print(f"\nthe audio is {said}" + (f", {cents:+.0f} ct" if not verdict else "") +
              (")" if verdict == "other" and heard is not None else ""))
    return 0


def cmd_ports(args):
    midi_ports()
    return 0


def cmd_port(args):
    """hold the virtual source open so a DAW can see and enable it"""
    try:
        m = MidiOut(args.name, dest=args.midi_out)
    except Exception as e:
        print(e)
        return 1
    mine = [n for _, n in MidiOut.sources()]
    print(f"port '{args.name}': open" + (" (system sources: " + ", ".join(mine) + ")" if mine else ""),
          flush=True)
    print("keep this running. In the DAW:")
    print("  Live     : Settings -> Link, Tempo & MIDI -> MIDI Ports -> 'StudioKEYS Autosampler' Track = On.")
    print("             If it is not listed yet, close and reopen that Settings window (Live caches")
    print("             the port table while the window is open).")
    print("  Logic    : File -> Project Settings -> MIDI -> Inputs, tick the port.")
    print("  Reaper   : Preferences -> MIDI Devices -> enable it for input.")
    print("  Cubase   : Studio -> Studio Setup -> MIDI Ports, tick 'All MIDI Inputs'.")
    print("Then Ctrl-C here. `session` creates the same port itself, so enable it once and it stays")
    print("enabled (DAWs remember input ports by name).")
    print("If the DAW still does not list it: quit the DAW, start this again, start the DAW (a port")
    print("created after an application's MIDI scan is not always picked up), or switch to IAC:")
    print("  Audio MIDI Setup -> Window -> MIDI Studio -> IAC Driver -> Device is online,")
    print("  then: autosample.py session NAME --midi-out 'IAC Driver Bus 1'", flush=True)
    t0 = time.monotonic()
    beat = 0.0
    try:
        while True:
            time.sleep(0.05)
            el = time.monotonic() - t0
            if args.seconds and el > args.seconds:
                break
            if el - beat >= 10.0:
                beat = el
                print(f"  ... alive {el:.0f}s", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        m.panic()
        time.sleep(0.05)
        m.close()
    print("port closed")
    return 0


DEFAULT_VELS, DEFAULT_HOLD, DEFAULT_TAIL = "40 84 118", 3.0, 1.2
ADPCM_BPS = sio.SLOT_RATE / 2.0                      # 11,025 B/s: 4-bit mono at 22.05 kHz
SLOT_BUDGET = sio.SLOT_MAX_DATA                      # 36,352 B in a 36 KiB user slot
FLASH_BUDGET = 88_860                                # SPACE_BUDGET.md, app slot free today


def resolve_plan(args):
    """explicit flag > --preset > default -> (roots, vels, hold, tail), and the preset itself

    A preset is not a magic mode: every field it fills can be overridden on the same command line,
    and it fills nothing the user typed. `horn+brass` merges roots (the seam gets recorded once) and
    takes the timing from the first.
    """
    name = (getattr(args, "preset", None) or "").strip().lower()
    p = {}
    if name:
        for part in name.split("+"):
            part = part.strip()
            if part not in PRESETS:
                raise SystemExit(f"unknown preset '{part}': {', '.join(sorted(PRESETS))}")
            p = p or PRESETS[part]
    if getattr(args, "roots", None):
        roots = [note_from_name(s) for s in args.roots.split()]
    elif name:
        roots = preset_notes(name)
    else:
        roots = list(range(args.root_lo, args.root_hi + 1, args.root_step))
    vels = args.vel if args.vel is not None else p.get("vels", (40, 84, 118))
    vels = [int(v) for v in (vels.split() if isinstance(vels, str) else vels)]
    hold = args.hold if args.hold is not None else p.get("hold", DEFAULT_HOLD)
    tail = args.tail if args.tail is not None else p.get("tail", DEFAULT_TAIL)
    return sorted(set(r for r in roots if 0 <= r < 128)), \
        sorted(set(max(1, min(127, v)) for v in vels)), float(hold), float(tail), p


FREED_BY_CUTS = 62_844                         # FLUTE + SAX only (SLICE's BREAK went in 1.0.6; see size_report.py C1)


def plan_budget(roots, vels, hold, tail, gap=0.0, log=print):
    """what this session will cost, and what of it can ever be played -- before recording 90 notes

    Three numbers, all exact, because the build is a fixed function of length: IMA ADPCM is 4
    bits/sample at 22.05 kHz (11,025 B/s), fp so a zone costs its seconds x 11,025 B. The point of
    printing them before the session is that a 3-layer 30-root piano is 15 minutes of recording and
    then does not fit anything, and it is better to know that first.

    "built" is not the recorded length: a sustaining zone becomes attack + loop (~1 s) and a
    decaying one gets its envelope divided out (~1 s at the same length), so the flash figure uses
    1 s per zone -- the length the shipped sets actually use (STRINGS and HORN: 1.65 s stored, 4 zones each).
    """
    secs = hold + tail
    n = len(roots) * len(vels)
    raw = secs * ADPCM_BPS * n
    built = 1.0 * ADPCM_BPS * n
    log(f"  record: {secs:.1f}s per note = {secs * ADPCM_BPS / 1024:.1f} KiB on disk; "
        f"{len(roots)} roots x {len(vels)} layers = {n} notes, {raw / 1024 / 1024:.1f} MiB, "
        f"~{n * (secs + gap) / 60 + 0.3:.0f} min"
        + (f" ({gap:.1f}s of silence after each note)" if gap else ""))
    log(f"  flash : {n} zones at ~1 s each = {built / 1024:.0f} KiB  |  "
        f"free today {FLASH_BUDGET / 1024:.0f} KiB, +cuts (FLUTE, SAX) "
        f"{(FLASH_BUDGET + FREED_BY_CUTS) / 1024:.0f} KiB, one user slot {SLOT_BUDGET / 1024:.0f} KiB"
        + ("   << fits" if built <= FLASH_BUDGET + FREED_BY_CUTS else "   << over: fewer roots/layers"))
    if len(vels) > 1:
        log("  note: SAMPLE picks a zone by key only (smp_zone_t has no velocity), so today the "
            "v2/v3 takes are a bank for a velocity-aware build, not layers it can play")
    return raw


def cmd_presets(args):
    for k, v in PRESETS.items():
        r = v["roots"]
        print(f"{k:8s} {len(r):2d} roots x {len(v['vels'])} layers, {v['hold']:.1f}s hold, "
              f"{len(r) * len(v['vels']):3d} notes, max stretch +-{v['zone']} st  "
              f"{note_name(r[0])}..{note_name(r[-1])}")
        print(f"         {v['desc']}")
    print("\n`session NAME --preset piano` fills roots, velocities, hold and tail; every flag still "
          "overrides it. `--preset horn+brass` merges two ranges (the seam recorded once).")
    return 0


def _ord(n):
    return f"{n}{'th' if 10 <= n % 100 <= 20 else {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th')}"


def _fade(x, n, out=True):
    """raised cosine on the last (or first) n samples -- a truncation that is not a click"""
    import math
    k = len(x)
    n = min(n, k)
    for i in range(n):
        g = 0.5 + 0.5 * math.cos(math.pi * (i + 1) / n)
        if out:
            x[k - n + i] *= g
        else:
            x[i] *= g
    return x


def cmd_pack(args):
    """a recorded session -> one user slot image (or a dry run that says it cannot fit)

    The slot is 36 KiB and the engine picks zones by key only, so this is where the musical
    decisions get made for real: how many roots survive, how long each one is allowed to be, and
    what stretch the gaps between them cost. Recording is generous; this is the part that has to
    fit, and it says so in cents and semitones rather than in bytes.
    """
    d = Path(args.dir) if args.dir else OWN / args.set.upper()
    man = json.loads((d / "session.json").read_text())
    good = [e for e in man["entries"] if not e.get("octave") and not e["file"].endswith("_BAD.wav")]
    if not good:
        print(f"{d}: no usable takes (session.json has {len(man['entries'])} entries)")
        return 1
    layers = sorted({e["layer"] for e in good})
    if args.layer is not None:
        layer = args.layer
    else:                                       # the layer closest to mf: a down-scaled ff is the
        want = args.velocity                 # least wrong single layer, an up-scaled p is hiss
        layer = min(layers, key=lambda li: abs(
            sum(e["velocity"] for e in good if e["layer"] == li) /
            max(1, sum(1 for e in good if e["layer"] == li)) - want))
    takes = {}
    for e in good:
        if e["layer"] == layer:
            takes.setdefault(e["midi"], e)      # first take per note; _p2/_p3 are a future bank
    if not takes:
        print(f"{d}: no take in layer v{layer}")
        return 1
    roots = sorted(takes)
    if args.roots:
        want = {note_from_name(t) for t in args.roots.split()}
        roots = [r for r in roots if r in want]
        if not roots:
            print("--roots matched nothing that was recorded")
            return 1
    budget = args.budget or SLOT_BUDGET
    cap = args.seconds or 1e9
    budget -= 16 * 2                            # zone table slack: never plan to the last byte

    # Most roots that still leave each zone long enough to be a note. Coverage first: dropping
    # every other root doubles the length a zone can use and costs one grid step of stretch on
    # either side, so it is only done when the budget cannot give `min-seconds` any other way.
    best = None
    for scale in range(1, len(roots) + 1):
        sel = roots[::scale][:sio.SLOT_ZONES]
        sec = min(cap, budget / (len(sel) * ADPCM_BPS))
        if sec >= args.min_seconds:
            best = (scale, sel, sec)
            break
    if best is None:
        need = args.min_seconds * ADPCM_BPS * min(len(roots), sio.SLOT_ZONES)
        print(f"cannot fit: even {min(len(roots), sio.SLOT_ZONES)} zones x {args.min_seconds:.2f}s "
              f"needs {need:.0f} B, the slot has {budget} B. Record shorter notes "
              f"(--hold/--tail) or accept fewer roots.")
        return 1
    scale, sel, sec = best
    print(f"{d.name}: {len(takes)} recorded roots, layer v{layer}, {len(sel)} zones x {sec:.2f}s "
          f"-> {int(sec * ADPCM_BPS) * len(sel) / 1024:.1f} KiB of {budget / 1024:.1f}"
          + (f" ({len(roots) - len(sel)} roots dropped, every {_ord(scale)} kept)" if scale > 1 else ""))

    zones, report = [], []
    for r in sel:
        e = takes[r]
        sr, x = sio.read_any_wav(d / e["file"])
        x = sio.resample(x, sr, sio.SLOT_RATE)[:int(sec * sio.SLOT_RATE)]
        if len(x) < 64 or not max(abs(v) for v in x) > 1e-6:
            print(f"  {note_name(r)}: take is too short or silent, dropped")
            continue
        _fade(x, max(1, int(min(0.04, len(x) * 0.25) * sio.SLOT_RATE)))
        ins = sio.to_int16(x)
        zones.append((ins, r, None, None))
        report.append((r, len(ins) / sio.SLOT_RATE, e.get("detune_ct", 0.0)))
    if not zones:
        print("nothing usable to pack")
        return 1
    hdr, data = sio.user_slot(args.name.upper(), zones)
    sel_roots = sorted(z[1] for z in zones)
    split = sio.key_split(sel_roots)

    # the engine really does play the outer zones down to note 0 and up to 127; that is not a
    # stretch anybody chose, so report the distance a *player* can reach (half the gap to the
    # neighbouring root) and mark the two ends instead of printing +-48 st and a useless warning
    print(f"\n  {'zone':4s} {'plays':13s} {'sec':5s} stretch  spread")
    worst = 0.0
    for j, ((r, dur, dt), (lo, hi)) in enumerate(zip(report, split)):
        # the ends have no neighbour to halve the gap with: the engine covers 0..127 there, but
        # nobody chose that, so the number stays the stretch inside the sampled range and the
        # row is marked instead of claiming +-48 st
        if j == 0:
            lo = r
        if j == len(split) - 1:
            hi = r
        st = max(r - lo, hi - r)
        worst = max(worst, st)
        end = "*" if j in (0, len(split) - 1) else " "
        print(f"  {note_name(r):4s} {note_name(lo)}..{note_name(hi)}{end:9s} {dur:5.2f}  "
              f"+-{st} st   {dt:.1f} ct")
    if len(split) > 1:
        print("  * the outer zones also answer every key outside that range, stretched further: "
              "the number above is the stretch inside the sampled range")
    print(f"\n  {len(data)} B ADPCM in a {sio.SLOT_SIZE}-B slot, {len(hdr)} B header, "
          f"{len(data) / 1024:.1f} / {SLOT_BUDGET / 1024:.1f} KiB used "
          f"({sum(r[1] for r in report):.1f} s of audio in all)")
    print("  a user slot holds 3.3 s of audio at 22.05 kHz, so this is a sketch that plays rather "
          "than an instrument: the real one goes into the image via gen_samples.py, funded by the "
          "FLUTE+SAX cuts (64 KiB)")
    lim = args.zone
    if worst > lim:
        print(f"  WORST STRETCH +-{worst} st is past what this instrument tolerates (+-{lim}): a "
              f"stretched zone transposes its vibrato/spread with the pitch. Record more roots, "
              f"shorten the zones (--seconds), or accept it lower down the keyboard.")
    if report and max(r[2] for r in report) > 8.0:
        print(f"  the widest zone holds a patch with {max(r[2] for r in report):.0f} ct of partial "
              f"spread (an ensemble or a chorus): it will stretch that by 2^(+-{worst}/12)")
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out + ".hdr").write_bytes(hdr)
        Path(args.out + ".bin").write_bytes(data)
        print(f"\n  wrote {args.out}.hdr + .bin")
        print(f"  upload: python3 tools/fm1_sample_upload.py upload {args.slot} {args.out}")
        print(f"  (device connected over USB-MIDI; `fm1_sample_upload.py erase {args.slot}` first "
              f"only if the slot is full or invalid)")
    else:
        print("\n  (dry run: --out PREFIX writes .hdr/.bin for fm1_sample_upload.py)")
    return 0


def cmd_miditest(args):
    """no DAW, no BlackHole: create a destination, send to it, read it back"""
    return midi_loopback()


def cmd_session(args):
    roots, vels, hold, tail, preset = resolve_plan(args)
    only = ({note_from_name(t) for t in args.only.split()} if args.only else None)
    cfg = argparse.Namespace(set=args.name.upper(), roots=roots, vels=vels, hold=hold,
                             tail=tail, only=only,
                             preroll=args.preroll, channel=args.channel, passes=args.passes,
                             skip_existing=not args.overwrite, dry_run=args.dry_run,
                             gap=args.gap)
    out = Path(args.out) if args.out else OWN / args.name.upper()
    if not cfg.dry_run:
        out.mkdir(parents=True, exist_ok=True)
    if preset.get("dry"):
        print(f"  {args.preset}: {preset['dry']}")
    plan_budget(roots, vels, hold, tail, args.gap)
    if len(roots) > 24 or len(vels) > 5:
        print(f"{len(roots)} roots x {len(vels)} layers: that is a long session. "
              f"Ctrl-C is safe (every note is written as it is recorded, rerun skips them).")
    if args.mid:
        write_mid(cfg, args.mid, Path(args.mid).with_suffix(".json"), log=None)
    if cfg.dry_run:
        midi, audio = SynthMidi(SynthIn()), SynthIn()
    else:
        i, devs = (0, []) if getattr(args, "input", None) else find_device(args.device)
        if i is None:
            print("no audio device; `autosample.py devices` lists them, --device NAME picks one, "
                  "or --input FILE to sample a file")
            return 1
        try:
            midi = NullMidi() if args.input else MidiOut(args.midi_name, dest=args.midi_out)
        except Exception as e:
            print(e)
            print("`autosample.py ports` lists what exists. For a DAW that will not show a virtual "
                  "port, enable the IAC Driver and use --midi-out 'IAC Driver Bus 1'.")
            return 1
        audio = FileIn(args.input, args.rate, args.preroll) if args.input else \
            FfmpegIn(i, args.rate, args.preroll, args.verbose)
        audio.start()
    r = Runner(cfg, midi, audio, out)
    try:
        r.run()
    except KeyboardInterrupt:
        print("\ninterrupted: every recorded note is on disk, rerun to continue")
    finally:
        try:
            midi.panic()
            time.sleep(0.05)
        finally:
            midi.close()
            audio.close()
    return 0


def cmd_mid(args):
    roots, vels, hold, tail, _p = resolve_plan(args)
    cfg = argparse.Namespace(set=args.name.upper(), roots=roots, vels=vels, hold=hold, tail=tail,
                             gap=args.gap, channel=args.channel, passes=args.passes)
    plan_path = Path(args.plan) if args.plan else \
        (Path(args.dir) if args.dir else OWN / cfg.set) / "plan.json"
    write_mid(cfg, Path(args.out), plan_path)
    print(f"  plan: {plan_path}   (what `render` reads: the grid, the notes, the gates)")
    return 0


def cmd_render(args):
    """a flattened export -> the set's WAVs, by grid *and* measured attack

    The DAW is trusted for *when* -- it played a MIDI file whose every position is an integer tick,
    with its own plugin delay compensation -- and not for where the file starts: a flattened clip
    can begin a few ms (or a whole bar) away from the tick 0 the plan assumes. So the lag is
    measured twice over: a comb over all N notes (immune to a silent lowest root), then each
    attack inside a +-20 % period window around its expected position.

    Two passes, because the failure mode of a wrong `--bpm` is that every cut is plausible and
    every file is garbage: pass A locates all the notes and validates the grid, pass B writes. A
    render that does not line up is refused before it can poison the set.
    """
    plan_path = Path(args.plan) if args.plan else \
        (Path(args.dir) if args.dir else OWN / args.set.upper()) / "plan.json"
    if not plan_path.exists():
        print(f"no plan at {plan_path}: `autosample.py mid SET OUT.mid` writes it beside the file")
        return 1
    plan = json.loads(plan_path.read_text())
    setname = plan.get("set") or args.set.upper()
    sr, x = sio.read_mono_i16(Path(args.input))         # a whole song: streamed, as raw shorts
    ppq = plan["ppq"]
    bpm = args.bpm or plan["bpm"]
    tps = ppq * bpm / 60.0                                    # ticks per second at THIS tempo
    period_s = plan["ticks_per_note"] / tps
    hold_s = plan["gate_ticks"] / tps
    cap = int((hold_s + plan["tail_seconds"] + 0.10) * sr)

    notes = plan["notes"]
    if args.only:
        want = {note_from_name(t) for t in args.only.split()}
        notes = [n for n in notes if n["midi"] in want]
        if not notes:
            print("--only matched nothing in the plan")
            return 1

    def locate(off, tps):
        """pass A for one candidate grid -> (spots, missing, bad), nothing written

        The search window is 100 ms before and 250 ms after the tick, *not* +-tol: a check whose
        window is its own tolerance can never fail, and every silent-free file would validate
        itself. Here the measured start is free to disagree with the tick, and `bad` is real
        evidence that this audio is not that plan's render.
        """
        spots, missing, bad = [], [], []
        pre, post = int(0.10 * sr), int(0.25 * sr)
        for n in notes:
            exp = off + int(round(n["tick"] / tps * sr))
            st = find_start(x, sr, max(0, exp - pre), min(len(x), exp + post))
            # 500 ms, not 50: a bowed or blown attack can still be at -60 dBFS after 50 ms, and
            # "silent" must mean silence, not a slow swell
            seg_pk = max((abs(v) for v in x[st:st + int(0.5 * sr)]), default=0)
            spots.append((n, exp, st, seg_pk))
            if seg_pk < 200:
                missing.append(n)
            elif abs(st - exp) > int(GRID_TOL * sr):
                bad.append((n, st - exp))
        return spots, missing, bad

    def guess_bpm():
        """the tempo this render was actually made at, from the spacing of its attacks

        The plan is a fixed number of ticks per note, so one measured period gives the tempo
        outright: bpm = ticks_per_note / period / ppq x 60. The shortest gap between attacks is
        used (a missed note doubles the gap, it never halves it), and the candidate is only
        believed if every note then lands on the grid -- the validation, not the estimate, is
        what makes this safe.
        """
        on = envelope_onsets(x, sr)
        gaps = [b - a for a, b in zip(on, on[1:]) if b > a]
        if not gaps:
            return None, 0
        per = min(gaps) / sr
        if per <= 0:
            return None, 0
        return 60.0 * plan["ticks_per_note"] / (ppq * per), len(on)

    if args.offset is not None:
        off = int(args.offset * sr)
    else:
        off, score = comb_offset(x, sr, period_s)
        print(f"  grid found at {off / sr * 1000:+.0f} ms (comb score {score:.2f})")
    print(f"{Path(args.input).name}: {len(x) / sr:.1f}s at {sr} Hz, plan {plan_path.name}: "
          f"{len(notes)} notes every {period_s:.2f}s at {bpm:.0f} BPM, {hold_s:.2f}s held")

    def fit_tempo(spots):
        """least-squares tempo and origin from the attacks -> (bpm, off) or (None, None)

        The envelope's onset spacing is a coarse guess (5 ms frames, and a missed note doubles
        it); the attacks themselves are measured, so a line through (tick, attack) is exact --
        the slope *is* samples per tick, hence the tempo, and the intercept is where the file
        starts. Without this a 0.1 % tempo error accumulates across 90 notes into a visible drift
        and a biased origin.
        """
        pts = [(n["tick"], o) for n, exp, o, pk in spots if pk >= 200]
        if len(pts) < 2 or args.offset is not None:
            return None, None
        mx = sum(a for a, _ in pts) / float(len(pts))
        my = sum(b for _, b in pts) / float(len(pts))
        den = sum((a - mx) ** 2 for a, _ in pts)
        if den <= 0:
            return None, None
        m = sum((a - mx) * (b - my) for a, b in pts) / den
        if m <= 0:
            return None, None
        return 60.0 * (sr / m) / ppq, int(round(my - m * mx))

    def adopt(bpm_new):
        """re-locate on a corrected grid; the caller only takes it if every note lands"""
        tpsp = ppq * bpm_new / 60.0
        offp, _s = comb_offset(x, sr, plan["ticks_per_note"] / tpsp)
        sp, mi, bg = locate(offp, tpsp)
        return tpsp, offp, sp, mi, bg

    # ---- pass A: locate every note, and prove the file is this render before writing a byte
    spots, missing, bad_grid = locate(off, tps)
    if (missing or bad_grid) and args.bpm is None and args.offset is None:
        est, seen_n = guess_bpm()
        if est and abs(est - bpm) / bpm > 0.01:
            tps2, off2, s2, m2, b2 = adopt(est)
            if not m2 and not b2:
                print(f"  the attacks are {plan['ticks_per_note'] / tps2:.2f}s apart, not "
                      f"{period_s:.2f}s: this render was made at about {est:.1f} BPM")
                bpm, tps, spots, missing, bad_grid = est, tps2, s2, m2, b2
                off = off2
    # the measured attacks give the tempo exactly; adopt it when it differs from what was assumed
    if not bad_grid and not missing and args.bpm is None:
        fit_bpm, fit_off = fit_tempo(spots)
        # 0.05 %, not 0.5: a 0.1 % tempo error is invisible in one note and ~2 periods across a
        # 90-note session, and it biases the reported origin by that drift
        if fit_bpm and abs(fit_bpm - bpm) / bpm > 0.0005:
            tps2, off2, s2, m2, b2 = adopt(fit_bpm)
            if not m2 and not b2:
                print(f"  the attacks fit {fit_bpm:.2f} BPM, not {bpm:.2f}: adopting it "
                      f"({plan['ticks_per_note'] / tps2:.2f}s per note)")
                bpm, tps, spots, missing, bad_grid = fit_bpm, tps2, s2, m2, b2
                off = off2
    if bpm != (args.bpm or plan["bpm"]):
        hold_s = plan["gate_ticks"] / tps
        cap = int((hold_s + plan["tail_seconds"] + 0.10) * sr)
    # a note with no attack is normal -- the patch's key range or velocity floor -- and the set
    # simply lacks that zone. Notes *off the grid* are not: that means the file is not this
    # render, and writing it would put the wrong audio under every name.
    if len(missing) > max(1, len(notes) // 5) or bad_grid or len(missing) == len(notes):
        print(f"\n  this file is not that plan's render, so nothing was written:")
        if missing:
            print(f"    {len(missing)} of {len(notes)} notes have no attack near where the tick "
                  f"says ({', '.join(m['name'] for m in missing[:6])}"
                  f"{' ...' if len(missing) > 6 else ''})")
        if bad_grid:
            w = max(abs(d) for _, d in bad_grid)
            print(f"    {len(bad_grid)} notes are off the grid by up to {w / sr * 1000:.0f} ms")
        est, seen_n = guess_bpm()
        if est:
            print(f"    the attacks in this file are {60.0 * plan['ticks_per_note'] / (ppq * est):.2f}s "
                  f"apart ({seen_n} of {len(plan['notes'])} found), i.e. {est:.1f} BPM")
        print("    likely: --bpm does not match the render, the export is not the whole song from "
              "1.1.1, the track was not flattened, or the patch's key range skips the lowest roots")
        print(f"    the plan wants {len(plan['notes'])} notes from {off / sr:.2f}s to "
              f"{(off + len(plan['notes']) * period_s * sr) / sr:.2f}s; --force writes anyway")
        if not args.force:
            return 1

    # the comb finds the lag at 5 ms frames; the measured starts make it exact. The median is the
    # file's own origin (export offset + the plugin's latency, which is a property of the render,
    # not of the notes) and the *spread* around it is the honest quality number: it says whether
    # all N notes agree with one grid, which is what makes a cut at tick - 8 ms safe.
    spread_ms = 0.0
    if spots and not bad_grid and args.offset is None:
        ds = [o - int(round(n["tick"] / tps * sr)) for n, exp, o, pk in spots if pk >= 200]
        if ds:
            off = sorted(ds)[len(ds) // 2]
            spread_ms = max(abs(d - off) for d in ds) / sr * 1000.0
            spots = [(n, off + int(round(n["tick"] / tps * sr)), o, pk) for n, exp, o, pk in spots]
    if missing:
        print(f"  {len(missing)} notes have no attack (the patch does not play them: key range, "
              f"velocity floor or a muted layer) -- the set will lack those zones: "
              f"{', '.join(m['name'] for m in missing[:8])}"
              f"{' ...' if len(missing) > 8 else ''}")
    if args.dry_run:
        for n, exp, o, pk in spots:
            print(f"  [{n['i'] + 1:3d}/{len(plan['notes'])}] {n['name']:4s} v{n['layer']} "
                  f"tick {n['tick']:7d} -> cut {max(0, exp - int(PRE_ROLL * sr)) / sr:8.3f}s -> "
                  f"{take_name(setname, n['i'], n['midi'], n['layer'], n['take'], plan.get('passes', 1))}")
        return 0

    # ---- pass B: cut
    out = Path(args.out) if args.out else OWN / setname
    out.mkdir(parents=True, exist_ok=True)
    npas = plan.get("passes", 1)
    entries, resid, silent = [], [], 0
    t0 = time.monotonic()
    for n, exp, o, _pk in spots:
        cut = max(0, exp - int(PRE_ROLL * sr))
        seg = x[cut:cut + cap]
        pk = max((abs(v) for v in seg), default=0)
        tag = f"[{n['i'] + 1:3d}/{len(plan['notes'])}] {n['name']:4s} v{n['layer']} ({n['velocity']:3d})"
        if not seg or pk < 200:
            silent += 1
            print(f"{tag}  SILENT at {o / sr:.2f}s")
            continue
        facts = take_facts(seg, sr, n["midi"])
        entry, name = save_take(out, take_name(setname, n["i"], n["midi"], n["layer"], n["take"], npas),
                                n["midi"], n["velocity"], n["layer"], n["take"], seg, sr, facts)
        entries.append(entry)
        print(f"{tag}  {len(seg) / sr:5.2f}s at {cut / sr:8.3f}s  peak "
              f"{db(facts['peak'] / 32768):6.1f} dBFS  "
              f"{'  '.join(facts['warnings']) if facts['warnings'] else 'ok'}"
              + ("  [written as _BAD, not used]" if facts["octave"] else ""))
    man = dict(set=setname, mode="render", generated=time.strftime("%Y-%m-%dT%H:%M:%S"),
               source=str(Path(args.input).resolve()), capture_rate=sr, c4_is_60=True,
               bpm=bpm, ticks_per_note=plan["ticks_per_note"], gate_ticks=plan["gate_ticks"],
               hold=hold_s, tail=plan["tail_seconds"], render_offset=round(off / sr, 6),
               velocities=sorted({n["velocity"] for n in notes}),
               roots=sorted({n["midi"] for n in notes}), passes=npas, entries=entries)
    (out / "session.json").write_text(json.dumps(man, indent=1) + "\n")
    if spread_ms:
        print(f"\n  grid: every note within {spread_ms:.1f} ms of one origin (the file starts at "
              f"{off / sr * 1000:+.0f} ms); cuts land {PRE_ROLL * 1000:.0f} ms before the tick")
    share = sorted(e.get("detune_ct", 0.0) for e in entries if not e.get("octave"))
    if share:
        med = share[len(share) // 2]
        print(f"  partials: median spread {med:.1f} ct"
              + ("  (steady)" if med < 3 else "  (the patch is an ensemble or has a chorus: baked in)"))
    print(f"done in {time.monotonic() - t0:.1f}s: {len(entries)} written, {silent} silent -> {out}")
    return 0 if entries and not silent else 1


# ---------------------------------------------------------------- bake (what the image stores) ---
# The step between the takes and gen_samples.py: a zone is stored as the attack plus ONE loopable
# stationary region, with the note's own decay divided out of everything from `knee` on. The
# firmware multiplies that decay back per voice (eng_sample.c sample_render, SMP_DECAY), so a
# 0.56 s zone still rings for as long as the key is held, and it darkens as it decays because the
# same envelope drives the low-pass. Without this the piano bank is 3-4x larger (the bass notes are
# the whole problem: A0 decays at -9 dB/s and needs 1.4 s to be usable as a one-shot).
# Per zone this writes build/baked/SET/NN_SET_NOTE_vL.wav (22050 Hz, 16-bit, loop in a smpl chunk)
# and one bake.json the encoder reads.
TR = 22050                     # the stored rate (gen_samples.py TR)
BAKE = SRC / "build" / "baked"
XF_MS = 45                     # loop tail crossfade: long enough that the fade itself is a glide, short
                               # enough that the loop still carries its own material
XF_LOCKED_MS = 12              # ... for a loop whose head was placed by loop_points: the seam is then
                               # a fraction of a sample out of phase, so the fade is a short glide
                               # (the 45 ms fade summed two moments of the note and combed at the
                               # pass rate; 4 ms was too quick a correction: heard as a tick)
# set -> (roots, keep s, loop s, knee s, layer, flatten): the roots are the takes the zones were
# chosen from (4 zones +-4.5 st on the strings, 4 +-6.0 on the horn -- their takes are 5.1 s, so
# the flash, not the recording, is what limits the zone count), and flatten=False for the
# ensembles: their takes are held notes, not decays, so the fit would report the bow/blow settling
# and a "decay" that fades a whole note out
#
# `keep` is also the attack the zone gets to keep. A layered ensemble patch swells: the strings
# reach half their peak 0.2 s in and are still rising at 0.8 s, the horn 0.12 s / 0.8 s. A 0.45 s
# zone that looped from 0.17 s cut that swell off mid-rise and jumped back into it, so a swell
# patch played as a flat pad. The ensembles now store 1.65 s and loop the last 0.85 s: the swell
# is the take's own for 0.8 s, then the loop's pass rate is 1.2 Hz (it was 2.4 at 0.42 s), which
# is where whatever is left of the window's own movement stops reading as a rhythm.
#
# The PIANO set is gone (70 KB of flash for 16 zones of 4-bit piano): the takes it needed cannot
# be improved, and the flash buys more of the two sets a player holds notes on instead.
BAKE_SETS = {
    "STRINGS": dict(roots=[46, 55, 64, 73], keep=1.65, loop=0.85, knee=0.20, layer=2, flatten=False),
    "HORN": dict(roots=[38, 50, 62, 74], keep=1.65, loop=0.85, knee=0.20, layer=2, flatten=False),
}


def env_db(x, sr, win_ms=50, hop_ms=10):
    """RMS envelope, dB relative to the take's peak -> [(dB, centre in s)], one point per hop"""
    w = max(2, int(sr * win_ms / 1000))
    h = max(1, int(sr * hop_ms / 1000))
    pk = sio.peak(x) or 1.0
    q = [v * v for v in x]
    out, acc = [], 0.0
    for i in range(len(q)):
        acc += q[i]
        if i >= w:
            acc -= q[i - w]
        if i >= w - 1 and (i - w + 1) % h == 0:
            out.append((db(math.sqrt(max(0.0, acc) / w) / pk), (i - w * 0.5 + 1) / sr))
    return out


def fit_line(env, t0, t1, floor_db):
    """least squares dB/s over the envelope between t0 and t1 s, above the noise floor. The envelope
    is median-filtered over 3 frames first: a single click or a frame of beating must not steer the
    fit, and a curved decay must not be trimmed into the flattest part of itself (rejecting
    outliers there collapses the slope towards 0). -> (slope dB/s, rms residual dB, points used)"""
    pts = [(t, d) for d, t in env if t0 <= t <= t1 and d > floor_db]
    if len(pts) < 4:
        return 0.0, 0.0, len(pts)
    d = [p[1] for p in pts]
    if len(d) >= 3:
        d = [d[0]] + [sorted(d[i - 1:i + 2])[1] for i in range(1, len(d) - 1)] + [d[-1]]
    pts = [(p[0], v) for p, v in zip(pts, d)]
    n = len(pts)
    mt = sum(p[0] for p in pts) / n
    md = sum(p[1] for p in pts) / n
    stt = sum((p[0] - mt) ** 2 for p in pts)
    if stt <= 0:
        return 0.0, 0.0, n
    s = sum((p[0] - mt) * (p[1] - md) for p in pts) / stt
    rms = math.sqrt(sum((md + s * (p[0] - mt) - p[1]) ** 2 for p in pts) / n)
    return max(-120.0, min(0.0, s)), rms, n


def best_lag(x, ls0, sr, span_ms=40, win_ms=30):
    """the offset near ls0 where the loop's tail window is most in phase with that position: the
    wrap then replays the material it just left instead of jumping (a click), and the crossfade
    between the two only has to bridge a level, not a phase"""
    n, W = len(x), max(16, int(win_ms * sr / 1000))
    span = max(1, int(span_ms * sr / 1000))
    tail = x[max(0, n - W):]
    tn = math.sqrt(sum(v * v for v in tail)) or 1.0

    def score(c):
        w = x[c:c + W]
        wn = math.sqrt(sum(v * v for v in w)) or 1.0
        return sum(a * b for a, b in zip(tail, w)) / (tn * wn)

    lo, hi, step = max(0, ls0 - span), min(n - W - 1, ls0 + span), 8
    if hi <= lo:
        return max(0, min(n - W - 1, ls0))
    best = max(range(lo, hi, step), key=score)
    fine = range(max(lo, best - step), min(hi, best + step) + 1)
    return max(lo, min(hi, max(fine, key=score)))


def period_near(x, sr, f0, a, b, spread=0.06):
    """the waveform's period in samples, measured by autocorrelation over x[a..b] around the
    expected f0 (the zone's root) +- spread. -> fractional samples, or 0 if it does not lock"""
    lo, hi = max(4, int(sr / (f0 * (1 + spread)))), min(len(x) - 1, int(sr / (f0 * (1 - spread))) + 1)
    if hi <= lo or b - a < 2 * hi:
        return 0.0
    seg = x[a:b]
    m = sum(seg) / len(seg)                    # remove DC: a loop's offset would bias every lag
    seg = [v - m for v in seg]
    e = sum(v * v for v in seg) or 1.0

    def ac(lag):
        s = sum(seg[i] * seg[i + lag] for i in range(len(seg) - lag))
        return s / e

    lags = range(lo, hi)
    best = max(lags, key=ac)
    if ac(best) < 0.30:                        # not periodic in this range (noise, a transient)
        return 0.0
    y0, y1, y2 = ac(best - 1), ac(best), ac(best + 1)
    d = y0 - 2 * y1 + y2
    frac = 0.5 * (y0 - y2) / d if d else 0.0   # parabolic peak: a fractional-sample period
    return best + max(-0.5, min(0.5, frac))


def head_on_continuation(x, ls, le, p, cont, sr):
    """the loop start near ls whose waveform matches what the take does right after the stored zone.

    Snapping the length to a whole number of periods is not enough on its own: the measured period
    is fractional and the take's pitch is not perfectly steady, so over the 100-200 periods a loop
    spans the phase creeps by a few samples, and the head lands a few samples off the phase the wrap
    arrives on. That mismatch is a step of 30-90 % of the peak at the seam (1.5-6x the waveform's own
    sample-to-sample slope) -- the click the crossfade then has to bury. The take itself continues
    past `le` (the bake reads keep + 1.2 s for the decay fit), so instead of guessing from the
    period, ask the recording: the right head is the one that the material *after* the end flows
    into. -> (ls, correlation 0..1)"""
    W = min(len(cont), ls - 2, max(int(0.03 * sr), int(2 * p)))
    if W < 16:
        return ls, 0.0                           # no room: the period-snapped head stands
    ref = cont[:W]
    rn = math.sqrt(sum(v * v for v in ref)) or 1.0

    def score(c):
        w = x[c:c + W]
        if len(w) < W:
            return -2.0
        wn = math.sqrt(sum(v * v for v in w)) or 1.0
        return sum(a * b for a, b in zip(ref, w)) / (rn * wn)

    span, step = int(round(p)), 2                # the correlation peak is a few samples wide
    lo, hi = max(2, ls - span), min(le - W - 2, ls + span)
    if hi <= lo:
        return ls, 0.0
    best = max(range(lo, hi + 1, step), key=score)
    fine = range(max(lo, best - step), min(hi, best + step) + 1)
    c = max(fine, key=score)
    return c, score(c)


def env_period(x, sr, lo_s=0.10, hi_s=1.20, win_ms=40, hop_ms=10):
    """The dominant period (s) of x's slow *timbre* movement -- the ensemble's chorus beat and
    vibrato envelope -- or 0 if it has none.

    A loop made of a whole number of the note's periods still ends on a different point of this
    movement than it started on, because the two periods are unrelated: every pass then begins a
    fraction of a beat later, and the band balance steps at the wrap -- heard as a pulse at the
    pass rate even when the level and the waveform phase are perfect (the "tick" that outlived the
    seam fix). Measure the movement (the three bands' envelopes summed, detrended, autocorrelated)
    and let the loop length be a multiple of it; the wrap then continues the beat where it left
    off instead of restarting it.
    """
    n = len(x)
    w = max(8, int(win_ms * sr / 1000))
    h = max(1, int(hop_ms * sr / 1000))
    if n < 4 * w:
        return 0.0
    bands = band_split(x, sr)
    sig = None
    for band in bands:
        pre, acc = [0.0], 0.0
        for v in band:
            acc += v * v
            pre.append(acc)
        env = []
        for i in range(0, n - w, h):
            hi, l = i + w, i
            env.append(10.0 * math.log10(max((pre[hi] - pre[l]) / max(hi - l, 1), 1e-12)))
        sig = env if sig is None else [a + b for a, b in zip(sig, env)]
    m = sum(sig) / len(sig)
    sig = [v - m for v in sig]
    np_ = len(sig)
    mt = (np_ - 1) / 2.0
    stt = sum((j - mt) ** 2 for j in range(np_)) or 1.0
    slope = sum((j - mt) * sig[j] for j in range(np_)) / stt
    sig = [sig[j] - slope * (j - mt) for j in range(np_)]        # the drift is level_flatten's job
    e0 = sum(v * v for v in sig) or 1.0
    lo = max(2, int(lo_s * sr / h))
    hi = min(np_ // 2, int(hi_s * sr / h))
    if hi <= lo:
        return 0.0
    best, bl = 0.0, 0
    for lag in range(lo, hi + 1):
        num = sum(sig[k] * sig[k + lag] for k in range(np_ - lag))
        den = math.sqrt((sum(v * v for v in sig[:np_ - lag]) or 1.0) * (e0 - sum(v * v for v in sig[np_ - lag:]) or 1.0))
        r = num / den
        if r > best:
            best, bl = r, lag
    if best < 0.45 or not bl:
        return 0.0                                  # no clear beat: let the period count decide
    return bl * h / sr


def loop_points(x, sr, f0, keep_s, loop_s, xf, cont=None, snap_env=False):
    """(ls, le) for a take of keep_s seconds: the loop is a whole number of the note's periods and
    ends at the last stored sample, so the wrap continues the waveform instead of restarting it at
    an arbitrary phase. A loop that is not period-aligned warps every partial by a different amount
    each pass: that is heard as a tremolo/warble at the loop rate even when the level is flat, and
    it gets slower (and more obvious) the further the zone is transposed down. Le (the last sample)
    is fixed; the length is the requested loop snapped to the nearest period count -- and then the
    head is moved to where the take's own continuation says the wrap should land (see
    head_on_continuation), which is what keeps the residual seam inside one sample's slope.
    snap_env (the held ensembles): the length is also snapped to a whole number of the take's own
    chorus beats (env_period), so the band balance wraps continuously too."""
    n = len(x)
    le = n - 1
    want = int(loop_s * sr)
    if snap_env:
        t = env_period(x, sr)
        if t > 0.02:
            k = max(1, int(round(want / (t * sr))))
            w2 = int(round(k * t * sr))
            if 0.55 * want <= w2 <= 1.8 * want and w2 < n - int(0.12 * sr):
                want = w2
    p = period_near(x, sr, f0, max(0, n - want - int(0.03 * sr)), n)
    if p <= 0 or p < 6:                        # too high for the lag search, or not periodic
        return best_lag(x, int((keep_s - loop_s) * sr), sr), le, False
    k = max(2, int(round(want / p)))
    ls = int(round(le + 1 - k * p))
    if cont:
        c, corr = head_on_continuation(x, ls, le, p, cont, sr)
        if corr > 0.35:                        # the continuation agrees on a phase: take it
            ls = min(max(ls - int(p), c), ls + int(p))
    if ls < xf or ls >= le - xf:
        return best_lag(x, int((keep_s - loop_s) * sr), sr), le, False
    return ls, le, True


def flatten(x, sr, knee, ls, s1, s2, cap_db=18.0):
    """divide the fitted decay out of x from `knee` on (1.0 there, so the attack keeps its own
    level), following s1 up to the loop start and s2 after it -> ([float], lift at the end).
    The boost stops at `cap_db`: a note that has already fallen into the room would otherwise be
    amplified back up, and the loop would hold 18 dB of noise instead of silence"""
    y = list(x)
    m1 = 10 ** (-s1 / (20.0 * sr))
    m2 = 10 ** (-s2 / (20.0 * sr))
    top = 10 ** (cap_db / 20.0)
    lit = 1.0
    for i in range(knee, len(y)):
        lit *= m2 if i >= ls else m1
        if lit > top:
            lit = top
        y[i] *= lit
    return y, lit


def crossfade_loop(x, ls, le, xf):
    """blend the material just *before* the loop start into the loop's own tail, so that the sample
    the wrap lands from is the sample the recording had right before the head: x[le] becomes
    x[ls - 1] by the end of the fade, and x[le] -> x[ls] is then the take's own adjacency, not a
    jump between two unrelated phases. Only the loop's tail is rewritten (never the head or the
    material before ls), so the first pass into the loop and the loop's own phase stay as recorded.
    The two windows are the same length and `best_lag` aligned the head's phase with this tail."""
    for i in range(xf):
        a = 0.5 - 0.5 * math.cos(math.pi * (i + 1) / xf)
        j = le - xf + 1 + i
        x[j] = x[j] * (1.0 - a) + x[ls - xf + i] * a


def _flat_gain_db(band, w, h, smooth_n, cap_db, periodic):
    """per-frame gain (dB) that makes the band's short-term level constant.

    periodic: the frames are a loop -- their windows wrap around the end and the gains are smoothed
    circularly. That is what makes the correction usable on a loop: a gain that fades back to 1 at
    the region's edges (the only option when the region is a piece of a longer signal) steps at the
    wrap, and the step is a timbre jump once per pass -- the tick again, this time from the leveller
    itself.
    """
    n = len(band)
    pre, acc = [0.0], 0.0
    for v in band:
        acc += v * v
        pre.append(acc)
    env = []
    for i in range(0, n, h):
        if periodic:
            e = 0.0
            for j in range(w):
                q = band[(i - w // 2 + j) % n]
                e += q * q
        else:
            l = max(0, i - w // 2)
            hi = min(n, l + w)
            e = pre[hi] - pre[max(0, hi - w)]
        env.append(math.sqrt(max(e, 0.0) / w))
    if len(env) < 4:
        return None
    tgt = math.exp(sum(math.log(max(v, 1e-6)) for v in env) / len(env))
    gdb = [max(-cap_db, min(cap_db, 20.0 * math.log10(tgt / max(v, 1e-6)))) for v in env]
    m = len(gdb)
    sm = []
    for i in range(m):
        if periodic:
            sm.append(sum(gdb[(i + j) % m] for j in range(-smooth_n, smooth_n + 1)) / (2 * smooth_n + 1))
        else:
            l, r = max(0, i - smooth_n), min(m, i + smooth_n + 1)
            sm.append(sum(gdb[l:r]) / (r - l))
    return sm


def level_flatten(x, sr, a, b, smooth_ms=12, cap_db=20.0, strength=1.0, periodic=False):
    """Make the RMS of x[a..b] constant, i.e. divide out its slow level envelope.

    A loop repeats one window for as long as the key is held, so whatever level shape that window
    has becomes a *periodic* one: an ensemble's chorus beats (3-7 dB of drift over a 0.3 s window)
    turn into a tremolo at the loop rate, and the same window transposed down an octave plays it
    twice as slow, which is why the low notes wobble most. The envelope here is measured over 30 ms
    and the gain is smoothed over `smooth_ms` (12 ms: short enough to catch the beats a 0.4 s pass
    repeats, long enough to leave the waveform's own edge alone). `cap_db` keeps the correction to a
    plausible range, so a genuinely rising crescendo is tamed rather than frozen, and `strength`
    below 1.0 leaves part of the movement in.

    periodic: the region is the loop itself (b = le), so the gains wrap with it and the pre-roll
    (the material just before `a`) gets the loop's first sample of correction: the wrap then
    continues the gain instead of returning to an unlevelled edge.
    """
    n = b - a + 1
    if n < int(0.15 * sr):
        return
    w = max(8, int(0.03 * sr))
    h = max(1, int(0.005 * sr))
    sm = _flat_gain_db(x[a:b + 1], w, h, max(1, int(smooth_ms / 5)), cap_db, periodic)
    if not sm:
        return
    _apply_gain(x, a, b, sm, h, strength, periodic, sr)


def _apply_gain(x, a, b, sm, h, strength, periodic, sr):
    """multiply x[a..b] by the interpolated per-frame gains. With `periodic` the gains wrap, and the
    40 ms before `a` get the first gain faded in: the material the first pass plays just before the
    loop has to be corrected too, or entering the loop steps by g[0] dB."""
    n = b - a + 1
    m = len(sm)
    for i in range(n):
        p = i / h
        k = int(p)
        f = p - k
        if periodic:
            g = sm[k % m] * (1.0 - f) + sm[(k + 1) % m] * f
        else:
            k = min(m - 1, k)
            g = sm[k] * (1.0 - f) + sm[min(m - 1, k + 1)] * f
        x[a + i] *= 10.0 ** (strength * g / 20.0)
    if periodic and a > 0:
        pre = min(a, int(0.04 * sr))
        g0 = strength * sm[0]
        for i in range(pre):
            x[a - pre + i] *= 10.0 ** (g0 * (i + 1) / pre / 20.0)


BAND_SPLITS = (350.0, 2000.0)   # the crossovers of band_flatten, Hz
BAND_POLES = 4                 # 24 dB/oct: a one-pole difference leaks so much above the corner that
                               # correcting the top band hardly moved it -- most of the brightness was
                               # still in the band below, uncorrected


def _lowpass(seg, sr, fc, poles=BAND_POLES):
    """cascaded one-pole low pass of a list (offline: no need to be cheap)"""
    out = list(seg)
    c = 1.0 - math.exp(-2.0 * math.pi * fc / sr)
    for _ in range(poles):
        s = out[0]
        for i in range(1, len(out)):
            s += c * (out[i] - s)
            out[i] = s
    return out


def band_split(seg, sr, splits=BAND_SPLITS):
    """seg -> [low, mid, high] by successive subtraction, so the three sum back to seg exactly while
    each one only holds its own octaves"""
    bands, rest = [], list(seg)
    for f in splits:
        lp = _lowpass(rest, sr, f)
        bands.append(lp)
        rest = [a - b for a, b in zip(rest, lp)]
    bands.append(rest)
    return bands


def _one_pole(x, sr, fc, lo, hi):
    """one-pole low pass of x[lo..hi), state seeded at x[lo] so the pad carries no startup step"""
    c = 1.0 - math.exp(-2.0 * math.pi * fc / sr)
    s = x[lo]
    out = [s]
    for i in range(lo + 1, hi):
        s += c * (x[i] - s)
        out.append(s)
    return out


def band_flatten(x, sr, a, b, splits=BAND_SPLITS, window_ms=26, hop_ms=6,
                 smooth_ms=16, cap_db=6.0, strength=1.0, periodic=False):
    """Make the level of each band of x[a..b] constant, not just the total level.

    level_flatten divides out the total RMS and leaves the *balance* between the bands free to
    wander. On a held ensemble note that balance is the chorus: the detuned partials beat, four of
    them add up at one moment and cancel at another, so the window is bright here and dark 0.2 s
    later. Set to repeat, that swing is a pulse at the loop rate -- a tick on the strings and the
    horn, and a tremolo when the loop is short enough to be heard as one. Flattening each band's own
    envelope takes the pulse out of the timbre while the total level stays where it was. The bands
    are the differences of the same two filtered signals, so they sum back to x exactly: with no
    correction this is the identity. The cap is what keeps it a correction and not a re-synthesis,
    and the smoothing keeps the gain itself slower than the lowest band's period.
    """
    n = b - a + 1
    if n < int(0.05 * sr):
        return
    pad = min(a, int(0.09 * sr))              # the filters settle over the pad, outside the window
    lo = a - pad
    bands = band_split(x[lo:b + 1], sr)       # LF, MF, HF (they sum back to x exactly)
    w = max(4, int(window_ms * sr / 1000))
    h = max(1, int(hop_ms * sr / 1000))
    sg = max(1, int(smooth_ms * sr / 1000 / h))
    pk = max(abs(v) for v in x[a:b + 1]) or 1.0
    for band in bands:
        seg = band[pad:pad + n]
        rms = math.sqrt(sum(v * v for v in seg) / max(len(seg), 1))
        if rms < pk * 1e-3:                    # 60 dB under the window: the band is dither, not music
            continue
        sm = _flat_gain_db(seg, w, h, sg, cap_db, periodic)
        if sm:
            _apply_gain(band, pad, pad + n - 1, sm, h, strength, periodic, sr)
    for i in range(n):
        x[a + i] = sum(band[pad + i] for band in bands)


def write_wav_loop(path, samples, sr, ls, le):
    """16-bit mono PCM with a smpl chunk carrying one forward loop (sampleio.read_wav reads it)"""
    a = array("h", samples)
    if sys.byteorder != "little":
        a.byteswap()
    data = a.tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, sr, sr * 2, 2, 16)
    smpl = struct.pack("<9I", 0, 0, int(round(1e9 / sr)), 60, 0, 0, 0, 1, 0)
    smpl += struct.pack("<6I", 0, 0, int(ls), int(le), 0, 0)
    body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt +
            b"data" + struct.pack("<I", len(data)) + data +
            b"smpl" + struct.pack("<I", len(smpl)) + smpl)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def takes_of(setdir):
    """{MIDI note: {layer: path}} from the file names (NN_SET_NOTE_vL.wav), _BAD takes skipped"""
    out = {}
    for p in sorted(setdir.glob("*.wav")):
        if "_BAD" in p.stem:
            continue
        m = re.search(r"_v(\d+)$", p.stem)
        if not m:
            continue
        out.setdefault(note_from_name(p.stem), {})[int(m.group(1))] = p
    return out


def pick_layer(layers, want):
    """the asked-for velocity layer, or the nearest one that has a take (a re-export sometimes only
    replaces one layer) -> (layer, path, was it the one asked for)"""
    if want in layers:
        return want, layers[want], True
    k = min(layers, key=lambda l: (abs(l - want), -l))
    return k, layers[k], False


def bake_zone(path, root, sec, log=print):
    """one take -> the stored zone: trim, resample to TR, fit the decay, flatten it, loop, normalise.
    -> (int16 samples, ls, le, zone dict)"""
    sr, x = sio.read_any_wav(path)
    on = sio.onset(x, 0.02)
    tail = x[-int(0.3 * sr):]
    pk0 = sio.peak(x) or 1.0
    room = db(math.sqrt(sum(v * v for v in tail) / max(1, len(tail))) / pk0)   # the take's own silence
    x = x[max(0, on - int(0.002 * sr)):]
    # analysis needs the tail past the stored zone: s2 (the aftersound) is fitted out there
    x = x[:int(min(len(x), (sec["keep"] + 1.2) * sr))]
    x = sio.resample(x, sr, TR)
    keep = int(sec["keep"] * TR)
    st = x[:keep]
    if len(st) < int(0.25 * TR):
        raise SystemExit(f"{path.name}: {len(st) / TR:.2f}s of tone, too short to bake")
    env = env_db(x, TR)
    thr = min(-12.0, room + 12.0)         # below this it is the take's room, not the note: never fit it
    ls0 = int((sec["keep"] - sec["loop"]) * TR)
    knee = int(sec["knee"] * TR)
    if sec["flatten"]:
        s1, r1, n1 = fit_line(env, sec["knee"], ls0 / TR, thr)          # the prompt sound
        # the aftersound: fitted inside the stored zone, because past `keep` a note is in the room
        # and a fit there reports "no decay" (the piano's top octave used to keep its whole fall)
        s2, r2, n2 = fit_line(env, ls0 / TR, min(ls0 / TR + 0.5, keep / TR), thr)
        if s2 > -0.5:                        # the zone is short and still climbing there (the low
            sf, rf, nf = fit_line(env, ls0 / TR, min(ls0 / TR + 0.5, len(x) / TR - 0.05), thr)
            if nf >= 8:                      # notes swell for a moment): read the take's own tail
                s2, r2, n2 = sf, rf, 2 * n2
        s1 = s1 if n1 >= 4 else s2          # a knee too close to the loop for a fit of its own
        s2 = s2 if n2 >= 8 else s1          # a note already in the room: one slope, the prompt one
        if s1 > -0.5 and s2 > -0.5:          # both fits rose: it is a sustained note, not a decay
            s1 = s2 = -6.0
        elif s1 > -0.5:
            s1 = s2
        s1, s2 = max(-60.0, s1), max(-60.0, s2)   # steeper than this is the room, not the string
    else:
        s1 = s2 = 0.0
        r1 = r2 = 0.0
        n1 = n2 = 0
    y, lift = flatten(st, TR, knee, ls0, s1, s2)
    # the loop's own level: flatten it before the loop start is chosen, so the pass that repeats has
    # no level shape to repeat (see level_flatten: a repeated swell is a tremolo at the loop rate)
    xf = int(XF_MS * TR / 1000)
    cont = x[keep:keep + int(0.25 * TR)]       # what the take does right after the stored zone
    ls, le, locked = loop_points(y, TR, 440.0 * 2 ** ((root - 69) / 12.0), sec["keep"], sec["loop"],
                                 xf, cont, snap_env=not sec["flatten"])
    if ls < xf:
        raise SystemExit(f"{path.name}: loop start {ls / TR:.3f}s leaves no room before it for the crossfade")
    # The loop is the region whose envelope has to be steady: per band first, then the total. Both
    # are periodic -- the gains wrap with the loop (b = le) and the 40 ms before ls get the first
    # one faded in -- so the correction itself continues across the wrap. A gain that faded back to
    # 1 at the loop's edges made the wrap a timbre step once per pass: the tick again, from the
    # leveller. The takes are swells (the players' crescendo), so the window cut from them carries
    # several dB of tilt that only a periodic correction can take out.
    #
    # Twice: each band is corrected against the signal as it was when the pass started, and the
    # bands are not independent (a low note's fundamental is most of the low band, so correcting it
    # changes what the split of the sum looks like). The second pass measures the result and takes
    # the rest out -- 2.8 dB of low-band movement becomes 2.1, where one pass left 2.5.
    for _ in range(2):
        band_flatten(y, TR, ls, len(y) - 1, periodic=True)
        level_flatten(y, TR, ls, len(y) - 1, periodic=True)
    # The head now lands where the take's own continuation flows into it, so what is left at the
    # wrap is a fraction of a sample of phase. The fade only has to glide that: 12 ms puts its
    # sidebands below ~80 Hz, whereas 4 ms put a 40-70 %-of-peak correction into 4 ms (a chirp once
    # per pass -- the tick). It no longer costs the comb a long fade did, because the head and the
    # tail are on the same phase now. The zones that could not be locked keep the 45 ms fade.
    crossfade_loop(y, ls, le, int((XF_LOCKED_MS if locked else XF_MS) * TR / 1000))
    pk = sio.peak(y) or 1.0
    smp = [max(-32768, min(32767, int(v / pk * 30000))) for v in y]
    z = dict(file=path.name.replace(".wav", "") + ".wav", root=root, note=note_name(root),
             n=len(smp), ls=ls, le=le, knee=knee, db1=round(s1, 2), db2=round(s2, 2),
             lift_db=round(db(lift), 2), fit_db=(round(r1, 2), round(r2, 2)), frames=(n1, n2))
    return smp, ls, le, z


def cmd_bake(args):
    """assets/samples-own/SET/*.wav -> build/baked/SET/ (the flattened zones + bake.json)"""
    name = args.set.upper()
    if name not in BAKE_SETS:
        raise SystemExit(f"bake: no plan for {name} (have: {', '.join(BAKE_SETS)})")
    plan = dict(BAKE_SETS[name])
    for k in ("keep", "loop", "knee", "layer"):
        if getattr(args, k, None) is not None:
            plan[k] = getattr(args, k)
    src = Path(args.dir) if args.dir else OWN / name
    out = Path(args.out) if args.out else BAKE / name
    takes = takes_of(src)
    if not takes:
        raise SystemExit(f"bake: no takes in {src}")
    missing = [r for r in plan["roots"] if r not in takes]
    if missing:
        raise SystemExit(f"bake: no take for " + ", ".join(note_name(r) for r in missing) + f" in {src}")
    t0 = time.monotonic()
    total, zones, swapped = 0, [], []
    for root in plan["roots"]:
        lay, path, exact = pick_layer(takes[root], plan["layer"])
        if not exact:
            swapped.append(f"{note_name(root)} v{lay}")
        smp, ls, le, z = bake_zone(path, root, plan)
        z["layer"] = lay
        z["file"] = f"{len(zones):02d}_{name}_{note_name(root)}_v{lay}.wav"
        zones.append(z)
        total += (len(smp) + 1) // 2
        if not args.dry_run:
            out.mkdir(parents=True, exist_ok=True)
            write_wav_loop(out / z["file"], smp, TR, ls, le)
        print(f"  {note_name(root):>4}  v{lay}  {len(smp) / TR:4.2f}s  loop {ls / TR:5.3f}..{le / TR:5.3f}s "
              f"({(le - ls) / TR:4.2f}s)  decay {z['db1']:7.2f}/{z['db2']:7.2f} dB/s "
              f"(fit {z['fit_db'][0]:.2f}/{z['fit_db'][1]:.2f} dB, {z['frames'][0]}/{z['frames'][1]} pts)  "
              f"lift {z['lift_db']:+5.2f} dB{'  [dry run]' if args.dry_run else ''}")
    meta = dict(set=name, layer=plan["layer"], keep=plan["keep"], knee=plan["knee"],
                tr=TR, zones=zones, bytes=total, layers_swapped=swapped,
                generated=time.strftime("%Y-%m-%dT%H:%M:%S"))
    if not args.dry_run:
        out.mkdir(parents=True, exist_ok=True)
        write = {z["file"] for z in zones}
        for old in out.glob("*.wav"):          # a zone list that changed must not leave the old ones
            if old.name not in write:          # behind: gen_samples.py reads the directory, not
                old.unlink()                   # bake.json, and would encode both
        (out / "bake.json").write_text(json.dumps(meta, indent=1) + "\n")
    if swapped:
        print(f"  (no v{plan['layer']} take, used the nearest layer: {', '.join(swapped)})")
    print(f"{name}: {len(zones)} zones, {total} B ADPCM at {TR} Hz "
          f"({total / 11025:.2f} s of stored audio) in {time.monotonic() - t0:.1f}s -> {out}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Auto-sampler: drive a DAW over MIDI, record its audio.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p, with_notes=True):
        p.add_argument("--device", help="CoreAudio input: index or name substring (default: BlackHole)")
        p.add_argument("--rate", type=int, default=48000, help="capture rate (default 48000)")
        if with_notes:
            p.add_argument("--preset",
                           help="instrument defaults (roots, velocities, hold, tail): "
                                + ", ".join(PRESETS) + "; '+' merges ranges, e.g. horn+brass")
            p.add_argument("--roots", help='explicit notes: "C2 G2 D3 A3 E4 B4" (default: the range)')
            p.add_argument("--root-lo", type=int, default=36)
            p.add_argument("--root-hi", type=int, default=84)
            p.add_argument("--root-step", type=int, default=5)
            p.add_argument("--vel", help=f'MIDI velocities, one per layer (default {DEFAULT_VELS})')
            p.add_argument("--passes", type=int, default=1,
                           help="takes per note (3: an ensemble built from one player)")
            p.add_argument("--hold", type=float, help=f"seconds the note is held (default {DEFAULT_HOLD})")
            p.add_argument("--tail", type=float, help=f"seconds recorded after note-off (default {DEFAULT_TAIL})")
            p.add_argument("--channel", type=int, default=0, help="MIDI channel 0..15")

    p = sub.add_parser("devices", help="list CoreAudio input devices")
    p.set_defaults(func=cmd_devices)

    p = sub.add_parser("doctor", help="check MIDI, audio and a 1 s capture")
    common(p, with_notes=False)
    p.add_argument("--midi-out", help="test this MIDI destination instead of a virtual port")
    p.add_argument("--no-loopback", action="store_true",
                   help="do not play a test tone into the device when it is silent")
    p.add_argument("--strict", action="store_true",
                   help="warnings are failures (the default exits 0 on a silent device)")
    p.add_argument("--no-capture", action="store_true")
    p.set_defaults(func=cmd_doctor)

    p = sub.add_parser("session", help="the sampling run")
    p.add_argument("name", help="set name (the folder and the file-name tag)")
    common(p)
    p.add_argument("--out", help="output directory (default assets/samples-own/NAME)")
    p.add_argument("--preroll", type=float, default=0.25, help="seconds of lead-in kept before each note")
    p.add_argument("--gap", type=float, default=3.0,
                   help="seconds of silence between notes, on top of --tail: a decaying patch "
                        "(piano, reverb you forgot to turn off) must be finished before the next "
                        "attack or the onset detector hears its release (default 3)")
    p.add_argument("--only", help='re-record just these roots: "A4 C#3". Keeps the file names of '
                                  'the full run, so it overwrites those takes and nothing else')
    p.add_argument("--overwrite", action="store_true",
                   help="re-record the takes that already exist (with --only: just those)")
    p.add_argument("--dry-run", action="store_true", help="print the plan, record nothing")
    p.add_argument("--mid", help="also write the session as a MIDI file")
    p.add_argument("--input", help="read a file instead of a device (it plays in real time, "
                                   "no MIDI is sent): for checking the capture path, one note")
    p.add_argument("--midi-name", default="StudioKEYS Autosampler", help="the virtual port's name")
    p.add_argument("--midi-out", help="send to an existing MIDI destination instead of creating a "
                                      "virtual port (e.g. 'IAC Driver Bus 1')")
    p.add_argument("--verbose", action="store_true")
    p.set_defaults(func=cmd_session)

    p = sub.add_parser("mid", help="write the session as one MIDI file + the plan `render` needs "
                                   "(drop it in the DAW, freeze, flatten, export)")
    p.add_argument("name", help="set name (the folder and the file-name tag)")
    p.add_argument("out", help="the .mid to write")
    common(p)
    p.add_argument("--gap", type=float, default=3.0,
                   help="silence between notes, on top of tail; the period is rounded up to a "
                        "whole beat (default 3)")
    p.add_argument("--dir", help="session folder (default assets/samples-own/NAME)")
    p.add_argument("--plan", help="the plan JSON (default DIR/plan.json)")
    p.set_defaults(func=cmd_mid)

    p = sub.add_parser("render", help="slice a flattened export of that MIDI file into the set")
    p.add_argument("set", help="set name (the plan's folder, and the WAV name tag)")
    p.add_argument("input", help="the exported WAV (stereo is fine, it is downmixed)")
    p.add_argument("--plan", help="the plan JSON (default assets/samples-own/SET/plan.json)")
    p.add_argument("--dir", help="session folder, if not the default")
    p.add_argument("--bpm", type=float, help="the tempo the render was made at (default: the "
                                             "plan's 120, then measured from the attacks)")
    p.add_argument("--offset", type=float, help="force the render's start, in seconds (default: "
                                                "measured from all the attacks)")
    p.add_argument("--only", help='slice just these roots: "A4 C#3"')
    p.add_argument("--out", help="output directory (default the set's folder)")
    p.add_argument("--dry-run", action="store_true", help="print where each note would be cut")
    p.add_argument("--force", action="store_true",
                   help="write even when the file does not line up with the plan")
    p.set_defaults(func=cmd_render)

    p = sub.add_parser("probe", help="record N seconds and say what is in the audio (with --note, "
                                     "send one note too)")
    p.add_argument("--device", help="CoreAudio input: index or name substring (default BlackHole)")
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--seconds", type=float, default=3.0, help="how long to listen")
    p.add_argument("--note", help="also play this note, e.g. A4")
    p.add_argument("--vel", type=int, default=100)
    p.add_argument("--channel", type=int, default=0)
    p.add_argument("--midi-name", default="StudioKEYS Autosampler")
    p.add_argument("--midi-out", help="send the note to an existing destination instead")
    p.add_argument("--out", help="where to write the capture (default /tmp/autosample-probe.wav)")
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("ports", help="list MIDI sources and destinations")
    p.set_defaults(func=cmd_ports)

    p = sub.add_parser("port", help="hold the virtual MIDI source open so a DAW can enable it")
    p.add_argument("--name", default="StudioKEYS Autosampler")
    p.add_argument("--midi-out", help="open an existing destination instead (does nothing useful "
                                      "for visibility; here for symmetry)")
    p.add_argument("--seconds", type=float, default=0, help="exit after N seconds (0 = until Ctrl-C)")
    p.set_defaults(func=cmd_port)

    p = sub.add_parser("miditest", help="loopback test of the MIDI send path (no DAW needed)")
    p.set_defaults(func=cmd_miditest)

    p = sub.add_parser("presets", help="the instrument presets: what to record, and what it costs")
    p.set_defaults(func=cmd_presets)

    p = sub.add_parser("bake", help="a recorded set -> build/baked/SET: decay flattened out of the "
                                    "tail, looped, normalised, plus the decay table gen_samples.py reads")
    p.add_argument("--set", required=True, choices=sorted(BAKE_SETS),
                   help="the set to bake (the folder under assets/samples-own)")
    p.add_argument("--dir", help="the takes' directory, if not assets/samples-own/SET")
    p.add_argument("--out", help="where to write (default build/baked/SET)")
    p.add_argument("--layer", type=int, help="velocity layer (default: the plan's, 2 = mf)")
    p.add_argument("--keep", type=float, help="stored seconds per zone")
    p.add_argument("--loop", type=float, help="loop length in seconds (the crossfade is inside it)")
    p.add_argument("--knee", type=float, help="where the decay starts to be divided out, in seconds")
    p.add_argument("--dry-run", action="store_true",
                   help="print the slope, loop and lift per zone, write nothing")
    p.set_defaults(func=cmd_bake)

    p = sub.add_parser("pack", help="a recorded session -> one user slot image (roots and lengths "
                                    "chosen to fit 36 KiB)")
    p.add_argument("set", help="session folder name (assets/samples-own/SET)")
    p.add_argument("--dir", help="the session directory, if not the default")
    p.add_argument("--name", default=None, help="slot name, max 8 chars (default: SET)")
    p.add_argument("--layer", type=int, help="velocity layer to pack (default: the one nearest "
                                            "--velocity)")
    p.add_argument("--velocity", type=int, default=100, help="which layer is the least wrong as a "
                                                            "single layer (default 100 = mf/ff)")
    p.add_argument("--roots", help="keep only these roots")
    p.add_argument("--seconds", type=float, help="longest a zone may be (default: as long as the "
                                                "budget allows)")
    p.add_argument("--min-seconds", type=float, default=0.40,
                   help="shortest zone worth building: a one-shot shorter than this is a click, not "
                        "a note (default 0.40)")
    p.add_argument("--budget", type=int, help=f"ADPCM bytes available (default {SLOT_BUDGET})")
    p.add_argument("--zone", type=float, default=4.0, help="widest stretch still acceptable, in "
                                                           "semitones (default 4)")
    p.add_argument("--slot", type=int, default=1, help="slot the printed upload command targets")
    p.add_argument("--out", help="write PREFIX.hdr / PREFIX.bin")
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("selftest", help="run the pipeline against an internal synth")
    p.set_defaults(func=lambda a: selftest(a))

    args = ap.parse_args(argv)
    if getattr(args, "gap", None) is None:
        args.gap = 0.5
    if getattr(args, "name", "x") is None:
        args.name = args.set
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
