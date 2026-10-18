# Navigation source resources

Three escaped text fragments retain printable text and original FF controls.
The compiler adds no NUL: assembly supplies the time-point terminator, while
the target prompt terminates in the first zero byte of drawing_parameters.
Text fragments compile as byte streams, not independent NUL-terminated strings.
The relative two-entry directory remains symbolic assembly.

Drawing parameters are seven unsigned words; marker shape is twenty bytes.
The radius lookup aliases shape+2 (eighteen bytes). Index 18 can reach the
following RETF opcode CB; original descriptor values 288..300 allow that read.
Do not append a terminator, pad, clamp or separate the emitted blocks.
All native files compile through intermediate JSON; no overlay input is needed.
