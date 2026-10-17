# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
from ableton.v3.control_surface import ElementsBase, MapMode
from . import midi as M


class Elements(ElementsBase):

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.add_button(M.PLAY, 'Play_Button')
        self.add_button(M.STOP, 'Stop_Button')
        self.add_button(M.REC, 'Record_Button')
        self.add_button(M.METRO, 'Metronome_Button')
        self.add_button(M.CAPTURE, 'Capture_Midi_Button')
        self.add_button(M.STOP_ALL, 'Stop_All_Clips_Button')
        self.add_button(M.BANK_M, 'Track_Bank_Left_Button')
        self.add_button(M.BANK_P, 'Track_Bank_Right_Button')
        self.add_button(M.SCENE_M, 'Scene_Bank_Up_Button')
        self.add_button(M.SCENE_P, 'Scene_Bank_Down_Button')
        self.add_button(M.FACE, 'Face_Button')
        self.add_button(M.PADFACE, 'Pad_Face_Button')
        self.add_button(M.KEYS, 'Keys_Route_Button')

        self.add_button_matrix(
            [[M.PAD0 + c + r * M.NUM_TRACKS for c in range(M.NUM_TRACKS)] for r in range(M.NUM_SCENES)],
            'Clip_Launch_Buttons',
        )
        self.add_button_matrix(
            [[M.PAD0 + i for i in range(8)]],
            'Drum_Pads',
        )
        self.add_button_matrix([[M.MUTE0 + i for i in range(M.NUM_TRACKS)]], 'Mute_Buttons')
        self.add_button_matrix([[M.SOLO0 + i for i in range(M.NUM_TRACKS)]], 'Solo_Buttons')
        self.add_button_matrix([[M.ARM0 + i for i in range(M.NUM_TRACKS)]], 'Arm_Buttons')
        self.add_button_matrix([[M.SEL0 + i for i in range(M.NUM_TRACKS)]], 'Track_Select_Buttons')

        self.add_encoder_matrix(
            [[M.CC_KNOB0 + i for i in range(8)]],
            'Device_Controls',
            map_mode=MapMode.LinearBinaryOffset,
        )
        self.add_encoder_matrix(
            [[M.CC_KNOB0 + i for i in range(M.NUM_TRACKS)]],
            'Pan_Controls',
            map_mode=MapMode.LinearBinaryOffset,
        )
        self.add_encoder_matrix(
            [[M.CC_KNOB0 + 4 + i for i in range(M.NUM_TRACKS)]],
            'Send_Controls',
            map_mode=MapMode.LinearBinaryOffset,
        )
        self.add_encoder_matrix(
            [[M.CC_FADER0 + i for i in range(M.NUM_TRACKS)]],
            'Volume_Controls',
            map_mode=MapMode.Absolute,
        )
