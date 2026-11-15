# font_resources: evidence

Area: `AddFontResource{A,W,ExA,ExW}`, `AddFontMemResourceEx`, `RemoveFontResource{A,W,ExA,ExW}`, `RemoveFontMemResourceEx`,
`CreateScalableFontResource{A,W}`, `GetFontResourceInfoW`, `GetFontFileData`. Source: `tests/font_resources/font_resources.c`.

Oracle: Windows 10 22H2 (19045.2965) guest, run through `tools/vm_test.sh font_resources`. Final runs: two separate boots with the
final build (runs 3 and 4), both `PASS tests=11 failed=0 skipped=0 checks=99/99`, 24 NOTE lines each, logs identical after masking
the per-process face-name suffix and scratch directory number. Two earlier boots of earlier revisions of the test (7 and 1 failing
tests) were used to correct the test (see "Oracle corrections").

Font data: no third-party font is used. The test writes a generated two-glyph TrueType font (10 tables, name table family = a per-process unique
name `TstFR<pid-hex><letter>`) to a scratch directory, and deletes it at exit. Faces are private (`FR_PRIVATE`) except the explicit
system-wide arms, which remove what they add. "Face present" means both `EnumFontFamiliesExW` filtered on the name returns an entry
and `GetTextFaceW` after `CreateFontW(name)` returns that name; "absent" means neither.

Source codes: D = Microsoft documentation, O = observed on Win10 22H2 19045.2965 (stable in the three final-build runs: runs 3 and 4,
and a review run 5 on the reviewed revision; runs 1 and 2 were development runs of earlier revisions), C = decompilation of gdi32full (22H2).
D urls: `https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-<lowercase export name>`.

## Asserted expectations

| # | Test | Expectation | Source | Stability (final-build boots) |
|---|------|-------------|--------|-----------|
| 1 | fr_addex_private_basic | before adding, face is absent | D (face is not installed) | 3/3 final-build runs |
| 2 | fr_addex_private_basic | `AddFontResourceExW(path, FR_PRIVATE)` returns 1 for a one-face file | D addfontresourceexw | 3/3 final-build runs |
| 3 | fr_addex_private_basic | face enumerated and selectable (`GetTextFace` == name) in this process | D | 3/3 final-build runs |
| 4 | fr_addex_private_basic | `RemoveFontResourceExW` returns TRUE once, FALSE the second time | D removefontresourceexw; O | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 5 | fr_addex_private_basic | after removal: not enumerated, `GetTextFace` differs from name | D, O | 3/3 final-build runs |
| 6 | fr_not_enum | `FR_PRIVATE|FR_NOT_ENUM`: returns 1, selectable, enumerates 0 entries; removal TRUE; then unselectable | D (FR_NOT_ENUM) | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 7 | fr_refcount | add twice (1,1), remove once TRUE and face still present, remove again TRUE and face gone, third remove FALSE | D ("as many times as added"), O | 3/3 final-build runs |
| 8 | fr_ex_ansi_wide_mix | ExA and ExW return equal counts and equal enumerated entry counts for an ASCII path; RemoveExW removes an ExA add and RemoveExA removes an ExW add; extra remove FALSE | D (A/W pair), O | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 9 | fr_private_vs_global_xproc | with `FR_PRIVATE` a child process (same exe) finds the face neither enumerated nor selectable (child exit 0) | D (FR_PRIVATE) | 3/3 final-build runs |
| 10 | fr_private_vs_global_xproc | with flags 0, add returns 1 and the child finds it enumerated and selectable (exit 3); after remove TRUE the child sees neither (exit 0) | D (system-wide) | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 11 | fr_addfontresource_global_aw | `AddFontResourceA` and `AddFontResourceW` return 1 with the same enumerated count; `RemoveFontResourceA/W` TRUE, second FALSE; A-add/W-remove and W-add/A-remove work | D | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 12 | fr_nonexistent_and_bad_args | `AddFontResourceExW` on a missing path returns 0; `RemoveFontResourceExW` for a never-added path returns FALSE | D (returns 0 / FALSE on failure), O | 3/3 final-build runs |
| 13 | fr_nonexistent_and_bad_args | `AddFontMemResourceEx` with cbFont 0 returns NULL | O | 3/3 final-build runs |
| 14 | fr_nonexistent_and_bad_args | 4-byte junk file: `AddFontResourceExW` returns 0 | O (see 12 for what the doc says) | 3/3 final-build runs |
| 15 | fr_mem_resource | `AddFontMemResourceEx` returns non-NULL handle with `*pcFonts == 1`; face selectable | D addfontmemresourceex | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 16 | fr_mem_resource | memory font is private: a child sees it neither enumerated nor selectable | D (private to process) | 3/3 final-build runs |
| 17 | fr_mem_resource | `RemoveFontMemResourceEx` TRUE, face gone, second call with same handle FALSE | D removefontmemresourceex, O | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 18 | fr_mem_resource | two adds of the same bytes give two different handles, each `pcFonts == 1`; removing one leaves the face, removing the other removes it | O | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 19 | fr_getfontresourceinfo | `GetFontResourceInfoW(path, QFR_DESCRIPTION)` returns TRUE with the string equal to the face name | O; C (calls `NtGdiGetFontResourceInfoInternalW`) | 3/3 final-build runs |
| 20 | fr_getfontresourceinfo | `QFR_LOGFONT` returns TRUE and `lfFaceName` equals the face | O, C | 3/3 final-build runs |
| 21 | fr_getfontresourceinfo | `path == NULL` returns FALSE | C (decompiled: returns 0 first) | 3/3 final-build runs |
| 22 | fr_createscalablefontresource | `CreateScalableFontResourceW/A` returns TRUE and the .fot exists for a TTF with a full path | D createscalablefontresourcew; O | 3/3 final-build runs |
| 23 | fr_createscalablefontresource | `AddFontResourceExW(.fot, FR_PRIVATE)` returns 1 and the face is enumerated and selectable (non-hidden .fot) | O | 3/3 final-build runs; earlier revision failed in development run 1 (test bug, see Oracle corrections) |
| 24 | fr_createscalablefontresource | `fHidden = TRUE` .fot: add returns 1, face not enumerated | D (fHidden: font hidden) | 3/3 final-build runs; earlier revision failed in development run 2 (test bug, see Oracle corrections) |
| 25 | fr_getfontfiledata_observe | `GetFontFileData` with an invalid file id returns FALSE | O, C (returns `NtGdiGetFontFileData` error == 0) | 3/3 final-build runs |

