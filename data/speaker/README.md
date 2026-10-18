# PC speaker resources

Edit `scripts.json` for the nine effects and ten reusable patterns.
`scripts.schema.json` supplies standard JSON Schema completion and validation
for editors. The compiler also validates symbolic references, loop depth,
terminators and the native command-byte restrictions.

An effect's `note` command refers to a pattern by name:

```json
{"op": "note", "pattern": "pattern_steady", "note": 200, "ticks": 5}
```

`note: "keep"` retains the current note. A duration of zero holds until the
pattern finishes the note. Effects can contain `pause` commands and must finish
with `{"op": "end"}`. Durations count driver updates rather than milliseconds.

Patterns contain `wait`, `pitch_delta`, `noise_on`, `noise_off`, and `loop`
commands, followed by `finish_note`. Loops contain their commands directly:

```json
{
  "op": "loop",
  "count": 4,
  "commands": [
    {"op": "pitch_delta", "steps": -3},
    {"op": "wait", "ticks": 2}
  ]
}
```

Loop counts are 1..256; the compiler generates the zero-byte encoding of 256.
At most four loops can nest. Each loop must contain a wait or pitch step,
directly or in a nested loop, so it yields to the driver. Pitch steps are signed
note-index changes. Six
values overlap native opcodes and are rejected rather than silently changing
the command. Pattern names are unique and effects resolve them at build time;
there are no manually maintained numeric pattern pointers.

`game_effects.csv` maps each of the 50 game-effect calls to a script in
`scripts.json`, or `none`. `level_durations.csv` is how many driver updates
each loudness step lasts. `pitch_divisors.csv` is note 0..255 and the PIT
channel-2 divisor; the tone is 1193182 / divisor hertz. A divisor below 2 is
silence.

`tools/optional_tables_c.py` compiles these resources for the standalone native
audio tests. Run `python test.py` after edits. Migration fixtures preserve the
original generated C tables and require review for intentional sound changes.
