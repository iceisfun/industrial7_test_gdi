# tests/font_enum_order: evidence

Area: font enumeration (`EnumFontFamilies*`, `EnumFonts*`), font creation round trips, `SetMapperFlags`, and the undocumented
face-alias / font-link / association exports. Built into `build/font_enum_order.exe`; run with `tools/vm_test.sh font_enum_order`.

Oracle: Windows 10 22H2, 10.0.19045.2965, en-US (ACP 1252), 96 DPI, standard font inventory (as in `data/win10/font_inventory/dpi96.rec.gz`),
Administrator session. Boots that agree: four fresh boots (one exploratory dump boot, then three full runs of the test). The first full run
failed three tests because of test bugs only (a macro evaluating a state-changing call twice, a wrong buffer-length constant, a `memcmp` over
uninitialised tail bytes of `lfFaceName`); those were corrected and the two later runs are byte-identical (`font_enum_order.log` diffs to
nothing: 24 tests, 476 checks, 0 failed, 0 skipped, 48 NOTE lines). The enumeration-order and listing assertions were in force in all three full runs and
never differed.

Source key: **doc** = Microsoft Learn page; **obs** = observed on the oracle, stable in every boot that ran the assertion; **rec** = cross-checked
with `data/win10/font_inventory/dpi96.rec.gz`; **decomp** = Win10 22H2 corpus decompilation of the export (gdi32full).

Docs: EnumFontFamiliesW `learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-enumfontfamiliesw`; EnumFontFamiliesExA `.../nf-wingdi-enumfontfamiliesexa`;
EnumFontsA `.../nf-wingdi-enumfontsa`; CreateFontA `.../nf-wingdi-createfonta`; SetMapperFlags `.../nf-wingdi-setmapperflags`;
ENUMLOGFONTEX, NEWTEXTMETRIC, DESIGNVECTOR structure pages.

## Expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| feo_efw_null_lists_each_family_once | `EnumFontFamiliesW(NULL)` returns the last callback value (1), passes `lParam` through, lists each family name once (no duplicates), FontType is only RASTER_FONTTYPE or TRUETYPE_FONTTYPE (no DEVICE_FONTTYPE on a screen DC), every TrueType entry has nonzero `ntmFlags`, at least one TrueType and one raster family | doc (return value, FontType flags) + obs (no duplicates) | 3 full runs, 155 families (145 TT, 7 raster, 3 type 0) identical |
| feo_efw_null_lists_each_family_once | every non-TrueType family is listed before the first TrueType family | obs | 4 boots |
| feo_efw_null_order_anchors | relative order of 10 raster/stroked families (System, Terminal, Fixedsys, Modern, Roman, Script, Courier, MS Serif, MS Sans Serif, Small Fonts) and of 21 TrueType families (Marlett, Arial, Arial Black, Calibri, Cambria, Candara, Comic Sans MS, Consolas, Courier New, Georgia, Impact, Lucida Console, Microsoft Sans Serif, Segoe UI, Symbol, Tahoma, Times New Roman, Trebuchet MS, Verdana, Webdings, Wingdings) in the NULL listing. Skips when an anchor family is absent. | obs; rec: every family in the recording's `face` table is enumerated (92 of 92; the 27 extra enumerated TrueType names are charset-alias families such as "Arial Greek" and Bahnschrift named instances) | 4 boots |
| feo_efw_first_face_of_family | The per-family listing's first entry equals the entry the NULL listing gives for the family (type, charset, pitch/family, full name, style); TrueType entries have style "Regular" | doc (one font per family when NULL) + obs | 3 full runs |
| feo_efw_callback_stop_and_return | callback returning 0 stops enumeration after that call and the function returns 0; nonzero (5, -1) continues and the last value is returned; same for the A entry point | doc | 3 full runs |
| feo_efa_vs_efw_same_dc | A and W lists on one DC have the same length, same order, same type/charset/ntmFlags/full name/style; the A name equals the W name narrowed through the ACP; `EnumFontsW/A(NULL)` give the same list as `EnumFontFamilies` | obs | 3 full runs (en-US, ASCII names) |
| feo_efa_vs_efw_per_family | same for Arial, Courier, Terminal, Segoe UI, MS Sans Serif | obs | 3 full runs |
| feo_exa_ansi_charset_empty_name | `lfCharSet=ANSI_CHARSET`, empty face: only ANSI faces (`lfCharSet` and `tmCharSet` both 0), OEM (Terminal), SYMBOL and stroked families absent, Arial and System present; the list equals the ANSI entries of the `DEFAULT_CHARSET` listing in the same order; every name is in the plain family list; `SYMBOL_CHARSET` lists only symbol faces (Marlett, Symbol, Webdings, Wingdings) | doc (charset filter) + obs (the equality with the DEFAULT_CHARSET listing) | 3 full runs |
| feo_exa_face_name_styles | With a face name, only that family's styles: Arial, Courier New, Segoe UI, Times New Roman give 4 entries in the order Regular, Bold, Bold Italic, Italic, with `ntmFlags` NTM_REGULAR / NTM_BOLD / NTM_BOLD+NTM_ITALIC / NTM_ITALIC, FontType TRUETYPE, `elfFullName` containing the family name, nonempty; a charset the family lacks (Arial + SYMBOL) lists nothing; Greek lists the 4 styles with `lfCharSet` and `tmCharSet` GREEK; an unknown family never calls back and returns 1; ExA and ExW agree; a raster family via the Ex entry point has FontType RASTER, empty `elfStyle`, nonempty `elfFullName` | doc (nf-wingdi-enumfontfamiliesexa, ENUMLOGFONTEX, NEWTEXTMETRIC `ntmFlags`) + obs (return value 1 when no callback ran) | 3 full runs |
| feo_exa_default_charset_order_recorded | Order of `DEFAULT_CHARSET` + face name: style-major, then charset ANSI, HEBREW, ARABIC, GREEK, TURKISH, BALTIC, EASTEUROPE, RUSSIAN, VIETNAMESE. Golden signatures for Arial, Courier New (34 entries) and Segoe UI (32 entries; no Arabic for Bold Italic/Italic). Skips when the installed style/charset set differs, fails when the same set arrives in a different order. | obs (recorded in the test as `SIG_FULL`, `SIG_SEGOE`) | 4 boots |
| feo_exa_substituted_face_listing | Enumerating the substituted face "MS Shell Dlg": every entry has `lfFaceName` "MS Shell Dlg" and `elfFullName` equal to the `FontSubstitutes` registry target (Microsoft Sans Serif); skips when no substitution exists | obs; the target is read from the registry at run time | 3 full runs |
| feo_enumfonts_bitmap_sizes | `EnumFontsA/W` on Courier, MS Sans Serif, Terminal: at least two entries, all RASTER_FONTTYPE with the requested face, at least two distinct heights, A and W agree, same count as `EnumFontFamilies`; final return value equals the last callback return (7), a callback returning 0 on the 2nd (1st) entry stops and returns 0 | doc (nf-wingdi-enumfontsa) + obs (counts 6, 12, 11 are NOTE only) | 3 full runs |
| feo_enumfonts_truetype_family | See surprises: on a TrueType family `EnumFonts` lists every style and every charset-alias family. Asserted: first entry is "Arial"/Regular, base name once per style (4), all entries TrueType with nonzero `ntmFlags`, names either "Arial*" or "Arabic Transparent", entries grouped in the style order Regular, Bold, Bold Italic, Italic, A equals W in count | obs (28 entries) | 3 full runs |
| feo_enumfonts_unknown_face | no callback and return 1 for `EnumFontsW/A` and `EnumFontFamiliesW` | obs | 3 full runs |
| feo_callback_structures | Ex callback: `elfFullName`, `elfStyle`, `elfScript` ("Western") nonempty; `NEWTEXTMETRIC` has `ntmFlags`, `ntmSizeEM`, `ntmCellHeight`, `ntmAvgWidth` nonzero; `lfWeight`/`lfItalic` of the four Arial entries match their styles (400/0, 700/0, 700/1, 400/1) | doc (ENUMLOGFONTEX, NEWTEXTMETRIC) + obs | 3 full runs |
| feo_createfonta_round_trip | `CreateFontA` fields (height, width, escapement, orientation, weight, italic, underline, strikeout, charset, precisions, quality, pitch/family, face) are returned unchanged by `GetObjectA`; `CreateFontIndirectA`, `CreateFontW`, `CreateFontIndirectW` store the same LOGFONT (compared field by field; bytes after the face-name terminator are not defined); `GetTextFaceA` on the selected font returns "Tahoma" (6), `GetTextMetricsA` reports weight, italic, underline, strikeout | doc (nf-wingdi-createfonta) + obs | 3 full runs |
| feo_createfonta_quality_charset_weight_passthrough | No charset/quality normalisation is visible in the stored LOGFONT: DEFAULT_CHARSET stays 1, lfQuality 0..5 and the out-of-range value 6 stay as given, weight 0 stays 0; Symbol requested with ANSI_CHARSET keeps ANSI in the LOGFONT (selected face is Symbol with `tmCharSet` SYMBOL, NOTE); a 63 character face name is cut to 31 characters; a NULL face name is accepted and stored empty | obs (a normalisation might be expected; there is none in the stored object) | 3 full runs |
| feo_createfont_unknown_face_fallback | an unknown face yields a font whose `GetTextFace` is an installed family (checked by enumeration) and is not the requested name; `GetObjectA` still returns the requested name. NOTE: with VARIABLE_PITCH or FF_SWISS the match is "Arial", with no pitch/family it is "MS Sans Serif" | obs (only installed-ness is asserted) | 3 full runs |
| feo_createfontindirectex | `CreateFontIndirectExA/W` with an empty DESIGNVECTOR (`STAMP_DESIGNVECTOR`, 0 axes): LOGFONT passes through, `GetObjectA(sizeof ENUMLOGFONTEXA)` returns 188 with the face name, `GetTextFace` returns Arial, `tmWeight` 700, `tmItalic`, cell height 16; `DeleteObject` succeeds; `CreateFontIndirectExA(NULL)` returns NULL (GetLastError stays 0, NOTE); the stored `elfFullName`, `elfStyle`, `elfScript` are reported with NOTE | doc (DESIGNVECTOR, ENUMLOGFONTEXDV) + obs | 3 full runs |
| feo_createfontindirectex_variable_axis_NOTE | NOTE only: Bahnschrift with a one axis design vector (700) gives `tmWeight` 400 and the same text extent as with no axes, i.e. the vector has no effect for an installed (not privately loaded) font | obs | 3 full runs |
| feo_setmapperflags_state | initial value 0; `SetMapperFlags(dc,ASPECT_FILTERING)` returns the previous flags (0), a second call returns 1; flags 2, 3 and 0xFFFFFFFF return `GDI_ERROR` (2 sets `ERROR_INVALID_PARAMETER`) and leave the state unchanged; `SetMapperFlags(NULL, 1)` returns `GDI_ERROR` (last error 87, NOTE); the state belongs to the DC; ends at 0 | doc (nf-wingdi-setmapperflags) + obs (error code, state kept) | 3 full runs |
| feo_setmapperflags_aspect_filtering | State reset to 0 at the end. With the flag set, the digitized aspect ratio of the chosen bitmap font matches the device ratio for MS Sans Serif, Courier, Terminal, Small Fonts. All NOTEs show the same selection with and without the flag in this setup (see surprises); no flag-dependent selection is asserted. | doc ("matches the aspect ratio of the device") + obs | 3 full runs |
| feo_gettextfacealiasw | For MS Shell Dlg, MS Shell Dlg 2, Helv, Tms Rmn, MS Sans Serif, Arial, Segoe UI, Courier, Symbol, NoSuchFaceX the result and return value equal `GetTextFaceW` for the same font; the alias is NOT the `FontSubstitutes` target, it is the requested substituted name ("MS Shell Dlg"); `c == 0` with a buffer returns 0 with `ERROR_INVALID_PARAMETER`; NULL buffer returns length + NUL (6 for Arial); `c == 3` truncates with a terminator | decomp (`GetTextFaceAliasW` rejects `c==0` with a buffer using error 0x57 and otherwise calls the same kernel entry as `GetTextFaceW`) + obs | 3 full runs |
| feo_font_link_and_assoc_exports | `AnyLinkedFonts` is nonzero when `FontLink\SystemLink` exists (recorded `regsub FontLink;SystemLink = 1`) and repeatable; `FontIsLinked` is 0 for Arial, Courier, Symbol and nonzero for Segoe UI, MS Gothic, SimSun, Microsoft YaHei, Malgun Gothic, repeatable; `GetFontAssocStatus(NULL)` is 0; `GetFontAssocStatus(dc)` and `QueryFontAssocStatus()` are repeatable (both 0 in these boots, NOTE); `FontIsLinked(NULL)` result NOTE. Faces that are not installed are NOTE-only. | decomp (`GetFontAssocStatus` returns 0 for a NULL DC; both association exports use `NtGdiQueryFontAssocInfo`; `AnyLinkedFonts`/`FontIsLinked` are thin stubs) + obs + rec | 3 full runs |
| feo_setfontenumeration_NOTE | NOTE only: run in a child process with arguments 0 and 1. Return 0x12, last error 50 (`ERROR_NOT_SUPPORTED`), the enumeration (155 families) is the same before and after, and the parent's enumeration is unchanged | obs | 3 full runs |

