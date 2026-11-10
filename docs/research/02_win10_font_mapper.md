# 02 - Win10 font mapper semantics (decomp study)

Role: WIN10 FONT MAPPER SEMANTICS. Source: the Win10 22H2 reference corpus (build 19041.2965) at
`http://docker1.dev.moonlightcompanies.com:8765`, modules `win32kfull.sys`, `win32kbase.sys`, `gdi32full.dll`,
`user32.dll`, `dwrite.dll`. Every function is cited as `module!Name @rva`. Decompilations were fetched with
`/api/function/<mod>/<rva>/decomp`. Raw bytes (string tables, constants) were read with `/api/bytes`.

Labels used below:

- **[code]**: read directly from the decompilation or assembly of the named function. Structure offsets are
  mapped to field names using the public WDK layouts (`IFIMETRICS`, `FD_DEVICEMETRICS`, `GLYPHDATA`, `LOGFONTW`,
  `TEXTMETRICW`, `OUTLINETEXTMETRICW`). The field mapping is checked against the copy code, for example
  `bGetDEVICEMETRICS` presets exactly the four `lNonLinear*` fields to `0x80000000`.
- **[W10] / [W7]**: a measured value already recorded in this repo (`docs/research/03_recorded_data.md`, from bb
  depot runs m199/m208 and the Win7 `*.win7.tsv` recordings). It is quoted here only to check a derivation.
- **[inf]**: an inference. It is not a fact. A probe has to confirm it (section 12 lists the probes).

Units: lfHeight is in **logical units**. With MM_TEXT and an identity world transform (the case for every UI
control), logical = device pixels. `ppem` = pixels per em (character height). `tmHeight` is the **cell** height,
= ascent + descent = em + internal leading. "28.4" means 28.4 fixed point, so 16 units = 1 px.

---

## 0. The main result for D12

1. **Face substitution is in the kernel (`win32kfull`). It runs at realisation time, not at CreateFont time.**
   `CreateFontIndirectW` stores the LOGFONT unchanged. Only one compatibility fix exists: "OCR-A" is forced to
   DEFAULT_CHARSET under an appcompat flag. `GetObject` returns the stored LOGFONT byte for byte (sections 1
   and 9).
2. **The exact name is tried first. FontSubstitutes is a second bucket.** The candidates in all buckets are
   scored by one penalty function (`MAPPER::bNearMatch`) and the lowest penalty wins. On a tie, the smaller
   `PFE+0x50` wins [inf: load order]. For TrueType there is **no height penalty**. Height
   only scores raster (`.fon`) fonts (sections 3 and 4).
3. **GetTextFaceW returns the *requested* name when the match came through a FontSubstitutes alias.** So
   "MS Shell Dlg" reports "MS Shell Dlg", not "Microsoft Sans Serif". Otherwise it returns the realised font's
   family name. This agrees with [W10] m199: `msshelldlg_m11` reports "MS Shell Dlg" (section 8).
4. **The "MS Shell Dlg" height snap. This is new and it matters for the 1-px question.** In `bGetNtoW_Win31`
   (the GM_COMPATIBLE path, which is the default for every DC), the requested height is snapped when **both**
   of these hold:
   - the face name is exactly "MS Shell Dlg" (case-insensitive; "MS Shell Dlg 2" does not qualify);
   - FontSubstitutes maps `MS Shell Dlg` -> `Microsoft Sans Serif` (the global `gbShellFontCompatible` is set).

   The snap: lfHeight -9..-12 -> **-11**, -13..-15 -> **-13**, +12..+15 -> **+14**, +16..+19 -> **+16**.
   This is why MS Shell Dlg -12 measures exactly like -11 on Win7 (13/11/2/2/0/5/23 at both sizes, [W7]
   GFNT.METRICS.001). Industrial7 must reproduce this snap for MS Shell Dlg to be pixel-exact, with or without
   micross.ttf (section 5.3).
5. **Pixel rounding happens in two places.**
   - The **font driver** (TTFD, a user-mode font driver in `fontdrvhost.exe`, which is **not in the corpus**)
     returns the ascender and descender in 28.4. The `[W7]` Arial and Segoe UI rows show these are hinted or
     VDMX values, not linear ones (section 7).
   - **GDI** then derives the TEXTMETRIC fields with fixed rules: `tmHeight = round(asc28.4 + desc28.4)`,
     `tmAscent = round(asc28.4)`, `tmDescent = tmHeight - tmAscent`,
     `tmInternalLeading = tmHeight - round(em)`, `tmExternalLeading = round(max(0, hhea line - win line))`,
     and ave/max widths linear from IFIMETRICS unless the driver overrides them (section 6).

   Every rule checked here reproduces the recorded Win7 Arial ladder (section 6.4).

---

## 1. CreateFontIndirect / GetObject (user mode and kernel entry)

| step | function | what it does [code] |
|---|---|---|
| A->W | `gdi32full!CreateFontIndirectA @0x1f00` | converts and calls the W path |
| W | `gdi32full!CreateFontIndirectW @0x6140` | `vConvertLogicalFont` copies the LOGFONT into an ENUMLOGFONTEXDVW, then copies `lfFaceName`. **Only quirk:** with `GetAppCompatFlags2(0x400)` bit 14 set and face == "OCR-A", it forces `lfCharSet = DEFAULT_CHARSET(1)`. Then it calls `CreateFontIndirectExW`. |
| Ex | `gdi32full!CreateFontIndirectExW @0x61e0` | allocates a LOCALFONT (`plfCreateLOCALFONT`, flag = escapement or orientation != 0). It rejects `dvNumAxes > 16`. It calls `NtGdiHfontCreate(elfdv, 0x164 + 4*axes)`. No name, height or charset is changed. |
| GetObject | `gdi32full!GetObjectW @0x19f60` -> `win32kfull!GreExtGetObjectW @0x99f88` | for a font handle, `pv == NULL` returns 0x5c (sizeof LOGFONTW). Otherwise it does `memcpy(pv, LFONT+0x114, min(c, stored size))`: **the stored LOGFONT, unchanged** (no substitution, no realised height). The exception is stock LFONT types 4/5/6 (DEVICE_DEFAULT_FONT, ANSI_VAR_FONT, ANSI_FIXED_FONT). For those it returns the *display PDEV's* default LOGFONTs (PDEV+0x598 / +0x5a0 / +0x5a8). |

So `GetObject(hfont)` never shows the realised face or height. To see what was realised, use `GetTextFace` and
`GetTextMetrics` on a DC.

---

## 2. Registry inputs to the mapper (all under `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion`)

`RtlQueryRegistryValues(3 = RTL_REGISTRY_WINDOWS_NT, ...)` is used for the first three rows.

