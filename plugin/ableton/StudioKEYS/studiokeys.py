# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Ableton Live remote script for StudioKEYS Micro (_Framework).

Protocol matches firmware/src/sk_live.c — custom StudioKEYS Live map.
"""
from __future__ import absolute_import, print_function, unicode_literals

import Live
from _Framework.ControlSurface import ControlSurface
from _Framework.ButtonElement import ButtonElement
from _Framework.EncoderElement import EncoderElement
from _Framework.SliderElement import SliderElement
from _Framework.InputControlElement import MIDI_NOTE_TYPE, MIDI_CC_TYPE
from _Framework.SessionComponent import SessionComponent
from _Framework.MixerComponent import MixerComponent
from _Framework.TransportComponent import TransportComponent
from _Framework.DeviceComponent import DeviceComponent
from _Framework.DrumGroupComponent import DrumGroupComponent
from _Framework.ButtonMatrixElement import ButtonMatrixElement

from . import midi as M

SYSEX_HDR = (0xF0, 0x7D, 0x53, 0x4B, 0x4D)


class StudioKEYS(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        with self.component_guard():
            self._face = 0  # 0 session, 1 mixer
            self._pad_face = 0  # 0 clips, 1 drums
            self._keys_to_daw = False
            self._suggested_input_port = "StudioKEYS Live"
            self._suggested_output_port = "StudioKEYS Live"

            self._session = SessionComponent(M.NUM_TRACKS, M.NUM_SCENES)
            self._session.name = "Session"
            self._mixer = MixerComponent(M.NUM_TRACKS)
            self._mixer.name = "Mixer"
            self._transport = TransportComponent()
            self._device = DeviceComponent()
            self._device.name = "Device"
            self.set_device_component(self._device)
            self._drums = DrumGroupComponent()
            self._drums.name = "Drums"

            self._build_controls()
            self.set_highlighting_session_component(self._session)
            self._apply_face()
            self._apply_pads()
            self.show_message("StudioKEYS Micro")
            self.song().add_is_playing_listener(self._push_state)
            self.song().add_record_mode_listener(self._push_state)
            self.song().add_metronome_listener(self._push_state)
            self._push_state()
            self._push_names()

    def _btn(self, note):
        return ButtonElement(True, MIDI_NOTE_TYPE, 0, note)

    def _rel(self, cc):
        # Firmware sends 64+Δ (same as MIDI CTRL knobs).
        return EncoderElement(
            MIDI_CC_TYPE, 0, cc, Live.MidiMap.MapMode.relative_smooth_binary_offset
        )

    def _abs(self, cc):
        return SliderElement(MIDI_CC_TYPE, 0, cc)

    def _build_controls(self):
        pad_buttons = [self._btn(M.PAD0 + i) for i in range(8)]
        self._clip_matrix = ButtonMatrixElement(
            rows=[pad_buttons[0:4], pad_buttons[4:8]]
        )
        self._drum_matrix = ButtonMatrixElement(rows=[pad_buttons])

        self._session.set_scene_bank_buttons(self._btn(M.SCENE_M), self._btn(M.SCENE_P))
        self._session.set_track_bank_buttons(self._btn(M.BANK_M), self._btn(M.BANK_P))
        self._session.set_stop_all_clips_button(self._btn(M.STOP_ALL))

        self._mute_btns = [self._btn(M.MUTE0 + i) for i in range(4)]
        self._solo_btns = [self._btn(M.SOLO0 + i) for i in range(4)]
        self._arm_btns = [self._btn(M.ARM0 + i) for i in range(4)]
        self._sel_btns = [self._btn(M.SEL0 + i) for i in range(4)]
        self._volumes = [self._abs(M.CC_FADER0 + i) for i in range(4)]
        self._pans = [self._rel(M.CC_KNOB0 + i) for i in range(4)]
        self._sends = [self._rel(M.CC_KNOB0 + 4 + i) for i in range(4)]
        self._macros = [self._rel(M.CC_KNOB0 + i) for i in range(8)]

        for i in range(4):
            strip = self._mixer.channel_strip(i)
            strip.set_mute_button(self._mute_btns[i])
            strip.set_solo_button(self._solo_btns[i])
            strip.set_arm_button(self._arm_btns[i])
            strip.set_select_button(self._sel_btns[i])
            strip.set_volume_control(self._volumes[i])

        self._transport.set_play_button(self._btn(M.PLAY))
        self._transport.set_stop_button(self._btn(M.STOP))
        self._transport.set_record_button(self._btn(M.REC))
        self._transport.set_metronome_button(self._btn(M.METRO))

        self._face_btn = self._btn(M.FACE)
        self._face_btn.add_value_listener(self._on_face)
        self._padface_btn = self._btn(M.PADFACE)
        self._padface_btn.add_value_listener(self._on_pad_face)
        self._keys_btn = self._btn(M.KEYS)
        self._keys_btn.add_value_listener(self._on_keys)
        self._capture_btn = self._btn(M.CAPTURE)
        self._capture_btn.add_value_listener(self._on_capture)

        try:
            self._session.add_offset_listener(self._on_offset)
        except Exception:
            pass

    def _on_offset(self, *_a):
        self._push_names()
        self._push_state()

    def _apply_pads(self):
        if self._pad_face:
            self._session.set_clip_launch_buttons(None)
            self._drums.set_drum_group_device(self._find_drum_device())
            try:
                self._drums.set_matrix(self._drum_matrix)
            except Exception:
                pass
        else:
            try:
                self._drums.set_matrix(None)
            except Exception:
                pass
            self._session.set_clip_launch_buttons(self._clip_matrix)

    def _find_drum_device(self):
        track = self.song().view.selected_track
        if track is None:
            return None
        for d in track.devices:
            if d.can_have_drum_pads:
                return d
        return None

    def _apply_face(self):
        if self._face:
            self._device.set_parameter_controls(None)
            for i in range(4):
                strip = self._mixer.channel_strip(i)
                strip.set_pan_control(self._pans[i])
                try:
                    strip.set_send_controls((self._sends[i],))
                except Exception:
                    pass
        else:
            for i in range(4):
                strip = self._mixer.channel_strip(i)
                strip.set_pan_control(None)
                try:
                    strip.set_send_controls(None)
                except Exception:
                    pass
            self._device.set_parameter_controls(tuple(self._macros))

    def _on_face(self, value):
        if value:
            self._face ^= 1
            self._apply_face()
            self.show_message("MIXER" if self._face else "SESSION")
            self._push_state()

    def _on_pad_face(self, value):
        if value:
            self._pad_face ^= 1
            self._apply_pads()
            self.show_message("DRUM PADS" if self._pad_face else "CLIP PADS")
            self._push_state()

    def _on_keys(self, value):
        if value:
            self._keys_to_daw = not self._keys_to_daw
            self.show_message("KEYS -> LIVE" if self._keys_to_daw else "KEYS -> SYNTH")
            self._push_state()

    def _on_capture(self, value):
        if value and hasattr(self.song(), "capture_midi"):
            try:
                self.song().capture_midi()
            except Exception:
                pass

    def _push_state(self, *_a):
        song = self.song()
        bank = 0
        try:
            bank = int(self._session.track_offset())
        except Exception:
            bank = 0
        payload = (
            0x01,
            self._face & 1,
            self._pad_face & 1,
            bank & 0x7F,
            1 if self._keys_to_daw else 0,
            1 if song.is_playing else 0,
            1 if song.record_mode else 0,
            1 if song.metronome else 0,
        )
        self._send_midi(SYSEX_HDR + payload + (0xF7,))

    def _push_names(self, *_a):
        tracks = self.song().visible_tracks
        try:
            offset = int(self._session.track_offset())
        except Exception:
            offset = 0
        for i in range(M.NUM_TRACKS):
            name = "----"
            idx = offset + i
            if idx < len(tracks):
                name = (tracks[idx].name or "")[:8]
            raw = tuple(ord(c) & 0x7F for c in name.ljust(8)[:8])
            self._send_midi(SYSEX_HDR + (0x02, i) + raw + (0xF7,))
