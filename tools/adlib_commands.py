"""Complete physical AdLib command grammar (80h..C9h).

Widths describe meaningful stream bytes, not the scheduler's unconditional
two-byte prefetch. Termination may leave an unused prefetched byte. Opcodes
CAh..F0h index beyond the table in the original driver and are not assigned
invented commands; F1h..FFh terminate through its explicit range check.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Command:
    name: str
    operands: tuple[str, ...] = ()

    @property
    def width(self):
        return 1 + len(self.operands)


COMMANDS = {opcode: Command('stop_channel') for opcode in range(0x80, 0xca)}


def command(opcode, name, *operands):
    COMMANDS[opcode] = Command(name, operands)


command(0x80, 'set_loop_count', 'count')
command(0x81, 'loop_end', 'target_low', 'target_high')
command(0x82, 'start_event', 'event')
command(0x83, 'set_release_threshold', 'threshold')
command(0x84, 'jump', 'target_low', 'target_high')
command(0x85, 'call', 'target_low', 'target_high')
command(0x86, 'return')
command(0x87, 'set_octave_bias', 'bias')
command(0x89, 'rest', 'duration')
command(0x8a, 'write_opl', 'register', 'value')
command(0x8b, 'set_pitch_duration', 'pitch', 'duration')
command(0x8c, 'set_note_bias', 'bias')
command(0x8d, 'configure_register_sweep', 'rate', 'count', 'register', 'table_low', 'table_high')
command(0x8e, 'deactivate_channel', 'channel')
command(0x90, 'load_instrument', 'instrument')
command(0x91, 'configure_pitch_slide', 'rate', 'step_high', 'step_low')
command(0x92, 'disable_pitch_slide')
command(0x93, 'set_frequency_bias', 'bias')
command(0x95, 'configure_pitch_modulation', 'rate', 'shift', 'count', 'delay')
command(0x9a, 'set_priority', 'priority')
command(0x9c, 'reset_global_gate', 'period')
command(0x9d, 'wait_global_gate', 'mask')
command(0x9e, 'set_level', 'level')
command(0xa0, 'delay', 'duration')
command(0xa1, 'retrigger_duration', 'duration')
command(0xa4, 'set_release_fraction', 'release_fraction')
command(0xa6, 'set_global_tempo', 'tempo')
command(0xa7, 'disable_register_sweep')
command(0xa9, 'set_channel_tempo', 'tempo')
command(0xab, 'set_level_bias', 'bias')
command(0xac, 'set_other_channel_level', 'channel', 'level')
command(0xad, 'add_other_channel_level', 'channel', 'delta')
command(0xae, 'set_bd_bit_seven', 'enabled')
command(0xaf, 'set_bd_bit_six', 'enabled')
command(0xb0, 'add_level', 'delta')
command(0xb3, 'clear_channel', 'channel')
command(0xb5, 'random_frequency_offset', 'mask_high', 'mask_low')
command(0xb6, 'disable_pitch_callback')
command(0xb9, 'apply_pitch_bend', 'bend')
command(0xba, 'use_global_tempo')
command(0xbb, 'noop')
command(0xbc, 'set_duration_mode', 'random_mask')
command(0xbd, 'adjust_channel_tempo', 'delta')
for opcode in (0xbf, 0xc0, 0xc1):
    command(opcode, 'setup_rhythm', 'bass_instrument', 'snare_hihat_instrument',
            'tom_cymbal_instrument', 'b6', 'a6', 'b7', 'a7', 'b8', 'a8')
command(0xc2, 'trigger_rhythm', 'mask')
command(0xc3, 'disable_rhythm')
command(0xc4, 'set_rhythm_bias', 'mask', 'bias')
command(0xc5, 'add_rhythm_level', 'mask', 'delta')
command(0xc6, 'set_rhythm_level', 'mask', 'level')
command(0xc8, 'follow_global_tempo', 'enabled')
command(0xc9, 'set_operator_level_modulation', 'modulator_scale', 'carrier_scale')


def note_width(channel):
    """Overlapping original word tests require byte 34h as well as 32h/33h."""
    return 3 if any(channel[0x32:0x35]) else 2