## Oracle surprises (Win10 differs from common assumptions or the docs)

- `FontType` is 0 (neither RASTER, TRUETYPE nor DEVICE) for the stroked families Modern, Roman and Script. The assertion allows 0 only through the "no DEVICE, no other bits" check and the three names are reported with NOTE.
- `EnumFonts`/`EnumFontFamilies` on a TrueType family do not return a single entry. For "Arial" they return 28 entries: four styles times seven families (Arial, Arabic Transparent, Arial Baltic, CE, CYR, Greek, TUR). The NULL listing contains these charset-alias families as separate families too.
- Windows lists an "@" vertical twin for every CJK family; they are separate entries (not asserted, only filtered out of the anchors).
- `ENUMLOGFONT` for TrueType carries `ntmFlags` such as 0x240040; only the NTM_REGULAR/BOLD/ITALIC bits are asserted.
- With `lfCharSet=DEFAULT_CHARSET` and a face name, `EnumFontFamiliesEx` returns one entry per (style, charset) pair: Arial 34 = 9 + 9 + 8 + 8 entries. A substituted face (MS Shell Dlg) additionally lists Thai (222).
- `GetTextFaceAliasW` is identical to `GetTextFaceW` for every face tried, including substituted ones; both return the requested name for a substituted face ("MS Shell Dlg", "Helv", "Tms Rmn"), not the `FontSubstitutes` target.
- `SetFontEnumeration` fails (error 50) for every argument tried and does not change enumeration.
- `CreateFont*` does not normalise charset or quality in the stored LOGFONT; `lfQuality=6` is stored as 6.
- ASPECT_FILTERING: an exploratory boot (heights -8, -14, -20 in sequence per face) showed a 96/96 selection for MS Sans Serif at -20 with the flag set against 120/120 without; the focused test with a single request per face showed 120/120 in both modes in every boot. The result depends on the mapper's recent history, so it is not asserted.
- Design vector axes have no effect on an installed variable font through `CreateFontIndirectExW`.

