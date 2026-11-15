# Evidence: control_heights

Area: font object round trips, `TEXTMETRIC` identity, and the geometry contracts of comctl32 v6 header (`HDM_LAYOUT`,
`HDM_GETITEMRECT`) and tree view (`TVM_GETITEMHEIGHT`/`TVM_SETITEMHEIGHT`/`TVM_GETITEMRECT`) controls. Source:
`tests/control_heights/control_heights.c`. The control-height numbers of Windows 10 per font and DPI are not asserted here;
they live in the recordings `data/win10/control_heights/` and in `tests/golden_control_heights`.

Oracle: Windows 10 22H2 (19045.2965, en-US, 1024x768, 96 DPI, one elevated session), `tools/vm_test.sh control_heights`.
comctl32 v6 is activated through an activation context built from an embedded manifest (the exe has no manifest of its own).
Runs: two boots of the earlier code (`PASS tests=9 checks=660/660`, identical logs) and two boots of the final code (runs 3 and 4:
`PASS tests=9 failed=0 skipped=0 checks=675/675`, identical logs). The 15 additional checks are the header item width (below).

Source codes: D = Microsoft documentation, O = observed on Win10 22H2 19045.2965 (n/n over the 4 boots), R = recording under
`data/win10/`, X = decompilation read through the reference corpus (gdi32full, win32kfull, comctl32 v6).
D urls: `https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-getobjectw`,
`https://learn.microsoft.com/windows/win32/controls/hdm-layout`, `.../controls/tvm-getitemheight`,
`.../controls/tvm-setitemheight`, `.../controls/tvm-getitemrect`, `.../api/wingdi/ns-wingdi-textmetricw` (same prefix).

## Asserted expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| getobject_font_null_buffer_returns_logfontw_size | `GetObjectW(hfont, 0 or 1000, NULL)` returns `sizeof(LOGFONTW)` (92), for a created font and for the DEFAULT_GUI and SYSTEM stock fonts | D getobjectw (NULL buffer returns the required size) + X gdi32full!GetObjectW (0x5c for a font) | 4/4 |
| getobject_font_roundtrip | for 8 (face, height) pairs including an absent face name: `GetObjectW` returns 92; lfHeight, lfWeight (400) and lfFaceName are exactly as given (no substitution is visible through GetObject) | D getobjectw + X win32kfull!GreExtGetObjectW (the stored LOGFONT is returned) | 4/4 |
| getobject_font_partial_buffer_copies_c_bytes | `GetObjectW(hfont, 8, buf)` returns 8, the first 4 bytes are lfHeight, bytes 8.. are untouched | D getobjectw ("number of bytes copied") + X GreExtGetObjectW (min(stored, c)) | 4/4 |
| getobject_on_dc_fails_invalid_handle | `GetObjectW` on a memory DC returns 0 with `ERROR_INVALID_HANDLE` (6) | X gdi32full!GetObjectW (GdiSetLastError 6) + O | 4/4 |
| textmetric_height_is_ascent_plus_descent | `tmHeight == tmAscent + tmDescent` for 10 faces x 9 heights (selected font) and for 7 stock fonts (raster and TrueType, absent faces fall back) | D textmetricw (tmHeight is defined as ascent plus descent) + X win32kfull!vIFIMetricsToTextMetricWStrict (tmDescent = tmHeight - tmAscent) | 4/4 |
| header_layout_contract | for 5 style sets (HORZ, BUTTONS, BUTTONS+HIDDEN, BUTTONS+FILTERBAR, FLAT) x 3 fonts (default, Segoe UI -12, MS Shell Dlg -11): `HDM_LAYOUT` returns 1; `hwndInsertAfter` NULL, flags `SWP_NOZORDER|SWP_NOACTIVATE`; x,y,cx come from the input rect; `rc.top` advances by `wp.cy`, other sides unchanged; the window handle field is never written; `cy == 0` with HDS_HIDDEN else `> 0` | D hdm-layout + X comctl32 Header_OnLayout (flags 0x14, cy = HDS_HIDDEN ? 0 : height, prc->top += cy) | 4/4 |
| header_layout_contract | `HDM_GETITEMRECT` top/bottom equal the client rect, an out-of-range index returns 0 | X Header_OnGetItemRect | 4/4 |
| header_layout_contract | the item rect of the first item is exactly as wide as the width set (100), for all 15 style/font combinations; checked on the rect of the successful `HDM_GETITEMRECT(0)`, before the out-of-range call | O (was a NOTE in the earlier code that never fired in 2 boots, promoted to an assertion after 2 more boots) | 4/4 (earlier code: no NOTE; final code: 15 passing checks) |
| header_layout_null_args | NULL HDLAYOUT, NULL `prc`, NULL `pwpos` each return 0 and write nothing | X Header_OnLayout NULL checks | 4/4 |
| treeview_item_height_contract | `TVM_GETITEMHEIGHT >= 1`, even without TVS_NONEVENHEIGHT; item rects (`fItemRect` FALSE) have left 0, height == item height, successive tops differ by exactly the item height; text rect has the same top/bottom | D tvm-getitemrect + X TV_GetItemRect (top = margin + cyItem * index) | 4/4 for each of 4 fonts (default, Segoe UI -12, MS Shell Dlg -11, Tahoma -13) x 2 styles |
| treeview_item_height_contract | `TVM_SETITEMHEIGHT` returns the previous height; 21 reads back as 20 (21 with TVS_NONEVENHEIGHT); 1 reads back 1; 0 reads back 1; 30 reads 30; -1 returns 30 and restores the font-derived default | D tvm-setitemheight (even rounding documented) + X TV_WndProc / TV_SetItemHeight (cyItem < 2 -> 1, round down to even) | 4/4 |