| key | read by | table | semantics [code] |
|---|---|---|---|
| `FontSubstitutes` | `vInitFontSubTable @0x394b80` + `QueryRegistryFontSubstituteListRoutine @0x394440` + `vProcessEntry @0x110be8` + `vCheckCharSet` | `gpfsTable` (0xc4-byte `_FONTSUB` entries), `gcfsTable` | see 2.1 |
| `FontMapper` | `InitializeFontSignatures @0x394d4c` + `DefaultFontQueryRoutine @0x394570` | `MAPPER::SignatureTable` / `MAPPER::FaceNameTable`, `MAPPER::DefaultCharset` | see 2.2 |
| `FontMapperFamilyFallbackDeprecated` | `vInitFontMapperFamilyFallbackTable @0x394ef0` + `QueryRegistryFontMapperFamilyFallbackListRoutine @0x395670` | `gpfmffTable` (0x80-byte entries: name[32], fallback[32], both upper-cased) | used only when a name is not found at all in the public font table (2.3) |
| `Gre_Initialize` (`GUIFont.Facename/Height/Weight/Italic/CharSet`) | `hfontInitDefaultGuiFont @0x39334c` | DEFAULT_GUI_FONT | section 10 |
| `Windows` (`SystemFont`, `SystemFontSize`) | `bInitStockFontsInternal @0x39353c`, `bInitSystemFont @0x39532c` | SYSTEM_FONT, `gppfeMapperDefault` | section 10 |
| `Gre_Initialize\SmallFont`, `\LargeFont`, `HKLM\System\CurrentControlSet\Hardware Profiles\Current\Software\Fonts` (`FONTS.FON`, `OEMFONT.FON`, `FIXEDFON.FON`) | `FinishStockFontInitInternal @0x1c994` | SYSTEM_FONT(13), OEM_FIXED_FONT(10), SYSTEM_FIXED_FONT(16) `.fon` files | section 10 |

### 2.1 FontSubstitutes parsing and lookup

The parsing rules [code]:

- **Value name and value data.** Each is `Face[,charset]`. `vProcessEntry` splits at the first `,` within 32
  WCHARs and upper-cases the face with `cCapString`.
- **Charset field.** With no `,`, the charset is DEFAULT_CHARSET(1) and the entry is flagged "no charset"
  (`+0x81` bit 0 = 1). With a `,`, `vCheckCharSet` parses a decimal number < 256. The number must be in the
  17-entry `charsets` table, or the entry gets error bit `+0x81 |= 2`.
- **When an entry is kept.** Only if both sides have the **same** charset form (both bare or both with charset)
  and neither side has an error.
- **Shell font flag.** If an entry maps `MS Shell Dlg` to `Microsoft Sans Serif` (both case-insensitive), it
  sets the global `gbShellFontCompatible = 1`. This flag gates the height snap in section 5.3.
- **Built-in entry 0.** It is `Default Sans Serif -> MS SANS SERIF`. It is skipped unless `GetAppCompatFlags2`
  bit 10 is set (`pfsubAlternateFacename`, `bFoundExactMatch`).
- **Order.** The registry enumeration order is kept. Lookups scan the whole table.

The `_FONTSUB` layout [code]:

| offset | field |
|---|---|
| `+0x00` | raw face text |
| `+0x40` | from-face (upper case) |
| `+0x80` | from-charset |
| `+0x81` | from-flags |
| `+0x82` | to-face |
| `+0xc2` | to-charset |
| `+0xc3` | to-flags |

### 2.2 FontMapper (signature -> default face)

`DefaultFontQueryRoutine` turns each value `NAME = DWORD` into a 32-bit table word. The high 16 bits are the
offset of the name in `FaceNameTable`, and the low 16 bits are the low word of the DWORD (the "signature").
A trailing digit on a value name is dropped, so `ARIAL1` gives "ARIAL". The value named `DEFAULT` instead sets
`MAPPER::DefaultCharset`. If the ANSI code page is 65001 (UTF-8), DefaultCharset is forced to 0xFE.

`FindFaceName @0x1c640` returns the **first** entry whose signature equals the wanted one exactly. If none
matches, it returns L"" (rdata 0x2e436c, read as an empty string).

`MAPPER::bGetFaceName @0x1c4dc` builds the wanted signature from the LOGFONT [code]:

| part | value |
|---|---|
| charset | `lfCharSet`, or `DefaultCharset` when `lfCharSet == 1` |
| `| 0x8000` | when the pitch (`lfPitchAndFamily & 3`) is FIXED_PITCH, or when the pitch is DEFAULT and the family is FF_MODERN |
| `| 0x4000` | when the family is FF_ROMAN |
| `| 0x2000` | when the face starts with `@` (vertical) |
| bitmap first | when charset == 0 and a PDEV flag is set (`flags & 0x100`), it first tries `sig|0x1000` and then `sig|0x800`. Each name must name a raster font with a strike at exactly the requested height (`bFindBitmapFont @0x1c67c`). Then it falls back to `sig`. |

The Win10 contents of `FontMapper` are **not known yet** (probe P1).

### 2.3 FontMapperFamilyFallbackDeprecated

When the requested name has no hash bucket in the **public** table and `gpfmffTable` is not empty, the cold
path `FUN_1c017f391` (in `bFoundExactMatch`) looks the name up in this table. If found, it searches the fallback
name as a second bucket. The key name suggests that it is normally empty on 22H2 [inf; probe P1].

---

## 3. The mapping algorithm (`ppfeGetAMatch` and friends)

Call chain [code]: `RFONTOBJ::bInit` / `GreGetTextFaceW` -> `LFONTOBJ::ppfeMapFont @0x5b3a0` (3-entry per-LFONT
cache keyed on PDEV, the 4 world-transform floats and the AA flags) -> `ppfeGetAMatch @0x59b40` -> `MAPPER`.

### 3.1 `MAPPER::MAPPER @0x57880` (normalises the request)

| item | rule [code] |
|---|---|
| face copy | copies the name up to `_<digit>`, then parses `_NNN_NNN` as design-vector axis values. If there are no axes, it uses the LOGFONT's `elfDesignVector`. |
| `lfHeight` | `0` -> PDEV default height (PDEV+0x724), flag `0x8`. `< 0` -> flag `0x2` ("em height"), then the absolute value is stored. |
| `lfWidth` | its absolute value is stored |
| `lfWeight` | `0` -> 400, flag `0x200000` (softer weight penalty). 400 or 700 -> flag `0x10000000` (disables the bitmap-font-first path). |
| `lfQuality == PROOF_QUALITY (2)` | flag `0x8000`: bitmap fonts may not be scaled (integer-multiple scaling is off) |
| DPI | `this+0xf4/0xf8` = PDEV LOGPIXELSX/Y. **But** when the PDEV is a display (`PDEV+0x28 & 1`) and the thread's DPI awareness (low nibble) is not 1 or 2, it is 96 (0x60). These values only take part in raster-font aspect matching (3.3). |
| special names (upper-case compare) | `MS SHELL DLG` -> flag `0x8000000` (charset is chosen by the font, no DEFAULT_CHARSET penalty). `SYSTEM` -> flag `0x2000`; **with FIXED_PITCH the face becomes `FIXEDSYS`** (cold path, rdata 0x2f91e8 reads "FIXEDSYS"). `TMS RMN` -> flag `0x800000` (DONTCARE family is scored as FF_ROMAN). `SYMBOL` -> flag `0x4000000` (charset mismatch is allowed at +65000). A leading `@` -> flag `0x2000000` (vertical). |
| empty face | `bGetFaceName` (FontMapper signature lookup, 2.2) |
| graphics mode | DC `iGraphicsMode == GM_COMPATIBLE (1)` and no metafile -> flag `0x1000` |