## Observed, reported with NOTE only (not asserted)

- `GetLastError` after `AddFontResourceExW` on a nonexistent path: 2 (ERROR_FILE_NOT_FOUND); after `RemoveFontResourceExW` of a path never added: 2; after the second remove of an added font: 0; after `AddFontMemResourceEx` with cbFont 0: 87; after a second `RemoveFontMemResourceEx`: 0; after `GetFontFileData` with a bad id: 87.
- `AddFontResourceExW` on a 4-byte junk file returns 0 with `GetLastError` 0.
- Fallback face of an absent name after removal is "Arial" (SKU/installed-font dependent).
- A memory font from `AddFontMemResourceEx` is selectable but not returned by `EnumFontFamiliesExW` (0 entries). Undocumented.
- `GetFontResourceInfoW` QFR_DESCRIPTION: size query (`*pcb` = 0, buffer NULL) returns TRUE and the byte count including the NUL (2*(len+1)); the text is the face (family) name for the generated font; QFR_LOGFONT writes `*pcb` = 92 (sizeof LOGFONTW) with lfHeight -24, lfWeight 400, charset 0.
- `.fot` created with (file name, directory) stores only the file name; `AddFontResourceExW` of that .fot fails (returns 0, gle 2) when the TTF is not in the system font directory or current directory. A .fot created from a full path stores the path (8.3 short form) and loads. `CreateScalableFontResource` on an existing target fails with gle 80 (ERROR_FILE_EXISTS), as documented.
- A hidden .fot (fHidden TRUE) is neither enumerated nor selectable once added.
- Child exit for the system-wide arm is 3 (enumerated and selectable).

## Oracle corrections

1. In the first run the tests failed because `EXPECT_EQ` evaluates its arguments twice, so the removal call ran twice; the area now uses a local single-evaluation `EXPECT_EQ`. This was a test bug, not an oracle difference.
2. First run: `AddFontMemResourceEx` fonts were assumed enumerable (as `FR_PRIVATE` file fonts are); the oracle does not enumerate them. The test now reports the count and asserts only selectability.
3. First run: a .fot built with (file name + directory) was assumed loadable; on the oracle it is not (see NOTE above). The arm is observation only; the full-path variant is asserted.
4. Hidden .fot: assumed selectable; the oracle also makes it unselectable. Only "not enumerated" (documented) is asserted.

## Skips

None on the oracle. `REQUIRE` skips a test if an export cannot be resolved; `fr_createscalablefontresource` skips when `CreateScalableFontResourceW` fails
for variant 0 on a system where the obsolete API is unavailable (it succeeded on 19045.2965).

## Not implemented (destructive or special-setup ideas for a `dx_font_install` area)

- Enumeration after many adds; install of more than 1000 fonts; many `AddFontMemResourceEx` handles; font-resource handle exhaustion.
- Corrupt or truncated TrueType files (bad checksums, short tables, table offsets past EOF), oversized or overlapping tables, zero glyphs.
- `AddFontMemResourceEx` leaks across process kill (a child adds many memory fonts and is killed; a later face count must return to baseline).
- `WM_FONTCHANGE` broadcast from the system-wide `AddFontResource` family (needs a top-level window and a message-wait harness, and perturbs session state).
- `AddFontResource` with a font in `%WINDIR%\Fonts` by bare file name, and `AddFontResourceTracking` / `RemoveFontResourceTracking` / `GdiAddFontResourceW`.
- `GetFontFileInfo` / positive `GetFontFileData` (needs a valid file-collection id, which is only obtained from the DirectWrite font collection path).
- Multi-face files (TTC) and `AddFontResource` return values greater than 1; Type 1, OpenType CFF and bitmap (.fon) files; non-ASCII paths for the A/W comparison; a second user session for the system-wide arm.
