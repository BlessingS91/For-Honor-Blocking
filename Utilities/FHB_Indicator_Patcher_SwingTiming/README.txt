FHB EARLY INDICATOR PATCHER - DESKTOP GUI + OPTIONAL SWING TIMING
==============================================================

PURPOSE
-------
Original feature (unchanged when swing alignment is OFF):
    weaponSwing     -> indicatorStart       (configurable earlier by X ms)
    weaponLeftSwing -> LeftIndicatorStart   (configurable earlier by Y ms)

New optional feature: retime existing weaponSwing and weaponLeftSwing annotations
relative to existing HitFrame annotations in the SAME HKX animation. With all
three lead values set to 200 ms, the result is:

    0.400  indicatorStart       [UI telegraph begins]
    0.600  weaponSwing          [UI receives its HitFrame phase here]
    0.800  HitFrame             [existing HKX hit annotation]

The UI callback phase called "HitFrame" is separate from the actual HKX
annotation called HitFrame. This editor locates *the HKX annotation*, and
never adds or retimes the HitFrame annotation itself.

LEFT / RIGHT PAIRING
--------------------
HitFrame annotations do not identify which weapon hand they belong to.
For each HKX file, the tool sorts BOTH types of swing together by their
ORIGINAL timestamp. It also sorts the HitFrame annotations by timestamp.
Then it pairs the 1st swing with the 1st hit, the 2nd swing with the 2nd hit,
and so on. For identical timestamps, original annotation order breaks ties.

Example: original swing order is Left, Right; hit order is HitFrame #1,
HitFrame #2. Left is positioned before hit #1; Right before hit #2.

If there is not exactly one HitFrame for every swing, the file reports an
ERROR during preview and is not edited. Review those animations manually.
Files containing neither hits nor swings remain unchanged.

SETUP (WINDOWS)
---------------
1. Install Python 3.10 or newer. Check `py -3 --version`.
2. Install HKXC Anno CLI plus hkxc.exe (dependency):
   https://www.nexusmods.com/skyrimspecialedition/mods/166424
3. Put hkxc.exe next to hkxc-anno-cli.exe (or add it to PATH).
4. Extract the complete ZIP to a folder.
5. Double-click Launch_GUI.bat (or run `py -3 fhb_indicator_gui.py`).
6. Select source HKX folder, separate output folder, and hkxc-anno-cli.exe.
7. Set right and left indicator lead (e.g. 200 ms each).
8. OPTIONAL: tick "Standardize swings before HitFrame" and set Swing lead,
   e.g. 200 ms. Leave it unchecked to keep the original behavior.
9. Preview changes; review swing_move / hits / conflicts / clamped / errors.
10. Click Patch HKX files to produce the output.

GUI CONTROLS
------------
Right-hand lead:    amount before a right weaponSwing for indicatorStart.
Left-hand lead:     amount before a left weaponLeftSwing for LeftIndicatorStart.
Standardize:       optional alignment of all swings to paired HitFrame events.
Swing lead:        amount before each paired HitFrame for either swing type.
Advanced events:   customize swing, indicator, and HitFrame annotation names.
Replace existing:  reposition one existing indicator if it belongs to a
                   swing but no longer matches desired timing. Keep off to
                   preserve hand-authored indicators; conflicts are reported.
Overwrite:         overwrite files already in output folder, NEVER source.
Verify:            re-dump each produced HKX to check its new event timestamps.
Stop:              finish the current file safely, then stop.

REPATCHING OLD OUTPUTS
----------------------
If the input HKX already contains indicatorStart or LeftIndicatorStart, and
you move a weaponSwing, its indicator may need to move too. Tick
"Replace existing indicator timings" to bring that one indicator to the new
lead. Otherwise, conflicting hand-authored indicators are preserved and the
preview reports conflicts (no duplicate indicator added).

SAFETY / LIMITATIONS
--------------------
- Input HKXs are never modified; output HKXs retain relative directory paths.
- Output must not be nested inside input, or vice versa.
- Preview is a dry run and does not write HKX files.
- Overwriting output and replacing indicator timings are OFF by default.
- Swing standardization is OFF by default, preserving old behavior.
- Output re-dump verification remains ON by default.
- Lead times are animation-local milliseconds, not real-world milliseconds
  after MCO speed modifiers.
- Times earlier than 0 are clamped to 0 and reported; a full lead is not
  possible for those early hit annotations.
- Original HitFrame annotations are never modified.
- Nothing in this program injects graph events. Ensure your behavior setup
  recognizes indicatorStart / LeftIndicatorStart where necessary.
- Some movesets have different hit annotation names. Use the Advanced
  Hit annotation field when the source file uses a different name.
- Only a single annotation track per HKX is supported safely, as before.

CLI EXAMPLE
-----------
py -3 fhb_indicator_patcher.py --input "C:\\Moveset" --output "C:\\Moveset_FHB" \
    --tool "C:\\Tools\\hkxc-anno-cli.exe" --right-lead-ms 200 --left-lead-ms 200 \
    --align-swings --swing-before-hit-ms 200

By default the CLI previews. Add --write to generate output HKX files.
Use --hit-trigger to choose another hit annotation name (default HitFrame).

REQUIREMENTS
------------
The GUI and Python patcher use only Python's standard library. HKXC Anno CLI
plus companion hkxc.exe are required for real HKX binary processing.
Those third-party tools are not included in the ZIP.
