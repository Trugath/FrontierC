# Historical setup messages

`messages.json` supplies the seven named messages/device strings used by the
preserved setup checker. `frontier-data unpack` writes it from `EL2SETUP.OVL`.
JSON escapes encode CR/LF and other control bytes.
The DOS `$` and NUL terminators are removed at the native C boundary.

The native game host does not perform DOS/EMS hardware detection. The original
checker is retained as a standalone C port and tested with platform callbacks.
`tools/optional_tables_c.py` generates its C string declarations.