### 3.2 Search order (`ppfeGetAMatch`)

Each step stops at the first success [code]:

1. **Forced match.** If the DC carries a forced UFI (`DC+0xfc & 4`), `bFoundForcedMatch`.
2. **Private fonts.** The private table (`gpPFTPrivate`, AddFontResourceEx FR_PRIVATE), face hash then family
   hash, via `bFoundExactMatch(.., 0)`.
3. **Device fonts.** These are the PDEV's own fonts (flag `0x200`, set when the PDEV has fonts, which means
   printers). In this case, `MAPPER::vAttemptDeviceMatch` is also tried later.
4. **Public fonts.** `gpPFTPublic`, face-name hash then family-name hash, via `bFoundExactMatch(.., 1)`. The `1`
   enables the FamilyFallback of 2.3.
5. **Synthesis.** `MAPPER::ppfeSynthesizeAMatch` makes a simulated (bold/italic) or linked match.
6. **Device match.** `vAttemptDeviceMatch` (device fonts only).
7. **No name match at all.** The mapper drops the name.
   - If the face name was not already produced by `bGetFaceName`: it calls `bGetFaceName`, which replaces the
     name with the FontMapper default for the signature (2.2). It resets the best penalty to `0xfffffffe` and
     re-runs `bFoundExactMatch` on the public tables. If that fails, it takes the cold path `FUN_1c017f31a`.
   - If the name already came from `bGetFaceName`: `MAPPER::vEmergency` (flag `0x4000000`, so charset
     mismatches become a +65000 penalty instead of a rejection) scans all fonts. `gppfeMapperDefault`
     (SYSTEM) is the last resort.

So for the UI faces (Segoe UI, Tahoma, Arial, Courier New, MS Shell Dlg[ 2]), step 4 decides.

### 3.3 `MAPPER::bFoundExactMatch @0x59d60` (buckets)

The steps [code]:

1. Upper-case the face (max 31 chars).
2. Scan the whole FontSubstitutes table for entries whose from-face equals it.
   - A **charset-specific** entry (`Face,cs`) wins if `cs == lfCharSet`.
   - Otherwise a **bare** entry (`Face`) is used.
3. Build up to **three candidate buckets** in the font hash:

   | bucket | contents | used when |
   |---|---|---|
   | 0 | the requested face itself | no substitute applies, or a bare substitute applies |
   | 1 | the substitute face | a substitute applies. For a charset-specific entry, only bucket 1 is used and the mapper's charset becomes the entry's to-charset. |
   | 2 | the base name without the `_axis` suffix | the hash is the family hash (type 1) |

4. Score every PFE in every bucket with `bNearMatch`. Keep the lowest penalty. On a tie, the PFE with the
   lower `PFE+0x50` (load order) wins, unless flags `0x1000080` are set.
5. Write the result flags (`*this+0xe8`). Bit 0 means "matched by name". **Bit 1 means "report the LOGFONT
   name"**. Bit 1 is set when the winner came from bucket 1 (the substitute), or from bucket 0/2 of a hash
   whose type field (`FONTHASH+4`) is 0. The public table has two hashes: `gpPFTPublic+0` (searched first)
   and `+8`. Type 0 being the *face-name* hash and type 1 the *family* hash follows the NT naming
   (FHT_FACE/FHT_FAMILY) [inf]. Bucket 2 is only searched when the type is 1. One consequence needs probe P2:
   a request typed in another case (e.g. "arial") would be echoed as typed. Bits 24..31 hold the chosen
   charset. Bits 8..23 hold the code page of that charset (from
   the `charsets`/`codepages` tables). `GetTextFace` uses bit 1 (section 8).

Consequence: if a real "MS Shell Dlg" font were installed, it would compete in bucket 0 with the substitute in
bucket 1. On stock Win10 there is no such font, so the substitute wins.

---

## 4. The penalty function `MAPPER::bNearMatch @0x5a300`

The penalty is in `this+0xb8`. Every step compares the running total with the best so far (`bNoMatch @0x5d3a8`)
and gives up early. "reject" = the candidate is impossible (penalty `0xfffffffe`).

| test | penalty or rule [code] |
|---|---|
| private font of another process, or flags `PFE+0xc & 0x22` | reject |
| bucket 2 (axis-stripped) but the font is not FM_INFO_TECH_MM | reject |
| **pitch** (`lfPitchAndFamily & 3`) | DEFAULT and the font is FIXED: +1. FIXED and the font is VARIABLE: +15000. VARIABLE and the font is not VARIABLE: cold path (same idea). |
| **family** (`& 0x70`) | A request of FF_DONTCARE with a font family != 0 and charset != SYMBOL is scored as FF_SWISS (FF_ROMAN when the face was "Tms Rmn"). If the family differs: font family 0 -> +8000; otherwise +9000, or +9050 when exactly one side is FF_MODERN or above (0x30). The family's default face is `gpwszFamilyDefaultFonts[family>>4]`. |
| **charset**, case DEFAULT(1) or face "MS SHELL DLG" | `jMapCharset @0x5d66c` picks the font's charset. It prefers `DefaultCharset`, then the font's first charset, also looking through linked fonts. If the request was DEFAULT, the face is not MS Shell Dlg, and the result != DefaultCharset: **+2**. |
| **charset**, case explicit | the font's charset list (`dpCharSets`, 16 bytes, terminated by 1) must contain it; 0xFE always matches. Otherwise: **reject**, except in emergency/SYMBOL mode (flag `0x4000000`) where it costs +65000. |
| **face name** (only callers that pass `param_3 != 0`, i.e. the enumeration/emergency paths, not `bFoundExactMatch`) | not the requested family: +10000, or +9000 if the font is the family's default face. A family-equivalent (alias) match: +1. |
| vertical (`@`) | must match, or reject |
| **height** (only non-scalable fonts: `flInfo & (ARB_XFORMS|ISOTROPIC|ANISOTROPIC) == 0`) | The font height is `fwdUnitsPerEm` for a negative lfHeight, or `fwdWinAscender + fwdWinDescender` otherwise. Integral scaling factor `k = (req + font/4) / font` (`bWin31BitmapHeightScaling`), capped at 8. It is used only when `font < req`, `font*7/4 < req`, FM_INFO_INTEGRAL_SCALING is set and not PROOF_QUALITY. Scaling costs `(k-1)*5 | k*20`. A smaller font costs `(req - font*k)*150`. A larger font costs `(font*k - req + 4)*150`, or `+20` instead of `+4` under the 0x1080 flags. A total of 10000 or more rejects unless flags 0x4800 are set. **TrueType fonts skip this step: they get no height penalty.** |
| raster aspect | when the DC asks for it and the font's lfOutPrecision is OUT_RASTER_PRECIS(6): the font's `ptlAspect` must equal the mapper DPI (3.1), or reject |
| **italic** | not requested but the font is italic: +4. Requested but the font is upright: +1 if it can simulate (sets FO_SIM_ITALIC 0x4000), else +4. |
| **weight** | `d = usWinWeight - lfWeight`. When lfWeight was 0: `|d|*19/128`. Otherwise: if the font is lighter by more than 150 and can simulate bold, set FO_SIM_BOLD (0x2000) and use `d -= 120`; the penalty is `d*73/256`. |
| bitmap-only rules | raster font on a device that cannot take it -> reject. `lfOutPrecision` OUT_TT_ONLY_PRECIS(7) rejects fonts that are not TT-precision. OUT_PS_ONLY_PRECIS(10) rejects non-Type1. OUT_SCREEN_OUTLINE_PRECIS(9), or a system or DC switch, adds +2 for a non-outline font. |
| **width** (`lfWidth != 0`, non-scalable) | `|req - fwdAveCharWidth*k|*50`, plus scaling cost. lfWidth == 0 adds an aspect cost `|ratio diff|*30` for raster fonts. |
| scaling mismatch (bitmap k_x != k_y) | +50 + 400*ratio |
| orientation | `lfOrientation` vs the font's orientation, for non-ARB_XFORMS fonts |
| flag 0x400000 (set in `bFoundExactMatch` when the hash bucket being scanned has `HASHBUCKET+0x20 & 2`; meaning not traced) | +1 |
| design vector | `dvNumAxes` and the values must equal the font's `IFIEXTRA.dpDesignVector`, or reject |