## Skips

None triggered on the oracle. Conditional skips (each with a concrete reason printed by the test): an anchor family missing (feo_efw_null_order_anchors), a recorded style/charset set differing (feo_exa_default_charset_order_recorded), no `FontSubstitutes` entry for MS Shell Dlg, the raster fonts missing, Bahnschrift missing, a missing export.

## Not covered by assertion

- `SetFontEnumeration`: undocumented, NOTE only (above).
- `QueryFontAssocStatus` and `GetFontAssocStatus` with a non-null DC: only repeatability and the NULL case are asserted, because the status is 0 in the standard image and nothing else was observed.
- Charset-dependent ordering for locales other than en-US, and the vertical "@" families' order.
- No recorder family was added. The enumeration order is recorded in the test (`ANCH_*`, `SIG_*`) and compared across four boots instead.

## Special-setup ideas (not implemented)

- dx_font_substitution (opt-in, registry change plus restore): add/change a `FontSubstitutes` value, then check `EnumFontFamiliesEx` of the alias, `GetTextFaceAliasW`, and the mapper result; restore the original value.
- dx_font_link (opt-in): edit `FontLink\SystemLink` for one face, check `FontIsLinked` and `AnyLinkedFonts`, restore.
- Font install/remove: `AddFontResource*` of a private bitmap font and a design-vector-capable variable font, then enumerate: expect the new family, then its removal; the design vector should take effect for privately loaded variable fonts.
- Enumeration order after installing a font in the middle of the alphabet (inventory dependency of order).
- Other user locales (Japanese, Greek): ACP narrowing for non-ASCII family names in `EnumFontFamiliesA`, charset-alias families.
- Printer DC enumeration (`DEVICE_FONTTYPE`).
- ASPECT_FILTERING with a DC whose aspect is not 1:1 (for example a printer or enhanced metafile DC), where a flag-dependent selection is expected.
