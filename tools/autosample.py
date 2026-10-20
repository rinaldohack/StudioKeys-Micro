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
  autosample.py pack SET [--out P]      that session -> one user slot (.hdr/.bin, roots chosen to fit)
  autosample.py mid SET OUT.mid         write the session as a MIDI file (no ports at all)
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
`--input FILE` reads a file instead of a device (it plays in real time, no MIDI is sent): the
way to check the whole capture path, and the way to sample a performance bounced to disk.

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

    def __init__(self, dev_index, sr=48000, preroll=0.25, verbose=False, path=None):
        self.sr, self.index, self.preroll, self.path = sr, dev_index, preroll, path
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.err = []
        self.proc = None
        self.stop_flag = False
        self.total = 0                       # frames received, monotonic
        self.verbose = verbose

    def start(self):
        import threading
        if self.path:
            # apad: the file plays once, then the stream is silence forever. Without it ffmpeg hits
            # EOF mid-session and `wait()` correctly reports "ffmpeg stopped", which looks exactly
            # like a routing failure when you are only trying to check the capture path.
            src = ["-re", "-i", str(self.path), "-af", "apad"]
        else:
            src = ["-f", "avfoundation", "-i", f":{self.index}"]
        cmd = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error"] + src + \
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
        a, b = max(0, a), max(0, b)
        with self.lock:
            return bytes(self.buf[a * 2:b * 2])

    def close(self):
        self.stop_flag = True
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()


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
    return cent, spread, peaks


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
    cent, spread, peaks = _cluster_raw(seg, sr, f, cts, win)
    ref = [16384.0 * math.sin(2.0 * math.pi * f * 2.0 ** (cent / 1200.0) * i / sr) for i in range(nn)]
    _c, floor, _p = _cluster_raw(ref, sr, f, cts, win)
    spread = (spread * spread - floor * floor) ** 0.5 if spread > floor else 0.0
    return cent, spread, peaks