## NOTE-only (not asserted)

| Test | Output | Why only a NOTE |
|---|---|---|
| listview_report_rows_observed | Per font (default, Segoe UI -12, MS Shell Dlg -11): `tmHeight 15 / row 19`, `15 / 19`, `13 / 17` (item rect height, `next top` = bottom, SM_CYBORDER 1) | Windows 10 specific numbers, stable 4/4 and also in the recording `data/win10/control_heights/dpi96.v6.rec.gz` (`lv` rows); they belong to the golden test, not to a contract. The relation row = tmHeight + 4 happens on all three fonts but is not documented, and the source decompilation (`CLVView::ComputeCYItemSize`) depends on the system `EditYPadding`, which is not asserted. |

## Skips

Every comctl32 test skips (with the reason printed: manifest not writable, activation context failure, v6 not loadable, parent
window not creatable) when the v6 environment cannot be built. It was built on every boot, so no skip occurred.

## Weakness review and changes made to the test

1. `header_layout_contract`: the item width was only reported with a NOTE. It never fired (item width 100 in all 15 combinations on
   all boots), so it is now `EQ(ir.right - ir.left, 100)`. That is a strengthening. The check reads the rect returned by
   `HDM_GETITEMRECT(0)` and is made before the out-of-range `HDM_GETITEMRECT(5)`, so it does not depend on a failing call
   leaving the buffer alone. The header comment says this check is observed, not decompile-backed. No other code change.
2. Reviewed and left as is: the `EQ` macro exists because `EXPECT_EQ` evaluates its arguments twice and would resend a
   SendMessage with a clobbered buffer; the memcpy of the HTREEITEM into the RECT is the documented TVM_GETITEMRECT input form.

## Ideas not implemented

- Tree view and list view heights with comctl32 v5 (the unmanifested exe) and with system-aware and per-monitor-aware manifests
  (SOW priority 1); the recorder already holds the v5 and v6 rows at 96 DPI, but no per-DPI contract test exists.
- `TVM_SETITEMHEIGHT` interplay with an image list (`max(text, image) + 1`).
- Header `HDS_FILTERBAR` height formula, list view `LVM_APPROXIMATEVIEWRECT`, combo and edit control heights (recorder only).
- A test of `TEXTMETRIC` overhang for the stock fonts (only TrueType faces have it asserted in `char_widths`).