The **TrueType UI case** (e.g. "Segoe UI", -12, weight 400, DEFAULT_CHARSET): the only costs are charset +2 (if
the font's first charset != DefaultCharset) and weight/italic. The **height never takes part** in choosing a
TrueType face. Height only enters later, in the transform (section 5).

---

## 5. Height -> transform -> ppem

### 5.1 Which function

`PFEOBJ::bSetFontXform @0x2610c` calls one of two functions [code]:

- **`bGetNtoD_Win31 @0x58e18`** for DCs in GM_COMPATIBLE (`DC_ATTR+0xd0 == 1`, the default);
- `bGetNtoD @0x5cf60` for GM_ADVANCED.

Both pass the result through the world-to-device matrix (`DC+0x140`, scaled by 1/16 = `0x3d800000`, because the
DC matrix is in 28.4). For bitmap fonts, both return integer multiples of the bitmap (`galFloat[k]`).

### 5.2 Notional -> world (`vGetNtoW @0x5d3f4` for GM_ADVANCED, `bGetNtoW_Win31 @0x596f8` for GM_COMPATIBLE)

[code]:

```
h = lfHeight
if h == 0: h = lGetDefaultWorldHeight(dc)           // see 5.4
if h < 0:  yScale = -h / IFI.fwdUnitsPerEm          // em height: ppem = |lfHeight|
else:      yScale =  h / (IFI.fwdWinAscender + IFI.fwdWinDescender)   // cell height
xScale = (lfWidth != 0 && IFI.fwdAveCharWidth >= 0) ? |lfWidth| / IFI.fwdAveCharWidth : yScale
if LOGPIXELSX != LOGPIXELSY: correct x by LOGPIXELSX/LOGPIXELSY (anisotropic devices only)
```

These scales are **floats**. GDI does no rounding of the em at this point. TTFD receives the float `FD_XFORM`
and decides the integer ppem itself (section 7).

[inf] For TrueType, `fwdWinAscender/Descender` are OS/2 `usWinAscent/usWinDescent` and `fwdUnitsPerEm` is
`head.unitsPerEm`. That is the documented IFIMETRICS meaning, but TTFD fills these fields and TTFD is not in
the corpus. A negative lfHeight is therefore an exact integer ppem. A positive lfHeight gives a fractional
ppem = `h*upem/(winAsc+winDesc)`, and TTFD must round or choose it (probe P4).

### 5.3 The MS Shell Dlg height snap (`bGetNtoW_Win31` only) [code]

```
if gbShellFontCompatible && _wcsicmp(lfFaceName, L"MS Shell Dlg") == 0 && !param_6:
    if h <= 0:
        if  -12 <= h <= -9 : h = -11
        elif -15 <= h <= -13: h = -13
    else:
        if  12 <= h <= 15  : h = 14
        elif 16 <= h <= 19 : h = 16
```

The snap is applied after the `lfHeight == 0` default is resolved.

- `gbShellFontCompatible` is set only by the FontSubstitutes entry `MS Shell Dlg = Microsoft Sans Serif`
  (2.1).
- It does **not** apply to "MS Shell Dlg 2", nor to GM_ADVANCED DCs.
- It emulates the MS Sans Serif bitmap sizes (8 pt = -11 / 13 px cell, 10 pt = -13 / 16 px cell).

Evidence: [W7] MS Shell Dlg -11 and -12 are both `13/11/2/2/0/5/23`, while Tahoma -12 is `14/12/2/2/0/5/25`
and Arial -12 is `15/12/3/3/0/5/32` (03_recorded_data.md 4.3). The meaning of `param_6` (passed from
`bSetFontXform`'s last argument) was not traced [inf: probably "linked/EUDC realisation"].

### 5.4 lfHeight == 0

`lGetDefaultWorldHeight @0x14d638` [code]:

- It takes `lfHeight` of the PDEV's default font (PDEV+0x598, the DEVICE_DEFAULT_FONT LOGFONT). When the
  world transform is not identity, it converts that value to world units.
- It returns the **negated** value (so a cell height becomes an em height).
- It returns 24 if there is no PDEV or the result is 0.

This explains [W10] `default_h0` (lfHeight 0, empty face -> Arial). It measures `18/15/../il 2/ave 7/max 43`.
That is exactly Arial **-16** ([W7] vdmx walk: `-16 -> 18/15/3/2/1/7/43`). So the display default font has
lfHeight 16 and the request became em 16 [inf: the 16 itself is from the measurement match, not read from the
PDEV].

Separately, the mapper uses PDEV+0x724 as the height when it scores raster fonts for lfHeight 0 (3.1).

---

## 6. TEXTMETRIC derivation

### 6.1 Where the numbers come from

- `RFONTOBJ::bRealizeFont @0x62fd8` -> `RFONTOBJ::bGetDEVICEMETRICS @0x62ad8`. This calls the driver with
  `QueryFontData(QFD_MAXEXTENTS = 3)` into an `FD_DEVICEMETRICS`. Before the call it presets
  `lNonLinearExtLeading`, `lNonLinearIntLeading`, `lNonLinearMaxCharWidth` and `lNonLinearAvgCharWidth` to
  `0x80000000` ("not supplied"). [code]
- The RFONT then stores [code]:

  | RFONT offset | value |
  |---|---|
  | +0x13c | `fxMaxAscender` |
  | +0x140 | `-fxMaxDescender` |
  | +0x144 | `fxMaxAscender + fxMaxDescender` (the 28.4 cell) |
  | +0x160 | `(fxMaxAscender+8)>>4` |
  | +0x164 | `(fxMaxAscender+fxMaxDescender+8)>>4` (= the extent cy, see 6.5) |
  | +0x178..+0x184 | the four `lNonLinear*` values |
  | +0xd4 / +0xd8 | notional-to-device x / y scale (`bGetNtoWScales`) |
  | +0x2a8 / +0x2ac | device-to-world x / y (1/16 when the DC is MM_TEXT identity) |

- `GreGetTextMetricsW @0x24d94` -> `vGetTextMetrics @0x24e40` uses a per-RFONT cached copy at RFONT+0x2b0
  (only underline, strikeout and charset are refreshed from the DC) or calls `vIFIMetricsToTextMetricW @0x672d4`.
  That function calls `vIFIMetricsToTextMetricWStrict @0x5d88c` and then appends `chFirst/Last/Default/Break`
  (TMW_INTERNAL).
- User mode caches too: `gdi32full!GetTextMetricsW @0x15010` keeps the result in the CFONT (`pcfLocateCFONT`),
  keyed on DC and font. Repeated calls do not reach the kernel.

### 6.2 The formulas (`vIFIMetricsToTextMetricWStrict` + `IFIOBJR::IFIOBJR @0x5d6e0`)

`round()` here is `bFToL(x, .., 0)`. It adds 0.5 to the magnitude and truncates, so it is **round half away
from zero** (`bFToL @0xf05f8`) [code]. The MM_TEXT fast path uses `(v28.4 + 8) >> 4` (round half up).

| field | MM_TEXT identity (DC flags `0x802`) | general | scalable font (TT) rule |
|---|---|---|---|
| tmHeight | `(fxAsc + fxDesc + 8) >> 4` | `round(cell28.4 * dev2world_y)` | from the driver extents |
| tmAscent | `(fxAsc + 8) >> 4` | `round(fxAsc * dev2world_y)` | from the driver extents |
| tmDescent | **`tmHeight - tmAscent`** (derived, never rounded on its own) | same | |
| tmInternalLeading | | | `lNonLinearIntLeading` if supplied, else **`tmHeight - round(fwdUnitsPerEm * yScale)`**, i.e. tmHeight - em |
| tmExternalLeading | | | `lNonLinearExtLeading` if supplied, else **`round(max(0, fwdMacAscender - fwdMacDescender + fwdMacLineGap - fwdWinAscender - fwdWinDescender) * yScale)`** |
| tmAveCharWidth | | | `lNonLinearAvgCharWidth` if supplied, else `round(fwdAveCharWidth * xScale)` |
| tmMaxCharWidth | | | `lNonLinearMaxCharWidth` if supplied, else `round(fwdMaxCharInc * xScale)` |
| (bitmap fonts) | | | int lead = `winAsc + winDesc - unitsPerEm`, ext lead = the same max(0, ..) expression, both multiplied by the integer scale (RFONT+0x288/+0x28c) and rounded |
| tmWeight | `usWinWeight` (or the FONTDIFF weight under bold simulation) | | |
| tmOverhang | `lOverhang @0x5e8e8`: **0 for TrueType** (only fonts with `flInfo & (BITMAP|STROKE)` and no device simulation). Italic sim gives `(cyMax-1)/2`; bold sim is a cold path (1 px for bitmap fonts [inf]). | | |
| tmDigitizedAspectX / Y | TT: `LOGPIXELSY` / `LOGPIXELSX` (note the X/Y swap; equal on square-DPI displays). Bitmap: `ptlAspect.y*k_y` / `ptlAspect.x*k_x`. | | |
| tmFirst/Last/Default/BreakChar | `IFIMETRICS.wcFirst/Last/Default/BreakChar` (TMW_INTERNAL adds the `ch*` bytes) | | |
| tmItalic | `fsSelection & 1` | | |
| tmUnderlined / tmStruckOut | from the **DC's LFONT** (`DC+0x6c8 & 0x20 / & 0x80`), not from the font | | |
| tmPitchAndFamily | `(flInfo & (OPTICALLY_FIXED_PITCH|CONSTANT_WIDTH)) == 0 ? 1 : 0` (TMPF_FIXED_PITCH means *variable*) `| (TT ? 6 : 0) | (STROKE ? 2 : 0) | jWinPitchAndFamily & 0xF0 | TMPF_DEVICE(8)` for device fonts or TT on a non-display DC | | |
| tmCharSet | the charset the mapper chose (DC_ATTR+6, written by `ppfeMapFont`) | | |

The MacAscender/Descender/LineGap fields are hhea and the WinAscender/Descender fields are OS/2 usWin*. This
mapping is [inf], for the same reason as 5.2.

### 6.3 Which inputs the driver controls

The driver controls `fxMaxAscender`, `fxMaxDescender` and the four optional non-linear overrides. Everything else
is linear in `yScale`/`xScale` with GDI's own rounding. So the only places where hinting, VDMX or hdmx can change
TEXTMETRIC are tmHeight, tmAscent and tmDescent, plus any of int lead, ext lead, ave and max for which TTFD
supplies a non-linear value.

### 6.4 Check against recorded data (Arial, upem 2048)

The Arial font constants used here (winAsc 1854, winDesc 434, hhea 1854/-434/67, xAvgCharWidth 904, bbox width
5457) are [inf: standard Arial 7.x values]. Probe P3 must confirm them from `GetFontData`.

| lfHeight | [W7] h/a/d/il/el/ave/max | linear prediction | agrees? |
|---|---|---|---|
| -15 | 17/14/3/2/0/7/40 | il = 17-15 = 2. el = round(67*15/2048 = 0.49) = 0. ave = round(904*15/2048 = 6.62) = 7. max = round(5457*15/2048 = 39.97) = 40 | yes |
| -16 | 18/15/3/2/1/7/43 | il 2. el = round(0.52) = **1**. ave round(7.06) = 7. max round(42.6) = 43 | yes |
| -11 | 14/11/3/3/0/5/29 | **tmHeight linear = 2288*11/2048 = 12.29 -> 12, measured 14**. il = 14-11 = 3. ave round(4.86) = 5. max round(29.3) = 29 | il/el/ave/max yes; **height from the driver** |
| -12 | 15/12/3/3/0/5/32 | linear height 13.4, measured 15 | driver |

So in GDI:

- `tmMaxCharWidth` follows the font **bounding-box width** (`head.xMax - head.xMin`), not
  `hhea.advanceWidthMax`. That is the TTFD value of `fwdMaxCharInc` [inf]. It also explains why the Segoe UI -12
  max is 31 on Win10 and 23 on Win7 (a newer segoeui.ttf with a wider bbox [inf; check with P3]).
- At small sizes the cell height comes from the driver (VDMX / hinted extremes), not the linear formula [inf,
  strongly suggested].

### 6.5 Text extents and widths

- **`GetTextExtentPoint32W`**. User-mode fast path `gdi32full!GetTextExtentPointWInternal @0xe8dc` ->
  `bComputeTextExtent @0x10ba0`. It is taken only if:
  - every char is < 0x80;
  - `GetDCDpiScaleValue(hdc) == 1` (no GDI scaling);
  - no RTL/LPK complication.

  It sums the 16-bit 28.4 advances cached in the CFONT (or uses `n * fixedWidth`), adds char-extra
  (`SetTextCharacterExtra`) and break-extra (`SetTextJustification`) and the overhang, and returns
  `cx = (sum + 8) >> 4`. **`cy` = the cached tmHeight.**

  Otherwise the call goes to `NtGdiGetTextExtent` -> `RFONTOBJ::bTextExtent @0x25044`, which does the same thing
  in the kernel: `cx = (Σ fxD + extras + overhang*16 + 8) >> 4`, `cy = RFONT+0x164 = tmHeight`. The advances are
  summed in 28.4 and **rounded once** [code]. The [W10] "extent == sum of widths, cy == tmHeight" agrees.
- **`GetTextExtentExPointW`** (`@0x518a0`) goes through `LpkGetTextExtentExPoint` / `GreGetTextExtentExW @0x15f140`.
  The partial extents (`alpDx`) are cumulative sums of the same advances.
- **`GetCharWidth32W`** -> `GreGetCharWidthW @0xf70d4`. The integer form returns `round((fxD + overhang*16) *
  dev2world)` per glyph, so it **includes tmOverhang**. The float form (`GetCharWidthFloatW`) returns
  `fxD/16 * dev2world` without overhang.
- **`GetCharABCWidthsW`** -> `GreGetCharABCWidthsW @0xf1738`. For each glyph: `A = round(fxA)`,
  `B = round(fxAB) - A`, `C = round(fxD) - round(fxAB)`. So **A+B+C == round(fxD) == GetCharWidth32 - overhang**.
  The integer request fails when the font driver lacks an entry (driver object +0xc08). This is presumably the
  documented "TrueType only" rule [inf].
- So for TrueType: extent, sum of CharWidth32 and sum of (A+B+C) agree whenever the driver returns integer-pixel
  advances. Hinted GDI TrueType does that [inf; the CC30/NP probes recorded equality on Win10].

---

## 7. What the font driver decides (not in the corpus)

The win32k side hands the driver a float notional-to-device transform and asks it for `QFD_MAXEXTENTS`, glyph
metrics (`fxD`, `fxA`, `fxAB`) and bitmaps. On Win10 that driver is the user-mode font driver: `UmfdHostLifeTimeManager`,
`UmfdQueryFontCapsEx` and the `UmfdEsc*` callbacks in win32kfull, with the TTFD code inside `fontdrvhost.exe`.
**`fontdrvhost.exe` is not in the corpus.** The following are therefore **[inf]** and need the probes in section 12:

- **ppem from a positive lfHeight.** Known GDI behaviour: TTFD uses the VDMX table to pick the largest ppem
  whose yMax-yMin fits the cell. The linear float is only the fallback. P4 sweeps lfHeight +8..+40 against
  `GetOutlineTextMetrics.otmEMSquare*scale` and VDMX.
- **tmAscent/tmDescent at small ppem** come from VDMX (yMax / -yMin) when the font has a VDMX record for that
  ppem, else from hinted or rounded win metrics. Corroboration from the same company's code: `dwrite!FontFace::GetGdiCompatibleMetrics
  @0x6f454` (DWrite's "GDI compatible" mode).
  - It rounds `ppem = emSize*pixelsPerDip*scale` half up.
  - It looks up `GetDisplayLineSpacing(ppem)` (a binary search of VDMX records) for 0 < ppem < 255.
  - If a VDMX record exists, it uses ascent = round(yMax) and descent = round(-yMin), half up.
  - Otherwise it uses `RoundDesignValueToPixelGrid` on the design ascent/descent.

  This matches the Arial -11 -> 14 (11+3) observation above.
- **GASP.** It decides grid-fit and antialias per ppem for rendering. It does not change advance widths in GDI
  (BW and greyscale use the same hinted advances) [inf]. The dwrite equivalent is `InstanceGaspTable @0x104740`.
- **Embedded bitmaps (EBLC/EBDT).** TTFD uses a strike only on an exact ppem match (dwrite's twin is
  `fs_FindBlocForPpem @0x20d54` -> `sfac_SearchForStrike(ppemX, ppemY)`, which is an exact search). Whether
  segoeui.ttf / tahoma.ttf on 22H2 carry EBLC at all is **unknown**. P3 dumps the table directory.
- **hdmx/LTSH.** These give per-ppem integer advances, which become `fxD` and so GetCharWidth and the extents.
  They are the likely source of the `lNonLinear*` overrides [inf].

---

## 8. GetTextFaceW / GetTextFaceAliasW

`gdi32full!GetTextFaceW @0x66e0` and `GetTextFaceAliasW @0x1a30` both call `NtGdiGetTextFaceW` ->
`win32kfull!GreGetTextFaceW @0x5af50`. The alias variant passes a non-zero 4th argument. [code]:

- It **re-runs the mapper**: `LFONTOBJ::ppfeMapFont` on the DC's current LFONT. For the stock LFONT types
  4/5/6, it first swaps in the PDEV default fonts.
- If result flag **bit 1** is set (3.3: matched through a substitute, or found under the face-name hash) and the
  LOGFONT face is not empty, it returns **the LOGFONT's lfFaceName** (case as given).
- Otherwise it returns `IFIMETRICS.dpwszFamilyName` of the realised face. If the font is
  `FM_INFO_FAMILY_EQUIV` (a multi-string of alias family names) and this is the Alias call, it returns the alias
  that equals the requested name.
- The return value is the count including the NUL. The output is truncated to `c` and always NUL-terminated.

So: "MS Shell Dlg" -> "MS Shell Dlg", "MS Shell Dlg 2" -> "MS Shell Dlg 2", "Helv" -> "Helv" ([W7] fonts_map
agrees). An unknown face or an empty face -> the substituted real family ("Arial" for `default_h0`, [W10]).

---

## 9. Charset-aware substitution: a worked example

Request "Arial", lfCharSet 204 (RUSSIAN):

1. FontSubstitutes has no `ARIAL,204` entry on a stock system [inf; probe P1]. So bucket 0 is "Arial".
2. `bNearMatch`: lfCharSet is explicit. Arial's `dpCharSets` contains 204, so the score is 0. The chosen charset
   is 204 and its code page 1251 goes into the result flags. `GetTextMetrics.tmCharSet` = 204.

Request "Arial", lfCharSet 128 (SHIFTJIS):

1. Arial lacks 128, so it is **rejected** (not penalised).
2. The name step fails, so `bGetFaceName` builds the signature `0x0080` and looks the default face up in
   FontMapper. The step-7 rescan of 3.2 then follows.

Request "MS Shell Dlg", any charset: the face flag `0x8000000` makes the charset come from `jMapCharset` with no
penalty, so a localised Microsoft Sans Serif picks the system charset.

`user32!CreateDlgFont @0x3ca4` / `FixupDlgFaceName @0x3f58` [code]:

- A dialog font "MS Shell Dlg" in a DIALOGEX template with **DS_SHELLFONT (DS_SETFONT|DS_FIXEDSYS = 0x48)** and a
  template version >= 0x400 is renamed **"MS Shell Dlg 2"**, and its charset is set to the ACP charset
  (`GetACPCharSet`).
- Plain "MS Shell Dlg" keeps the name, also with the ACP charset.
- The point size becomes `lfHeight = -((pt*dpi + 36)/72)` (8 pt at 96 DPI -> **-11**; 9 pt -> -12).
- After creation, a non-shell face is checked with `GetTextFaceAliasW` + `IsFontNotGood`, and the font creation
  is retried with DEFAULT_CHARSET.
- comctl32's property sheet has its own `ShouldUseMSShellDlg2Font @0x37cbc`. It returns true when every page is
  DS_SHELLFONT.

---

## 10. Stock and default fonts

[code], `bInitStockFontsInternal @0x39353c`, `hfontInitDefaultGuiFont @0x39334c`, `FinishStockFontInit @0x234b0`,
`FinishStockFontInitInternal @0x1c994`, `bInitSystemFont @0x39532c`, `GreGetDpiDepDefaultGuiFont (win32kbase) @0x1438e0`:

| stock object | built-in LOGFONT before registry overrides |
|---|---|
| SYSTEM_FONT (13) | "System", h 16, w 7, weight 700, OUT_STRING_PRECIS, CLIP_STROKE_PRECIS, PROOF_QUALITY, VARIABLE_PITCH|FF_SWISS (0x22). Replaced by the `SystemFont`/`SystemFontSize` file (the raster face whose `winAsc+winDesc == SystemFontSize`; a TT file is used with lfHeight = size), then by the `FONTS.FON` value of the Hardware-Profile/Gre_Initialize/SmallFont/LargeFont keys. |
| OEM_FIXED_FONT (10) | "Terminal", h 12, w 8, weight 400, OEM_CHARSET (0xFF), FIXED_PITCH|FF_MODERN (0x31). `OEMFONT.FON` overrides it. |
| SYSTEM_FIXED_FONT (16) | from `FIXEDFON.FON`. Without it, it is a copy of SYSTEM_FONT. |
| DEVICE_DEFAULT_FONT (14) | LFONT type 4, FIXED_PITCH. Realised as the **PDEV default font** (PDEV+0x598). |
| ANSI_VAR_FONT (12) | type 5, VARIABLE_PITCH. Realised as PDEV+0x5a0. |
| ANSI_FIXED_FONT (11) | type 6, FIXED_PITCH. Realised as PDEV+0x5a8. |
| **DEFAULT_GUI_FONT (17)** | "MS Shell Dlg", weight 400, charset = `gjCurCharset`, height **8 pt**. `Gre_Initialize\GUIFont.*` can override it. Finalised as **`lfHeight = -((8*dpi + 36)/72)`**: 96 -> **-11**, 120 -> -13, 144 -> -16. `gahStockObjects96[17]` is the 96-DPI copy. Per-DPI plateau copies (120..480 in steps of 24) are created on demand for per-monitor-aware threads (`GreEnsureDpiDepDefaultGuiFontForPlateau`). |
| `gppfeMapperDefault` (the emergency face) | the SystemFont PFE. Otherwise the face named "SYSTEM" loaded from **`\SystemRoot\System32\winsrv.dll`**. |

The [W10] DEFAULT_GUI_FONT = "-11 MS Shell Dlg, 400, cs 0" agrees with this.

---

## 11. Other APIs in scope (where they go)

| API | path [code] | relevant semantics |
|---|---|---|
| `GetOutlineTextMetricsW` | `gdi32full @0x220e0` -> `GreGetOutlineTextMetricsInternalW @0x620d4` -> `cjIFIMetricsToOTMW @0x61754` | see the notes below |
| `GetFontData` | `gdi32full!GetFontDataImpl @0x20df0` -> `NtGdiGetFontData @0x5fbc0` -> `ulGetFontData @0x5fcc8` / `ulGetFontData2 @0x5fd4c` | raw sfnt table bytes from the realised font's file (PFF). This is the tool for P3. |
| `GetGlyphOutlineW` | `GetGlyphOutlineInternalW @0x2240c` -> `GreGetGlyphOutlineInternal @0x264bc` | driver glyph data. GGO_METRICS returns the same `fxD`-based advance. |
| `GetKerningPairsW` | `@0x51140` -> `GreGetKerningPairs @0x134864` -> `PFEOBJ::cKernPairs @0x134d30` | notional pairs scaled by the RFONT x-scale |
| `EnumFontFamiliesExW` | `@0x3b00` -> `EnumEngineOnly` / `EnumDeviceAndEngine`, `PFEOBJ::bFilteredOut @0x65e1c`, `EFSOBJ::bAdd/WriteEFE` | see the notes below |
| FontLink | `ParseFontLinkEntry @0x1ff4c` (SystemLink value `file,Face[,scaleX,scaleY]`), `bAddFlEntry`, `RFONTOBJ::vInitEUDC`, `bIsLinkedGlyph` | linked fonts are consulted per glyph, after mapping. `jMapCharset` also searches linked fonts for the charset. |

GetOutlineTextMetricsW notes:

- It fails (returns 0) if `fwdUnitsPerEm == 0` or `winAsc + winDesc == 0`.
- `otmTextMetrics` comes from the same function as GetTextMetrics.
- Scaled by the y-scale (round half away): `otmAscent`/`otmDescent`/`otmLineGap` = `fwdTypoAscender/Descender/LineGap`
  ([inf] OS/2 sTypo*), `otmMacAscent/Descent/LineGap` = `fwdMac*` ([inf] hhea), and `otmsXHeight`,
  `otmsCapEmHeight`, the strikeout and underscore fields, and `rcFontBox`.
- Not scaled: `otmEMSquare = fwdUnitsPerEm`, `otmusMinimumPPEM = fwdLowestPPEm`.
- **Quirk:** in the scaled path, `otmptSuperscriptSize` is computed from the **subscript** size fields (IFI
  +0x54/+0x56 instead of +0x5c/+0x5e). The notional-unit path uses the right fields. Probe P5 checks this.

EnumFontFamiliesExW notes:

- It skips `FM_INFO_DO_NOT_ENUMERATE`.
- With an aspect filter, it skips raster fonts whose `ptlAspect` != device aspect.
- With a TT-only filter, it skips raster fonts.
- The charset filter goes through `jMapCharset` (DEFAULT_CHARSET enumerates every charset).

---

## 12. Probes that turn the [inf] items into data (for the recorder roles)

All probes are batchable in one Win10 boot. Output is one line per record.

| id | what to record | settles |
|---|---|---|
| P1 | `reg query` of `...\FontSubstitutes`, `...\FontMapper`, `...\FontMapperFamilyFallbackDeprecated`, `...\Gre_Initialize` (+SmallFont/LargeFont), `...\Windows` (SystemFont*), `HKLM\System\CurrentControlSet\Hardware Profiles\Current\Software\Fonts`, `...\FontLink\SystemLink` (Segoe UI, Tahoma, Microsoft Sans Serif, Arial) | 2.1-2.3, 9, 10 |
| P2 | For the faces {Segoe UI, MS Shell Dlg, MS Shell Dlg 2, Tahoma, Microsoft Sans Serif, Arial, Courier New, Courier, MS Sans Serif, System, Fixedsys, Terminal, "", NoSuchFace} x lfHeight {-8..-24, 0, +8..+30} x weight {0, 400, 700} x charset {0, 1, 204} x quality {0, 3, 5, 6} on a GM_COMPATIBLE **and** a GM_ADVANCED screen DC: GetTextFace, GetTextFaceAlias, the full TEXTMETRIC, the 52-letter extent, GetCharWidth32 'A'..'z', GetCharABCWidths 'A'..'z' | 3-6, 8; the MS Shell Dlg snap (5.3) in both graphics modes |
| P3 | `GetFontData` table directory + `head`, `hhea`, `OS/2`, `VDMX`, `gasp`, `hdmx` (first/last record), `EBLC` presence for each realised UI face, with the sha256 of each table | 5.2, 6.4, 7 (the font constants used in 6.4) |
| P4 | Positive-lfHeight sweep +8..+40 for Arial, Tahoma, Segoe UI, Microsoft Sans Serif: tmHeight, tmInternalLeading (-> realised ppem = tmHeight - il) vs the VDMX records from P3 | 7 (ppem choice for cell heights) |
| P5 | `GetOutlineTextMetricsW` for the same faces at -11/-12/-16: dump every field | 11 (sub/superscript quirk), typo vs win metrics |
| P6 | DPI: repeat P2 rows for Segoe UI -12, MS Shell Dlg -11, DEFAULT_GUI_FONT under a 120/144-DPI boot or per-monitor-v2 threads; GetObject(DEFAULT_GUI_FONT) per awareness context | 3.1 DPI rule, 10 plateau fonts |
| P7 | `GetObject` of stock fonts 10..17 and GetTextFace/TEXTMETRIC of each when selected | 1, 10 (PDEV default font LOGFONTs) |

---

## 13. Consequences for Industrial7 (inference, for the D12 decision)

1. **Face substitution order to replicate:**
   1. the exact name first (face hash, then family hash);
   2. the FontSubstitutes alias as a second bucket. A charset-specific entry only matches when lfCharSet equals
      its from-charset, and then replaces the charset.
   3. Only if nothing matched by name: FontMapper signature -> default face. This is roughly what
      `map::win7_default_family` hard-codes.

   GetTextFace must return the requested name when the match came through an alias.
2. **TrueType height is a transform, not a choice.** Industrial7 does not need a height penalty for TT faces. It
   does need, in GM_COMPATIBLE:
   - the **MS Shell Dlg height snap** (5.3);
   - the **lfHeight 0 = -(device default lfHeight)** rule (5.4).
3. **TEXTMETRIC rules (6.2) are mechanical.** Implement them exactly:
   - tmDescent = tmHeight - tmAscent;
   - il = tmHeight - em;
   - el = linear hhea-minus-win excess;
   - ave/max linear from xAvgCharWidth and the bbox width;
   - round half away from zero.

   The only font-file-specific pixel decisions are the ascender/descender (VDMX/hinting) and the advances
   (hinting/hdmx). Those are exactly what the P2/P3/P4 data tables can pin down per (face, ppem).
4. **micross.ttf and MS Shell Dlg -11.** [W10]/[W7] give 13/11/2 for MS Shell Dlg -11 = Microsoft Sans Serif at
   ppem 11. Arial at ppem 11 gives 14/11/3. Without micross.ttf, Industrial7's D12-on fallback is Arial, hence
   "1 px off".
   - With micross.ttf, MS Shell Dlg needs no snap at -11 itself.
   - But the -12 -> -11 and -14 -> -13 snaps (5.3) only reproduce if FontSubstitutes maps MS Shell Dlg to
     Microsoft Sans Serif **and** the snap is implemented.
   - Without micross.ttf, Win10 behaviour cannot be matched by substituting another face.

---

## Appendix: function index (win32kfull unless noted)

| function | rva | role |
|---|---|---|
| `ppfeGetAMatch` | 0x59b40 | top-level search |
| `MAPPER::MAPPER` | 0x57880 | normalise the request, special names |
| `MAPPER::bFoundExactMatch` | 0x59d60 | substitutes and buckets |
| `MAPPER::bNearMatch` | 0x5a300 | penalty |
| `MAPPER::bGetFaceName` / `FindFaceName` | 0x1c4dc / 0x1c640 | FontMapper signature |
| `MAPPER::bFindBitmapFont` | 0x1c67c | exact-height raster check |
| `MAPPER::bWin31BitmapHeightScaling` / `...WidthScaling` | 0x115cb4 / 0x155f10 | integral bitmap scaling |
| `MAPPER::ppfeSynthesizeAMatch`, `vAttemptDeviceMatch`, `vEmergency`, `vSetBest`, `bNoMatch` | 0x10d194, 0x293798, 0x293954, 0x293a90, 0x5d3a8 | |
| `jMapCharset` | 0x5d66c | charset pick |
| `pfsubAlternateFacename` | 0x1e8a8 | alias lookup (bare entries) |
| `vInitFontSubTable`, `QueryRegistryFontSubstituteListRoutine`, `vProcessEntry` | 0x394b80, 0x394440, 0x110be8 | FontSubstitutes |
| `InitializeFontSignatures`, `DefaultFontQueryRoutine` | 0x394d4c, 0x394570 | FontMapper |
| `vInitFontMapperFamilyFallbackTable` | 0x394ef0 | FamilyFallback |
| `LFONTOBJ::ppfeMapFont` | 0x5b3a0 | per-LFONT cache, AA/ClearType flags from lfQuality |
| `PFEOBJ::bSetFontXform` | 0x2610c | selects Win31 / advanced |
| `bGetNtoD_Win31` / `bGetNtoW_Win31` | 0x58e18 / 0x596f8 | GM_COMPATIBLE height (MS Shell Dlg snap) |
| `bGetNtoD` / `vGetNtoW` | 0x5cf60 / 0x5d3f4 | GM_ADVANCED height |
| `lGetDefaultWorldHeight` | 0x14d638 | lfHeight 0 |
| `RFONTOBJ::bRealizeFont` / `bGetDEVICEMETRICS` | 0x62fd8 / 0x62ad8 | driver extents |
| `vIFIMetricsToTextMetricWStrict`, `IFIOBJR::IFIOBJR`, `RFONTOBJ::lOverhang` | 0x5d88c, 0x5d6e0, 0x5e8e8 | TEXTMETRIC |
| `GreGetTextMetricsW`, `vGetTextMetrics`, `vIFIMetricsToTextMetricW` | 0x24d94, 0x24e40, 0x672d4 | |
| `RFONTOBJ::bTextExtent`, `GreGetCharWidthW`, `GreGetCharABCWidthsW` | 0x25044, 0xf70d4, 0xf1738 | extents and widths |
| `cjIFIMetricsToOTMW` | 0x61754 | OUTLINETEXTMETRIC |
| `GreGetTextFaceW` | 0x5af50 | face name |
| `bFToL` | 0xf05f8 | float -> long rounding |
| stock fonts | 0x39353c, 0x39334c, 0x234b0, 0x1c994, 0x39532c | |
| gdi32full `CreateFontIndirectW/ExW`, `GetTextMetricsW`, `GetTextExtentPointWInternal`, `bComputeTextExtent`, `GetObjectW` | 0x6140, 0x61e0, 0x15010, 0xe8dc, 0x10ba0, 0x19f60 | user mode |
| user32 `CreateDlgFont`, `FixupDlgFaceName`, `FixupDlgLogFont` | 0x3ca4, 0x3f58, 0x4000 | dialog fonts |
| dwrite `FontFace::GetGdiCompatibleMetrics`, `GetDisplayLineSpacing`, `fs_FindBlocForPpem` | 0x6f454, 0x8d45c, 0x20d54 | corroboration for 7 |