def check_pitch(x, sr, midi):
    """what came back, relative to the note that was asked for -> (cents, verdict, heard, detune)

    `detune` is the power-weighted spread of the partials around `cents`: 1-3 ct for a clean note,
    3-6 for a real 3-string piano unison, 10-30 for a vibrato / chorus / detuned stack. It is not an
    error -- but it is baked into the sample, and stretching a zone transposes it with the pitch.

    Three Goertzel probes (f/2, f, 2f) instead of a general pitch detector, because the expected
    note is known and the only questions left are "is it this note", "is it an octave out" and "is
    it in tune". Autocorrelation cannot answer the octave question: any tone with even harmonics
    correlates just as well at T/2, which is why YIN needs its cumulative-mean normalisation. A DFT
    probe has no such ambiguity -- a note at f has no energy at f/2 at all.

    A verdict of "other" is then identified, not just reported: a semitone sweep over +-2 octaves
    says which note it actually was, which is the difference between "wrong note" and "your track
    is transposed by a fifth / the arpeggiator is latched / that is the metronome".
    """
    import math
    f = 440.0 * 2.0 ** ((midi - 69) / 12.0)
    start = min(len(x) // 5, sr // 5)                      # past the attack, before the tail
    # 0.5 s, not 0.25: a 5-6 Hz unison split is one lobe of a 0.25 s window, so the centroid would
    # wander with whichever side happened to win. Longer window, less variance, same JND.
    n = min(len(x) - start, max(int(sr * 0.50), int(12.0 * sr / f)))
    if n < 512:
        return 0.0, "", None, 0.0
    seg = x[start:start + n]
    rms = (sum(v * v for v in seg) / max(1, len(seg))) ** 0.5 / 32768.0
    if rms < 0.002:
        return 0.0, "noise", None, 0.0                     # nothing pitched came back
    # magnitudes relative to the signal's own energy, so the thresholds are absolute:
    # a probe that matches reads >= 0.28, a note a semitone or a fifth away reads <= 0.04
    m2, m1, m4 = (goertzel(seg, f * k, sr, n) / (rms * n * 32768.0) for k in (0.5, 1.0, 2.0))
    if m2 > 1.3 * m1 and m2 > 0.15:
        return 0.0, "half", midi - 12.0, 0.0               # a real subharmonic: we asked an octave high
    if m1 < 0.10 and m4 > 0.15:
        return 0.0, "twice", midi + 12.0, 0.0              # no fundamental: we asked an octave low
    if max(m2, m1, m4) >= 0.12:
        ct, spread, _ = cluster(seg, sr, f)
        return ct, "", None, spread
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
    c, width, _ = cluster(seg, sr, f * 2.0 ** (best_off / 12.0))
    return c, "other", midi + best_off + c / 100.0, width


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
    return lo + j * step


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
        self.log(f"{c.set}: {len(c.roots)} roots x {len(c.vels)} velocity layers = {n} notes, "
                 f"{c.hold:.2f}s hold + {c.tail:.2f}s tail -> {self.out}")
        t0 = time.monotonic()
        for k, (note, vel, layer, take) in enumerate(items):
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
            clipped = sum(1 for v in x if abs(v) >= 32700)
            cents, octave, heard, detune = check_pitch(x, self.audio.sr, note)
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
                warn.append("not this note and not identifiable (broadband: clicks, noise, a "
                            "metronome)")
            elif octave == "noise":
                warn.append("no pitched content: reverb/delay tail only, or the wrong track")
            dc = sum(x) / max(1, len(x))
            # a recording that is not the note asked for is kept as evidence, under a name the
            # builder's glob never picks up and a rerun never skips: a wrong-pitch zone is worse
            # than a missing one, and silently keeping it is how a bank gets poisoned
            bad = bool(octave)
            if bad:
                name = name[:-4] + "_BAD.wav"
                path = self.out / name
            write_wav(path, x, self.audio.sr)
            self.entries.append(dict(file=name, midi=note, name=note_name(note), velocity=vel,
                                     layer=layer, take=take, rate=self.audio.sr, frames=len(x),
                                     seconds=round(len(x) / self.audio.sr, 4), peak=round(pk / 32768, 5),
                                     peak_dbfs=round(db(pk / 32768), 2),
                                     dc=round(dc / 32768, 5), cents=round(cents, 1),
                                     detune_ct=round(detune, 1),
                                     octave=octave, heard=(note_name(int(round(heard)))
                                                           if heard is not None else ""),
                                     clipped=clipped, warnings=warn))
            self.manifest()
            if clipped:
                self.clipped += 1
                if self.clipped >= 3:
                    raise SystemExit("3 notes in a row are clipped: lower the track/master and rerun "
                                     "(the files already written are fine, the rest will be skipped)")
            self.log(f"{tag}  {len(x) / self.audio.sr:5.2f}s  peak {db(pk / 32768):6.1f} dBFS"
                     f"  {'  '.join(warn) if warn else 'ok'}"
                     + ("  [written as _BAD, not used]" if bad else ""))
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
        ct, width, pk = cluster(sig, sr, f)
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
        ok = pack_check(out) and ok
        print("  selftest:", "ok" if ok else "FAILED")
    return 0 if ok else 1


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
def smf(events, out, division=480, tempo=500000):
    """type 0 SMF from [(seconds, bytes)] -> OUT (a fallback when the virtual port cannot be used)"""
    ev = sorted(events, key=lambda e: e[0])
    ticks = [round(t * division * 1e6 / tempo) for t, _ in ev]
    body = bytearray()
    body += b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big")
    prev = 0
    for tk, (_, data) in zip(ticks, ev):
        d = tk - prev
        prev = tk
        body += _varlen(d) + bytes(data)
    body += b"\x00\xff\x2f\x00"
    with open(out, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, division) + b"MTrk" + struct.pack(">I", len(body)) + body)


def _varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def write_mid(cfg, out):
    events, t = [], 0.0
    for note, vel, _, _ in Plan(cfg.roots, cfg.vels).items():
        events.append((t, [0x90 | cfg.channel, note, vel]))
        events.append((t + cfg.hold, [0x80 | cfg.channel, note, 64]))
        t += cfg.hold + cfg.tail + cfg.gap
    events.append((t, [0xB0 | cfg.channel, 123, 0]))
    smf(events, out)
    print(f"{out}: {len(Plan(cfg.roots, cfg.vels).items())} notes, {t:.1f}s at 120 BPM "
          f"(note lengths are in seconds: set the DAW tempo to 120 or the notes stretch)")


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


FREED_BY_CUTS = 62_844 + 22_050                # FLUTE + SAX + SLICE's BREAK (SPACE_BUDGET.md C1/C1b)


