# SPDX-License-Identifier: GPL-3.0-only
"""Ableton v3 mappings for StudioKEYS Micro (cable StudioKEYS Live)."""


def create_mappings(_cs):
    session = dict(
        component='Session',
        clip_launch_buttons='clip_launch_buttons',
        stop_all_clips_button='stop_all_clips_button',
    )
    drums = dict(component='Drum_Group', matrix='drum_pads')
    device = dict(component='Device', parameter_controls='device_controls')
    mixer_extra = dict(
        component='Mixer',
        pan_controls='pan_controls',
        send_controls='send_controls',
        mute_buttons='mute_buttons',
        solo_buttons='solo_buttons',
        arm_buttons='arm_buttons',
        track_select_buttons='track_select_buttons',
        volume_controls='volume_controls',
    )
    return {
        'Transport': dict(
            play_button='play_button',
            stop_button='stop_button',
            session_record_button='record_button',
            metronome_button='metronome_button',
            capture_midi_button='capture_midi_button',
        ),
        'Session_Navigation': dict(
            left_button='track_bank_left_button',
            right_button='track_bank_right_button',
            up_button='scene_bank_up_button',
            down_button='scene_bank_down_button',
        ),
        'Mixer': dict(volume_controls='volume_controls'),
        'Pad_Modes': dict(
            cycle_mode_button='pad_face_button',
            clips=dict(modes=[session]),
            drums=dict(modes=[drums]),
        ),
        'Face_Modes': dict(
            cycle_mode_button='face_button',
            session=dict(modes=[device, 'Pad_Modes']),
            mixer=dict(modes=[mixer_extra]),
        ),
    }
