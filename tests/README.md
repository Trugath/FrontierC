# Test suite

Run `python test.py --integration` from the repository root. The default
`python test.py` runs the Python tests and native C unit executables;
`--python-only` runs the authoring/tooling suite without requiring Zig.

The suite contains:

- Resource grammar, encoding and invalid-input tests, including every unpacked
  model program and geometry source, all typed table manifests, all native C
  table generators, and concurrent resource publication. The model round-trip
  reads 353 local `.model` files under `data/`. Unpack a Frontier install
  before running it. Sprite, font, music, description, dialogue, and setup-message
  checks read the same local unpack.
- Data migration fixtures that pin the compiled model banks, pitch tables,
  dispatch tables and full systems image. Intentional game-data changes need
  corresponding fixture review rather than silently replacing baselines.
- Native C tests for mining slot limits, hopper boundaries, recovery and record
  compaction; catalogue lookup and fallback boundaries; normalized scalar
  round trips and integer quirks; keyboard decoding, pointer-region capacity,
  and wrapped system-record storage.
- Standalone native checks for PC speaker effects, all supported Roland effect
  selections and music tracks, and setup-check failure paths and sound choices.
- Headless gameplay checks for starts, flight, contacts, police, navigation,
  views, scanner/HUD, trading, fitting, ship purchases, missions, jumps,
  commander save/load, preferences and renderer state. One check runs a
  trading, mission, flight and mining loop and locks the assembly defects
  those paths still have. A separate model-viewer check verifies
  deterministic frame rendering and textured rendering.
- A naming regression audit and a compilation inventory that rejects retained
  C sources omitted from the native build/test targets.

Native test executables explicitly enable C assertions. Game libraries compile
with `-Wall -Wextra -Werror -pedantic`. The Windows window-input test requires
`FE2_WINDOW_TESTS=1`; ordinary integration runs are headless.

These checks are not a measured claim of 100% line or branch coverage.
Validation in this workspace was run on Windows; Linux execution has not been
verified here.