def plan_budget(roots, vels, hold, tail, log=print):
    """what this session will cost, and what of it can ever be played -- before recording 90 notes

    Three numbers, all exact, because the build is a fixed function of length: IMA ADPCM is 4
    bits/sample at 22.05 kHz (11,025 B/s), fp so a zone costs its seconds x 11,025 B. The point of
    printing them before the session is that a 3-layer 30-root piano is 15 minutes of recording and
    then does not fit anything, and it is better to know that first.

    "built" is not the recorded length: a sustaining zone becomes attack + loop (~1 s) and a
    decaying one gets its envelope divided out (~1 s at the same length), so the flash figure uses
    1 s per zone -- the length the shipped sets actually use (PIANO: 0.75 s, 41,349 B for 5 zones).
    """
    secs = hold + tail
    n = len(roots) * len(vels)
    raw = secs * ADPCM_BPS * n
    built = 1.0 * ADPCM_BPS * n
    log(f"  record: {secs:.1f}s per note = {secs * ADPCM_BPS / 1024:.1f} KiB on disk; "
        f"{len(roots)} roots x {len(vels)} layers = {n} notes, {raw / 1024 / 1024:.1f} MiB, "
        f"~{n * (secs + 0.95) / 60:.0f} min")
    log(f"  flash : {n} zones at ~1 s each = {built / 1024:.0f} KiB  |  "
        f"free today {FLASH_BUDGET / 1024:.0f} KiB, +cuts (FLUTE, SAX, BREAK) "
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
          "FLUTE+SAX+BREAK cuts (85 KiB)")
    lim = args.zone
    if worst > lim:
        print(f"  WORST STRETCH +-{worst} st is past what this instrument tolerates (+-{lim}): a "
              f"stretched zone transposes its vibrato/spread with the pitch. Record more roots, "
              f"shorten the zones (--seconds), or accept it lower down the keyboard.")
    if report and max(r[2] for r in report) > 8.0:
        print(f"  the widest zone holds a patch with {max(r[2] for r in report):.0f} ct of partial "
              f"spread (an ensemble or a chorus): it will stretch that by 2^(+-{worst}/12)")
    if args.out:
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
    cfg = argparse.Namespace(set=args.name.upper(), roots=roots, vels=vels, hold=hold,
                             tail=tail,
                             preroll=args.preroll, channel=args.channel, passes=args.passes,
                             skip_existing=not args.overwrite, dry_run=args.dry_run,
                             gap=args.gap)
    out = Path(args.out) if args.out else OWN / args.name.upper()
    if not cfg.dry_run:
        out.mkdir(parents=True, exist_ok=True)
    if preset.get("dry"):
        print(f"  {args.preset}: {preset['dry']}")
    plan_budget(roots, vels, hold, tail)
    if len(roots) > 24 or len(vels) > 5:
        print(f"{len(roots)} roots x {len(vels)} layers: that is a long session. "
              f"Ctrl-C is safe (every note is written as it is recorded, rerun skips them).")
    if args.mid:
        write_mid(cfg, args.mid)
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
        audio = FfmpegIn(i, args.rate, args.preroll, args.verbose,
                         path=Path(args.input) if args.input else None)
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
    cfg = argparse.Namespace(set=args.name, roots=roots, vels=vels, hold=hold, tail=tail,
                             gap=args.gap, channel=args.channel, passes=args.passes)
    write_mid(cfg, Path(args.out))
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
    p.add_argument("--overwrite", action="store_true", help="re-record files that exist")
    p.add_argument("--dry-run", action="store_true", help="print the plan, record nothing")
    p.add_argument("--mid", help="also write the session as a MIDI file")
    p.add_argument("--input", help="read a file instead of a device (it plays in real time, "
                                   "no MIDI is sent): for checking the capture path")
    p.add_argument("--midi-name", default="StudioKEYS Autosampler", help="the virtual port's name")
    p.add_argument("--midi-out", help="send to an existing MIDI destination instead of creating a "
                                      "virtual port (e.g. 'IAC Driver Bus 1')")
    p.add_argument("--verbose", action="store_true")
    p.set_defaults(func=cmd_session)

    p = sub.add_parser("mid", help="write the session as a MIDI file only")
    p.add_argument("name")
    p.add_argument("out")
    common(p)
    p.add_argument("--gap", type=float, default=0.5)
    p.set_defaults(func=cmd_mid)

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
