# Coverage of the gdi32 / gdi32full export surface

Generated 2026-11-15 by `python3 tools/gen_coverage_md.py` from the repository at that date. Do not edit by hand.

- Oracle: Windows 10 22H2 build 19045.2965 (oracle guest of the recordings and tests). The scan itself is static and does not run anything on the guest; whether the tests pass there is stated in `STATE.md` and in each area's `EVIDENCE.md`.
- Export lists: `tools/gdi32_exports.txt`, `tools/gdi32full_exports.txt` (distinct names; a name exported by both counts once).
- Scan: `python3 tools/coverage.py . tools/gdi32_exports.txt tools/gdi32full_exports.txt` (also writes `tools/uncovered.txt`). It prints `1443 exports; 146 called; 1297 not called; 1282 never mentioned`; this file was generated from the same scan.
- The scan is static and name based. It sees `U(Name)`, `UN(Name)`, `UIN("dll", Name)`, `t_proc("Name")` and `t_proc_in(module, "Name")`; macros that build names are invisible. It cannot tell whether a test checks the result it gets: "behavioural" below means a test in this repository calls the export, and the area's `EVIDENCE.md` says what is asserted.

## Summary

| Class | Exports |
|-------|--------:|
| behavioural (called by a test) | 140 |
| observe-only (called by a test, nothing asserted about the export) | 6 |
| resolve-only (a test only checks that it exists) | 0 |
| probe / recorder only | 4 |
| delegated to the sibling corpus | 212 |
| uncovered, with a recorded reason | 1080 |
| uncovered, no reason recorded | 1 |
| **total distinct names** | **1443** |

Probe/recorder-only means a recorder under `probes/` calls the export and no conformance test does; the data lives in `data/win10/`.

## Behavioural (140)

Called by at least one conformance test. Which results are asserted is in `tests/<area>/EVIDENCE.md` (new areas) or in the test source (older areas).

| Export | Module | Test area(s) |
|--------|--------|------------|
| `AddFontMemResourceEx` | gdi32+gdi32full | font_resources |
| `AddFontResourceA` | gdi32+gdi32full | font_resources |
| `AddFontResourceExA` | gdi32+gdi32full | font_resources |
| `AddFontResourceExW` | gdi32+gdi32full | font_resources |
| `AddFontResourceW` | gdi32 | font_resources |
| `AnyLinkedFonts` | gdi32+gdi32full | font_enum_order |
| `CloseEnhMetaFile` | gdi32+gdi32full | kerning_placement |
| `CreateCompatibleBitmap` | gdi32+gdi32full | kerning_placement, text_output_extents |
| `CreateCompatibleDC` | gdi32+gdi32full | char_widths, control_heights, font_enum_order, font_inventory, font_metrics, font_resources, glyph_bitmaps, kerning_placement, text_output_extents |
| `CreateDIBSection` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `CreateEnhMetaFileW` | gdi32+gdi32full | kerning_placement |
| `CreateFontA` | gdi32+gdi32full | font_enum_order |
| `CreateFontIndirectA` | gdi32+gdi32full | face_substitution, font_enum_order, font_inventory |
| `CreateFontIndirectExA` | gdi32+gdi32full | font_enum_order |
| `CreateFontIndirectExW` | gdi32+gdi32full | face_substitution, font_enum_order |
| `CreateFontIndirectW` | gdi32+gdi32full | char_widths, control_heights, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, glyph_bitmaps, glyph_indices_and_ranges, kerning_placement, outline_and_abc, text_output_extents, ui_metrics |
| `CreateFontW` | gdi32+gdi32full | dpi_scaling, face_substitution, font_enum_order, font_resources |
| `CreateScalableFontResourceA` | gdi32+gdi32full | font_resources |
| `CreateScalableFontResourceW` | gdi32 | font_resources |
| `DPtoLP` | gdi32+gdi32full | text_output_extents |
| `DeleteDC` | gdi32+gdi32full | control_heights, font_enum_order, font_inventory, font_metrics, font_resources, glyph_bitmaps, kerning_placement, text_output_extents |
| `DeleteEnhMetaFile` | gdi32+gdi32full | kerning_placement |
| `DeleteObject` | gdi32 | char_widths, control_heights, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, font_resources, glyph_bitmaps, glyph_indices_and_ranges, kerning_placement, outline_and_abc, text_output_extents, ui_metrics |
| `EnumFontFamiliesA` | gdi32+gdi32full | font_enum_order |
| `EnumFontFamiliesExA` | gdi32+gdi32full | font_enum_order |
| `EnumFontFamiliesExW` | gdi32+gdi32full | face_substitution, font_enum_order, font_resources |
| `EnumFontFamiliesW` | gdi32+gdi32full | font_enum_order |
| `EnumFontsA` | gdi32+gdi32full | font_enum_order |
| `EnumFontsW` | gdi32+gdi32full | font_enum_order |
| `ExtTextOutA` | gdi32+gdi32full | text_output_extents |
| `ExtTextOutW` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `FontIsLinked` | gdi32+gdi32full | font_enum_order |
| `GdiFlush` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `GdiGetCharDimensions` | gdi32+gdi32full | text_output_extents |
| `GdiGetCodePage` | gdi32 | glyph_indices_and_ranges |
| `GetBkColor` | gdi32+gdi32full | text_output_extents |
| `GetBkMode` | gdi32+gdi32full | text_output_extents |
| `GetCharABCWidthsA` | gdi32+gdi32full | outline_and_abc |
| `GetCharABCWidthsFloatA` | gdi32+gdi32full | outline_and_abc |
| `GetCharABCWidthsFloatI` | gdi32+gdi32full | outline_and_abc |
| `GetCharABCWidthsFloatW` | gdi32+gdi32full | char_widths, outline_and_abc |
| `GetCharABCWidthsI` | gdi32+gdi32full | char_widths, glyph_indices_and_ranges, outline_and_abc |
| `GetCharABCWidthsW` | gdi32+gdi32full | char_widths, dpi_scaling, face_substitution, font_inventory, font_metrics, glyph_bitmaps, outline_and_abc, text_output_extents |
| `GetCharWidth32A` | gdi32+gdi32full | char_widths, outline_and_abc, text_output_extents |
| `GetCharWidth32W` | gdi32+gdi32full | char_widths, dpi_scaling, face_substitution, font_inventory, font_metrics, glyph_bitmaps, kerning_placement, outline_and_abc, text_output_extents, ui_metrics |
| `GetCharWidthA` | gdi32+gdi32full | outline_and_abc |
| `GetCharWidthFloatA` | gdi32+gdi32full | outline_and_abc |
| `GetCharWidthFloatW` | gdi32+gdi32full | face_substitution, outline_and_abc |
| `GetCharWidthI` | gdi32+gdi32full | char_widths, outline_and_abc, text_output_extents |
| `GetCharWidthInfo` | gdi32 | glyph_indices_and_ranges |
| `GetCharWidthW` | gdi32+gdi32full | char_widths, outline_and_abc |
| `GetCharacterPlacementA` | gdi32+gdi32full | kerning_placement |
| `GetCharacterPlacementW` | gdi32+gdi32full | font_metrics, kerning_placement |
| `GetCodePage` | gdi32full | glyph_indices_and_ranges |
| `GetCurrentObject` | gdi32+gdi32full | char_widths, face_substitution, font_metrics, glyph_bitmaps |
| `GetCurrentPositionEx` | gdi32+gdi32full | text_output_extents |
| `GetDeviceCaps` | gdi32+gdi32full | dpi_scaling, font_enum_order, font_metrics, text_output_extents |
| `GetETM` | gdi32+gdi32full | outline_and_abc |
| `GetFontAssocStatus` | gdi32+gdi32full | font_enum_order |
| `GetFontData` | gdi32+gdi32full | face_substitution, font_inventory, glyph_bitmaps, glyph_indices_and_ranges, kerning_placement, outline_and_abc |
| `GetFontFileData` | gdi32+gdi32full | font_resources |
| `GetFontFileInfo` | gdi32+gdi32full | face_substitution |
| `GetFontLanguageInfo` | gdi32+gdi32full | font_metrics, glyph_indices_and_ranges, kerning_placement |
| `GetFontRealizationInfo` | gdi32+gdi32full | face_substitution |
| `GetFontResourceInfoW` | gdi32+gdi32full | font_resources |
| `GetFontUnicodeRanges` | gdi32+gdi32full | face_substitution, font_inventory, glyph_indices_and_ranges |
| `GetGlyphIndicesA` | gdi32+gdi32full | glyph_bitmaps, glyph_indices_and_ranges |
| `GetGlyphIndicesW` | gdi32+gdi32full | char_widths, face_substitution, glyph_bitmaps, glyph_indices_and_ranges, outline_and_abc, text_output_extents |
| `GetGlyphOutlineA` | gdi32+gdi32full | glyph_bitmaps, outline_and_abc |
| `GetGlyphOutlineW` | gdi32+gdi32full | glyph_bitmaps, outline_and_abc |
| `GetGlyphOutlineWow` | gdi32+gdi32full | outline_and_abc |
| `GetKerningPairs` | gdi32 | kerning_placement |
| `GetKerningPairsA` | gdi32+gdi32full | kerning_placement, outline_and_abc |
| `GetKerningPairsW` | gdi32+gdi32full | char_widths, glyph_indices_and_ranges, kerning_placement |
| `GetLayout` | gdi32+gdi32full | kerning_placement |
| `GetMapMode` | gdi32+gdi32full | text_output_extents |
| `GetObjectA` | gdi32+gdi32full | face_substitution, font_enum_order, font_inventory, font_metrics, ui_metrics |
| `GetObjectType` | gdi32+gdi32full | face_substitution, ui_metrics |
| `GetObjectW` | gdi32+gdi32full | char_widths, control_heights, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, glyph_bitmaps, ui_metrics |
| `GetOutlineTextMetricsA` | gdi32+gdi32full | outline_and_abc |
| `GetOutlineTextMetricsW` | gdi32+gdi32full | font_inventory, font_metrics, glyph_bitmaps, glyph_indices_and_ranges, outline_and_abc |
| `GetRasterizerCaps` | gdi32+gdi32full | glyph_indices_and_ranges |
| `GetStockObject` | gdi32+gdi32full | char_widths, control_heights, dpi_scaling, font_metrics, ui_metrics |
| `GetStringBitmapA` | gdi32+gdi32full | outline_and_abc |
| `GetStringBitmapW` | gdi32 | outline_and_abc |
| `GetTextAlign` | gdi32+gdi32full | text_output_extents |
| `GetTextCharacterExtra` | gdi32+gdi32full | kerning_placement |
| `GetTextCharset` | gdi32+gdi32full | glyph_indices_and_ranges |
| `GetTextCharsetInfo` | gdi32+gdi32full | glyph_indices_and_ranges |
| `GetTextColor` | gdi32+gdi32full | text_output_extents |
| `GetTextExtentExPointA` | gdi32+gdi32full | text_output_extents |
| `GetTextExtentExPointI` | gdi32+gdi32full | text_output_extents |
| `GetTextExtentExPointW` | gdi32+gdi32full | char_widths, font_metrics, kerning_placement, text_output_extents |
| `GetTextExtentPoint32A` | gdi32+gdi32full | dpi_scaling, glyph_bitmaps, kerning_placement, text_output_extents |
| `GetTextExtentPoint32W` | gdi32+gdi32full | char_widths, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, glyph_bitmaps, kerning_placement, text_output_extents, ui_metrics |
| `GetTextExtentPointA` | gdi32+gdi32full | text_output_extents |
| `GetTextExtentPointI` | gdi32+gdi32full | text_output_extents |
| `GetTextExtentPointW` | gdi32+gdi32full | text_output_extents |
| `GetTextFaceA` | gdi32+gdi32full | face_substitution, font_enum_order, font_inventory, font_metrics, glyph_indices_and_ranges |
| `GetTextFaceAliasW` | gdi32+gdi32full | font_enum_order |
| `GetTextFaceW` | gdi32+gdi32full | char_widths, face_substitution, font_enum_order, font_inventory, font_metrics, font_resources, glyph_bitmaps, glyph_indices_and_ranges, kerning_placement, outline_and_abc, text_output_extents |
| `GetTextMetricsA` | gdi32+gdi32full | dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, glyph_bitmaps |
| `GetTextMetricsW` | gdi32+gdi32full | char_widths, control_heights, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, glyph_bitmaps, glyph_indices_and_ranges, outline_and_abc, text_output_extents, ui_metrics |
| `GetViewportExtEx` | gdi32+gdi32full | text_output_extents |
| `GetViewportOrgEx` | gdi32+gdi32full | text_output_extents |
| `GetWindowExtEx` | gdi32+gdi32full | text_output_extents |
| `GetWindowOrgEx` | gdi32+gdi32full | text_output_extents |
| `LPtoDP` | gdi32+gdi32full | text_output_extents |
| `MoveToEx` | gdi32+gdi32full | text_output_extents |
| `OffsetViewportOrgEx` | gdi32+gdi32full | text_output_extents |
| `OffsetWindowOrgEx` | gdi32+gdi32full | text_output_extents |
| `PolyTextOutA` | gdi32+gdi32full | text_output_extents |
| `PolyTextOutW` | gdi32+gdi32full | text_output_extents |
| `RemoveFontMemResourceEx` | gdi32+gdi32full | font_resources |
| `RemoveFontResourceA` | gdi32+gdi32full | font_resources |
| `RemoveFontResourceExA` | gdi32+gdi32full | font_resources |
| `RemoveFontResourceExW` | gdi32+gdi32full | font_resources |
| `RemoveFontResourceW` | gdi32 | font_resources |
| `RestoreDC` | gdi32+gdi32full | text_output_extents |
| `SaveDC` | gdi32+gdi32full | text_output_extents |
| `ScaleViewportExtEx` | gdi32+gdi32full | text_output_extents |
| `ScaleWindowExtEx` | gdi32+gdi32full | text_output_extents |
| `SelectObject` | gdi32+gdi32full | char_widths, control_heights, dpi_scaling, face_substitution, font_enum_order, font_inventory, font_metrics, font_resources, glyph_bitmaps, glyph_indices_and_ranges, kerning_placement, outline_and_abc, text_output_extents, ui_metrics |
| `SetBkColor` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `SetBkMode` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `SetGraphicsMode` | gdi32+gdi32full | outline_and_abc |
| `SetLayout` | gdi32+gdi32full | kerning_placement |
| `SetMapMode` | gdi32+gdi32full | outline_and_abc, text_output_extents |
| `SetMapperFlags` | gdi32+gdi32full | font_enum_order |
| `SetTextAlign` | gdi32+gdi32full | text_output_extents |
| `SetTextCharacterExtra` | gdi32+gdi32full | kerning_placement |
| `SetTextColor` | gdi32+gdi32full | glyph_bitmaps, text_output_extents |
| `SetTextJustification` | gdi32+gdi32full | kerning_placement |
| `SetViewportExtEx` | gdi32+gdi32full | text_output_extents |
| `SetViewportOrgEx` | gdi32+gdi32full | outline_and_abc, text_output_extents |
| `SetWindowExtEx` | gdi32+gdi32full | text_output_extents |
| `SetWindowOrgEx` | gdi32+gdi32full | outline_and_abc, text_output_extents |
| `TextOutA` | gdi32+gdi32full | text_output_extents |
| `TextOutW` | gdi32+gdi32full | glyph_bitmaps, kerning_placement, text_output_extents |
| `TranslateCharsetInfo` | gdi32+gdi32full | glyph_indices_and_ranges |

## Observe-only (6)

Called by a test that reports what happens (`NOTE`) but asserts nothing about the contract of the export; hand-maintained in `tools/coverage_observe_only.txt`.

| Export | Module | Where and why |
|--------|--------|-------------|
| `GetBitmapDpiScaleValue` | gdi32 | tests/text_output_extents dpi.c: undocumented, values reported with NOTE only |
| `GetCurrentDpiInfo` | gdi32 | tests/text_output_extents dpi.c: undocumented, values reported with NOTE only |
| `GetDCDpiScaleValue` | gdi32+gdi32full | tests/text_output_extents dpi.c: undocumented, values reported with NOTE only |
| `QueryFontAssocStatus` | gdi32+gdi32full | tests/font_enum_order: value reported with NOTE; only checks that two calls agree |
| `SetFontEnumeration` | gdi32+gdi32full | tests/font_enum_order: undocumented, run in a child process, result reported with NOTE only |
| `SetLayoutWidth` | gdi32+gdi32full | tests/kerning_placement layout_width_observe: undocumented, no assertions, NOTE lines only |

## Resolve-only (0)

Only an existence check was found; this does not count as coverage.

None.

## Probe / recorder only (4)

Called from a recorder, not from a conformance test.

| Export | Module | Probe(s) |
|--------|--------|--------|
| `BitBlt` | gdi32+gdi32full | dpi_scaling |
| `GetGraphicsMode` | gdi32+gdi32full | font_metrics |
| `ModifyWorldTransform` | gdi32+gdi32full | char_widths |
| `SetWorldTransform` | gdi32+gdi32full | char_widths |

## Delegated to the sibling corpus (212)

Non-text GDI breadth (DCs, bitmaps, regions, paths, metafiles, palettes). Not duplicated here; names are listed, the reason is the same for all.

`AbortPath`, `AngleArc`, `AnimatePalette`, `Arc`, `ArcTo`, `BeginPath`, `CancelDC`, `CheckColorsInGamut`, `ChoosePixelFormat`, `Chord`, `ClearBitmapAttributes`, `ClearBrushAttributes`, `CloseFigure`, `CloseMetaFile`, `ColorCorrectPalette`, `ColorMatchToTarget`, `CombineRgn`, `CombineTransform`, `CopyEnhMetaFileA`, `CopyEnhMetaFileW`, `CopyMetaFileA`, `CopyMetaFileW`, `CreateBitmap`, `CreateBitmapIndirect`, `CreateBrushIndirect`, `CreateColorSpaceA`, `CreateColorSpaceW`, `CreateDCA`, `CreateDCExW`, `CreateDCW`, `CreateDIBPatternBrush`, `CreateDIBPatternBrushPt`, `CreateDIBitmap`, `CreateDiscardableBitmap`, `CreateEllipticRgn`, `CreateEllipticRgnIndirect`, `CreateEnhMetaFileA`, `CreateHalftonePalette`, `CreateHatchBrush`, `CreateICA`, `CreateICW`, `CreateMetaFileA`, `CreateMetaFileW`, `CreatePalette`, `CreatePatternBrush`, `CreatePen`, `CreatePenIndirect`, `CreatePolyPolygonRgn`, `CreatePolygonRgn`, `CreateRectRgn`, `CreateRectRgnIndirect`, `CreateRoundRectRgn`, `CreateSolidBrush`, `DeleteColorSpace`, `DeleteEMFSpoolData`, `DeleteMetaFile`, `DescribePixelFormat`, `Ellipse`, `EndPath`, `EnumEnhMetaFile`, `EnumICMProfilesA`, `EnumICMProfilesW`, `EnumMetaFile`, `EnumObjects`, `EqualRgn`, `ExcludeClipRect`, `ExtCreatePen`, `ExtCreateRegion`, `ExtFloodFill`, `ExtSelectClipRgn`, `FillPath`, `FillRgn`, `FixBrushOrgEx`, `FlattenPath`, `FloodFill`, `FrameRgn`, `GdiAlphaBlend`, `GdiGradientFill`, `GdiTransparentBlt`, `GetArcDirection`, `GetAspectRatioFilterEx`, `GetBitmapAttributes`, `GetBitmapBits`, `GetBitmapDimensionEx`, `GetBoundsRect`, `GetBrushAttributes`, `GetBrushOrgEx`, `GetClipBox`, `GetClipRgn`, `GetColorAdjustment`, `GetColorSpace`, `GetDCBrushColor`, `GetDCOrgEx`, `GetDCPenColor`, `GetDIBColorTable`, `GetDIBits`, `GetEnhMetaFileA`, `GetEnhMetaFileBits`, `GetEnhMetaFileDescriptionA`, `GetEnhMetaFileDescriptionW`, `GetEnhMetaFileHeader`, `GetEnhMetaFilePaletteEntries`, `GetEnhMetaFilePixelFormat`, `GetEnhMetaFileW`, `GetICMProfileA`, `GetICMProfileW`, `GetLogColorSpaceA`, `GetLogColorSpaceW`, `GetMetaFileA`, `GetMetaFileBitsEx`, `GetMetaFileW`, `GetMetaRgn`, `GetMiterLimit`, `GetNearestColor`, `GetNearestPaletteIndex`, `GetPaletteEntries`, `GetPath`, `GetPixel`, `GetPixelFormat`, `GetPolyFillMode`, `GetROP2`, `GetRegionData`, `GetRelAbs`, `GetRgnBox`, `GetStretchBltMode`, `GetSystemPaletteEntries`, `GetSystemPaletteUse`, `GetTransform`, `GetWinMetaFileBits`, `GetWorldTransform`, `IntersectClipRect`, `InvertRgn`, `IsValidEnhMetaRecord`, `IsValidEnhMetaRecordOffExt`, `LineDDA`, `LineTo`, `MaskBlt`, `MirrorRgn`, `OffsetClipRgn`, `OffsetRgn`, `PaintRgn`, `PatBlt`, `PathToRegion`, `Pie`, `PlayEnhMetaFile`, `PlayEnhMetaFileRecord`, `PlayMetaFile`, `PlayMetaFileRecord`, `PlgBlt`, `PolyBezier`, `PolyBezierTo`, `PolyDraw`, `PolyPatBlt`, `PolyPolygon`, `PolyPolyline`, `Polygon`, `Polyline`, `PolylineTo`, `PtInRegion`, `PtVisible`, `RealizePalette`, `RectInRegion`, `RectVisible`, `Rectangle`, `ResizePalette`, `RoundRect`, `ScaleRgn`, `SelectClipPath`, `SelectClipRgn`, `SelectPalette`, `SetArcDirection`, `SetBitmapAttributes`, `SetBitmapBits`, `SetBitmapDimensionEx`, `SetBoundsRect`, `SetBrushAttributes`, `SetBrushOrgEx`, `SetColorAdjustment`, `SetColorSpace`, `SetDCBrushColor`, `SetDCPenColor`, `SetDIBColorTable`, `SetDIBits`, `SetDIBitsToDevice`, `SetEnhMetaFileBits`, `SetICMMode`, `SetICMProfileA`, `SetICMProfileW`, `SetMetaFileBitsEx`, `SetMetaRgn`, `SetMiterLimit`, `SetPaletteEntries`, `SetPixel`, `SetPixelFormat`, `SetPixelV`, `SetPolyFillMode`, `SetROP2`, `SetRectRgn`, `SetRelAbs`, `SetStretchBltMode`, `SetSystemPaletteUse`, `SetWinMetaFileBits`, `StretchBlt`, `StretchDIBits`, `StrokeAndFillPath`, `StrokePath`, `SwapBuffers`, `UnrealizeObject`, `UpdateColors`, `UpdateICMRegKeyA`, `UpdateICMRegKeyW`, `WidenPath`

## Uncovered, with a reason (1080)

Reasons are recorded in `tools/uncovered_justified.txt`; names grouped by reason.

### raw win32u syscall stub; kernel ABI, not a stable contract, behaviour covered through the public wrapper (225)

`NtGdiAbortPath`, `NtGdiAddFontMemResourceEx`, `NtGdiAddFontResourceW`, `NtGdiAddRemoteFontToDC`, `NtGdiAddRemoteMMInstanceToDC`, `NtGdiAlphaBlend`, `NtGdiAngleArc`, `NtGdiAnyLinkedFonts`, `NtGdiArcInternal`, `NtGdiBeginGdiRendering`, `NtGdiBeginPath`, `NtGdiBitBlt`, `NtGdiCancelDC`, `NtGdiChangeGhostFont`, `NtGdiCheckBitmapBits`, `NtGdiClearBitmapAttributes`, `NtGdiClearBrushAttributes`, `NtGdiCloseFigure`, `NtGdiColorCorrectPalette`, `NtGdiCombineRgn`, `NtGdiCombineTransform`, `NtGdiComputeXformCoefficients`, `NtGdiConvertMetafileRect`, `NtGdiCreateBitmap`, `NtGdiCreateBitmapFromDxSurface`, `NtGdiCreateBitmapFromDxSurface2`, `NtGdiCreateClientObj`, `NtGdiCreateColorSpace`, `NtGdiCreateColorTransform`, `NtGdiCreateCompatibleBitmap`, `NtGdiCreateCompatibleDC`, `NtGdiCreateDIBBrush`, `NtGdiCreateDIBSection`, `NtGdiCreateDIBitmapInternal`, `NtGdiCreateEllipticRgn`, `NtGdiCreateHalftonePalette`, `NtGdiCreateHatchBrushInternal`, `NtGdiCreateMetafileDC`, `NtGdiCreatePaletteInternal`, `NtGdiCreatePatternBrushInternal`, `NtGdiCreatePen`, `NtGdiCreateRectRgn`, `NtGdiCreateRoundRectRgn`, `NtGdiCreateServerMetaFile`, `NtGdiCreateSessionMappedDIBSection`, `NtGdiCreateSolidBrush`, `NtGdiDeleteClientObj`, `NtGdiDeleteColorSpace`, `NtGdiDeleteColorTransform`, `NtGdiDeleteObjectApp`, `NtGdiDescribePixelFormat`, `NtGdiDoPalette`, `NtGdiDrawStream`, `NtGdiDwmCreatedBitmapRemotingOutput`, `NtGdiDxgGenericThunk`, `NtGdiEllipse`, `NtGdiEndGdiRendering`, `NtGdiEndPath`, `NtGdiEnsureDpiDepDefaultGuiFontForPlateau`, `NtGdiEnumFonts`, `NtGdiEnumObjects`, `NtGdiEqualRgn`, `NtGdiExcludeClipRect`, `NtGdiExtCreatePen`, `NtGdiExtCreateRegion`, `NtGdiExtFloodFill`, `NtGdiExtGetObjectW`, `NtGdiExtSelectClipRgn`, `NtGdiExtTextOutW`, `NtGdiFillPath`, `NtGdiFillRgn`, `NtGdiFlattenPath`, `NtGdiFlush`, `NtGdiFontIsLinked`, `NtGdiForceUFIMapping`, `NtGdiFrameRgn`, `NtGdiFullscreenControl`, `NtGdiGetAndSetDCDword`, `NtGdiGetAppClipBox`, `NtGdiGetBitmapBits`, `NtGdiGetBitmapDimension`, `NtGdiGetBitmapDpiScaleValue`, `NtGdiGetBoundsRect`, `NtGdiGetCharABCWidthsW`, `NtGdiGetCharSet`, `NtGdiGetCharWidthInfo`, `NtGdiGetCharWidthW`, `NtGdiGetCharacterPlacementW`, `NtGdiGetColorAdjustment`, `NtGdiGetColorSpaceforBitmap`, `NtGdiGetCurrentDpiInfo`, `NtGdiGetDCDpiScaleValue`, `NtGdiGetDCDword`, `NtGdiGetDCObject`, `NtGdiGetDCPoint`, `NtGdiGetDCforBitmap`, `NtGdiGetDIBitsInternal`, `NtGdiGetDeviceCaps`, `NtGdiGetDeviceCapsAll`, `NtGdiGetDeviceWidth`, `NtGdiGetDhpdev`, `NtGdiGetETM`, `NtGdiGetEmbUFI`, `NtGdiGetEntry`, `NtGdiGetFontData`, `NtGdiGetFontFileData`, `NtGdiGetFontFileInfo`, `NtGdiGetFontResourceInfoInternalW`, `NtGdiGetFontUnicodeRanges`, `NtGdiGetGlyphIndicesW`, `NtGdiGetGlyphIndicesWInternal`, `NtGdiGetGlyphOutline`, `NtGdiGetKerningPairs`, `NtGdiGetLinkedUFIs`, `NtGdiGetMiterLimit`, `NtGdiGetNearestColor`, `NtGdiGetNearestPaletteIndex`, `NtGdiGetObjectBitmapHandle`, `NtGdiGetOutlineTextMetricsInternalW`, `NtGdiGetPath`, `NtGdiGetPixel`, `NtGdiGetRandomRgn`, `NtGdiGetRasterizerCaps`, `NtGdiGetRealizationInfo`, `NtGdiGetRegionData`, `NtGdiGetRgnBox`, `NtGdiGetServerMetaFileBits`, `NtGdiGetStats`, `NtGdiGetStringBitmapW`, `NtGdiGetSystemPaletteUse`, `NtGdiGetTextCharsetInfo`, `NtGdiGetTextExtent`, `NtGdiGetTextExtentExW`, `NtGdiGetTextFaceW`, `NtGdiGetTextMetricsW`, `NtGdiGetTransform`, `NtGdiGetUFI`, `NtGdiGetUFIPathname`, `NtGdiGetWidthTable`, `NtGdiGradientFill`, `NtGdiHLSurfGetInformation`, `NtGdiHLSurfSetInformation`, `NtGdiHfontCreate`, `NtGdiIcmBrushInfo`, `NtGdiInit`, `NtGdiIntersectClipRect`, `NtGdiInvertRgn`, `NtGdiLineTo`, `NtGdiMakeFontDir`, `NtGdiMakeInfoDC`, `NtGdiMakeObjectUnXferable`, `NtGdiMakeObjectXferable`, `NtGdiMaskBlt`, `NtGdiMirrorWindowOrg`, `NtGdiModifyWorldTransform`, `NtGdiMonoBitmap`, `NtGdiMoveTo`, `NtGdiOffsetClipRgn`, `NtGdiOffsetRgn`, `NtGdiOpenDCW`, `NtGdiPatBlt`, `NtGdiPathToRegion`, `NtGdiPlgBlt`, `NtGdiPolyDraw`, `NtGdiPolyPatBlt`, `NtGdiPolyPolyDraw`, `NtGdiPolyTextOutW`, `NtGdiPtInRegion`, `NtGdiPtVisible`, `NtGdiQueryFontAssocInfo`, `NtGdiQueryFonts`, `NtGdiRectInRegion`, `NtGdiRectVisible`, `NtGdiRectangle`, `NtGdiRemoveFontMemResourceEx`, `NtGdiRemoveFontResourceW`, `NtGdiRemoveMergeFont`, `NtGdiResizePalette`, `NtGdiRestoreDC`, `NtGdiRoundRect`, `NtGdiSaveDC`, `NtGdiScaleRgn`, `NtGdiScaleValues`, `NtGdiScaleViewportExtEx`, `NtGdiScaleWindowExtEx`, `NtGdiSelectBitmap`, `NtGdiSelectBrush`, `NtGdiSelectClipPath`, `NtGdiSelectFont`, `NtGdiSelectPen`, `NtGdiSetBitmapAttributes`, `NtGdiSetBitmapBits`, `NtGdiSetBitmapDimension`, `NtGdiSetBoundsRect`, `NtGdiSetBrushAttributes`, `NtGdiSetBrushOrg`, `NtGdiSetColorAdjustment`, `NtGdiSetColorSpace`, `NtGdiSetDIBitsToDeviceInternal`, `NtGdiSetFontEnumeration`, `NtGdiSetFontXform`, `NtGdiSetIcmMode`, `NtGdiSetLayout`, `NtGdiSetLinkedUFIs`, `NtGdiSetMetaRgn`, `NtGdiSetMiterLimit`, `NtGdiSetPixel`, `NtGdiSetPixelFormat`, `NtGdiSetRectRgn`, `NtGdiSetSizeDevice`, `NtGdiSetSystemPaletteUse`, `NtGdiSetTextJustification`, `NtGdiSetVirtualResolution`, `NtGdiStretchBlt`, `NtGdiStretchDIBitsInternal`, `NtGdiStrokeAndFillPath`, `NtGdiStrokePath`, `NtGdiSwapBuffers`, `NtGdiTransformPoints`, `NtGdiTransparentBlt`, `NtGdiUnmapMemFont`, `NtGdiUnrealizeObject`, `NtGdiUpdateColors`, `NtGdiUpdateTransform`, `NtGdiWidenPath`

### WDDM kernel-mode thunk; needs a GPU adapter and display driver, not behaviour of GDI text (224)

`D3DKMTAbandonSwapChain`, `D3DKMTAcquireKeyedMutex`, `D3DKMTAcquireKeyedMutex2`, `D3DKMTAcquireSwapChain`, `D3DKMTAddSurfaceToSwapChain`, `D3DKMTAdjustFullscreenGamma`, `D3DKMTCacheHybridQueryValue`, `D3DKMTChangeVideoMemoryReservation`, `D3DKMTCheckExclusiveOwnership`, `D3DKMTCheckMonitorPowerState`, `D3DKMTCheckMultiPlaneOverlaySupport`, `D3DKMTCheckMultiPlaneOverlaySupport2`, `D3DKMTCheckMultiPlaneOverlaySupport3`, `D3DKMTCheckOcclusion`, `D3DKMTCheckSharedResourceAccess`, `D3DKMTCheckVidPnExclusiveOwnership`, `D3DKMTCloseAdapter`, `D3DKMTConfigureSharedResource`, `D3DKMTCreateAllocation`, `D3DKMTCreateAllocation2`, `D3DKMTCreateBundleObject`, `D3DKMTCreateContext`, `D3DKMTCreateContextVirtual`, `D3DKMTCreateDCFromMemory`, `D3DKMTCreateDevice`, `D3DKMTCreateHwContext`, `D3DKMTCreateHwQueue`, `D3DKMTCreateKeyedMutex`, `D3DKMTCreateKeyedMutex2`, `D3DKMTCreateOutputDupl`, `D3DKMTCreateOverlay`, `D3DKMTCreatePagingQueue`, `D3DKMTCreateProtectedSession`, `D3DKMTCreateSwapChain`, `D3DKMTCreateSynchronizationObject`, `D3DKMTCreateSynchronizationObject2`, `D3DKMTCreateTrackedWorkload`, `D3DKMTDDisplayEnum`, `D3DKMTDestroyAllocation`, `D3DKMTDestroyAllocation2`, `D3DKMTDestroyContext`, `D3DKMTDestroyDCFromMemory`, `D3DKMTDestroyDevice`, `D3DKMTDestroyHwContext`, `D3DKMTDestroyHwQueue`, `D3DKMTDestroyKeyedMutex`, `D3DKMTDestroyOutputDupl`, `D3DKMTDestroyOverlay`, `D3DKMTDestroyPagingQueue`, `D3DKMTDestroyProtectedSession`, `D3DKMTDestroySynchronizationObject`, `D3DKMTDestroyTrackedWorkload`, `D3DKMTDispMgrCreate`, `D3DKMTDispMgrOperation`, `D3DKMTDispMgrSourceOperation`, `D3DKMTDispMgrTargetOperation`, `D3DKMTDisplayPortOperation`, `D3DKMTDuplicateHandle`, `D3DKMTEnumAdapters`, `D3DKMTEnumAdapters2`, `D3DKMTEnumAdapters3`, `D3DKMTEscape`, `D3DKMTEvict`, `D3DKMTExtractBundleObject`, `D3DKMTFlipOverlay`, `D3DKMTFlushHeapTransitions`, `D3DKMTFreeGpuVirtualAddress`, `D3DKMTGetAllocationPriority`, `D3DKMTGetAvailableTrackedWorkloadIndex`, `D3DKMTGetCachedHybridQueryValue`, `D3DKMTGetContextInProcessSchedulingPriority`, `D3DKMTGetContextSchedulingPriority`, `D3DKMTGetDWMVerticalBlankEvent`, `D3DKMTGetDeviceState`, `D3DKMTGetDisplayModeList`, `D3DKMTGetMemoryBudgetTarget`, `D3DKMTGetMultiPlaneOverlayCaps`, `D3DKMTGetMultisampleMethodList`, `D3DKMTGetOverlayState`, `D3DKMTGetPostCompositionCaps`, `D3DKMTGetPresentHistory`, `D3DKMTGetPresentQueueEvent`, `D3DKMTGetProcessDeviceRemovalSupport`, `D3DKMTGetProcessList`, `D3DKMTGetProcessSchedulingPriorityBand`, `D3DKMTGetProcessSchedulingPriorityClass`, `D3DKMTGetResourcePresentPrivateDriverData`, `D3DKMTGetRuntimeData`, `D3DKMTGetScanLine`, `D3DKMTGetSetSwapChainMetadata`, `D3DKMTGetSharedPrimaryHandle`, `D3DKMTGetSharedResourceAdapterLuid`, `D3DKMTGetTrackedWorkloadStatistics`, `D3DKMTGetYieldPercentage`, `D3DKMTInvalidateActiveVidPn`, `D3DKMTInvalidateCache`, `D3DKMTLock`, `D3DKMTLock2`, `D3DKMTMakeResident`, `D3DKMTMapGpuVirtualAddress`, `D3DKMTMarkDeviceAsError`, `D3DKMTNetDispGetNextChunkInfo`, `D3DKMTNetDispQueryMiracastDisplayDeviceStatus`, `D3DKMTNetDispQueryMiracastDisplayDeviceSupport`, `D3DKMTNetDispStartMiracastDisplayDevice`, `D3DKMTNetDispStartMiracastDisplayDevice2`, `D3DKMTNetDispStartMiracastDisplayDeviceEx`, `D3DKMTNetDispStopMiracastDisplayDevice`, `D3DKMTOfferAllocations`, `D3DKMTOpenAdapterFromDeviceName`, `D3DKMTOpenAdapterFromGdiDisplayName`, `D3DKMTOpenAdapterFromHdc`, `D3DKMTOpenAdapterFromLuid`, `D3DKMTOpenBundleObjectNtHandleFromName`, `D3DKMTOpenKeyedMutex`, `D3DKMTOpenKeyedMutex2`, `D3DKMTOpenKeyedMutexFromNtHandle`, `D3DKMTOpenNtHandleFromName`, `D3DKMTOpenProtectedSessionFromNtHandle`, `D3DKMTOpenResource`, `D3DKMTOpenResource2`, `D3DKMTOpenResourceFromNtHandle`, `D3DKMTOpenSwapChain`, `D3DKMTOpenSyncObjectFromNtHandle`, `D3DKMTOpenSyncObjectFromNtHandle2`, `D3DKMTOpenSyncObjectNtHandleFromName`, `D3DKMTOpenSynchronizationObject`, `D3DKMTOutputDuplGetFrameInfo`, `D3DKMTOutputDuplGetMetaData`, `D3DKMTOutputDuplGetPointerShapeData`, `D3DKMTOutputDuplPresent`, `D3DKMTOutputDuplPresentToHwQueue`, `D3DKMTOutputDuplReleaseFrame`, `D3DKMTPinDirectFlipResources`, `D3DKMTPinResources`, `D3DKMTPollDisplayChildren`, `D3DKMTPresent`, `D3DKMTPresentMultiPlaneOverlay`, `D3DKMTPresentMultiPlaneOverlay2`, `D3DKMTPresentMultiPlaneOverlay3`, `D3DKMTPresentRedirected`, `D3DKMTQueryAdapterInfo`, `D3DKMTQueryAllocationResidency`, `D3DKMTQueryClockCalibration`, `D3DKMTQueryFSEBlock`, `D3DKMTQueryProcessOfferInfo`, `D3DKMTQueryProtectedSessionInfoFromNtHandle`, `D3DKMTQueryProtectedSessionStatus`, `D3DKMTQueryRemoteVidPnSourceFromGdiDisplayName`, `D3DKMTQueryResourceInfo`, `D3DKMTQueryResourceInfoFromNtHandle`, `D3DKMTQueryStatistics`, `D3DKMTQueryVidPnExclusiveOwnership`, `D3DKMTQueryVideoMemoryInfo`, `D3DKMTReclaimAllocations`, `D3DKMTReclaimAllocations2`, `D3DKMTRegisterTrimNotification`, `D3DKMTRegisterVailProcess`, `D3DKMTReleaseKeyedMutex`, `D3DKMTReleaseKeyedMutex2`, `D3DKMTReleaseProcessVidPnSourceOwners`, `D3DKMTReleaseSwapChain`, `D3DKMTRemoveSurfaceFromSwapChain`, `D3DKMTRender`, `D3DKMTReserveGpuVirtualAddress`, `D3DKMTResetTrackedWorkloadStatistics`, `D3DKMTSetAllocationPriority`, `D3DKMTSetContextInProcessSchedulingPriority`, `D3DKMTSetContextSchedulingPriority`, `D3DKMTSetDisplayMode`, `D3DKMTSetDisplayPrivateDriverFormat`, `D3DKMTSetDodIndirectSwapchain`, `D3DKMTSetFSEBlock`, `D3DKMTSetGammaRamp`, `D3DKMTSetHwProtectionTeardownRecovery`, `D3DKMTSetMemoryBudgetTarget`, `D3DKMTSetMonitorColorSpaceTransform`, `D3DKMTSetProcessDeviceRemovalSupport`, `D3DKMTSetProcessSchedulingPriorityBand`, `D3DKMTSetProcessSchedulingPriorityClass`, `D3DKMTSetQueuedLimit`, `D3DKMTSetStablePowerState`, `D3DKMTSetStereoEnabled`, `D3DKMTSetSyncRefreshCountWaitTarget`, `D3DKMTSetVidPnSourceHwProtection`, `D3DKMTSetVidPnSourceOwner`, `D3DKMTSetVidPnSourceOwner1`, `D3DKMTSetVidPnSourceOwner2`, `D3DKMTSetYieldPercentage`, `D3DKMTShareObjects`, `D3DKMTSharedPrimaryLockNotification`, `D3DKMTSharedPrimaryUnLockNotification`, `D3DKMTSignalSynchronizationObject`, `D3DKMTSignalSynchronizationObject2`, `D3DKMTSignalSynchronizationObjectFromCpu`, `D3DKMTSignalSynchronizationObjectFromGpu`, `D3DKMTSignalSynchronizationObjectFromGpu2`, `D3DKMTSubmitCommand`, `D3DKMTSubmitCommandToHwQueue`, `D3DKMTSubmitPresentBltToHwQueue`, `D3DKMTSubmitPresentToHwQueue`, `D3DKMTSubmitSignalSyncObjectsToHwQueue`, `D3DKMTSubmitWaitForSyncObjectsToHwQueue`, `D3DKMTTrimProcessCommitment`, `D3DKMTUnOrderedPresentSwapChain`, `D3DKMTUnlock`, `D3DKMTUnlock2`, `D3DKMTUnpinDirectFlipResources`, `D3DKMTUnpinResources`, `D3DKMTUnregisterTrimNotification`, `D3DKMTUpdateAllocationProperty`, `D3DKMTUpdateGpuVirtualAddress`, `D3DKMTUpdateOverlay`, `D3DKMTUpdateTrackedWorkload`, `D3DKMTVailConnect`, `D3DKMTVailDisconnect`, `D3DKMTVailPromoteCompositionSurface`, `D3DKMTWaitForIdle`, `D3DKMTWaitForSynchronizationObject`, `D3DKMTWaitForSynchronizationObject2`, `D3DKMTWaitForSynchronizationObjectFromCpu`, `D3DKMTWaitForSynchronizationObjectFromGpu`, `D3DKMTWaitForVerticalBlankEvent`, `D3DKMTWaitForVerticalBlankEvent2`

### DirectDraw/Direct3D legacy kernel thunk; needs a display driver DDI, not exercisable from a black-box GDI test (138)

`DdCreateFullscreenSprite`, `DdDestroyFullscreenSprite`, `DdEntry0`, `DdEntry1`, `DdEntry10`, `DdEntry11`, `DdEntry12`, `DdEntry13`, `DdEntry14`, `DdEntry15`, `DdEntry16`, `DdEntry17`, `DdEntry18`, `DdEntry19`, `DdEntry2`, `DdEntry20`, `DdEntry21`, `DdEntry22`, `DdEntry23`, `DdEntry24`, `DdEntry25`, `DdEntry26`, `DdEntry27`, `DdEntry28`, `DdEntry29`, `DdEntry3`, `DdEntry30`, `DdEntry31`, `DdEntry32`, `DdEntry33`, `DdEntry34`, `DdEntry35`, `DdEntry36`, `DdEntry37`, `DdEntry38`, `DdEntry39`, `DdEntry4`, `DdEntry40`, `DdEntry41`, `DdEntry42`, `DdEntry43`, `DdEntry44`, `DdEntry45`, `DdEntry46`, `DdEntry47`, `DdEntry48`, `DdEntry49`, `DdEntry5`, `DdEntry50`, `DdEntry51`, `DdEntry52`, `DdEntry53`, `DdEntry54`, `DdEntry55`, `DdEntry56`, `DdEntry6`, `DdEntry7`, `DdEntry8`, `DdEntry9`, `DdNotifyFullscreenSpriteUpdate`, `DdQueryVisRgnUniqueness`, `NtGdiD3dContextCreate`, `NtGdiD3dContextDestroy`, `NtGdiD3dContextDestroyAll`, `NtGdiD3dDrawPrimitives2`, `NtGdiD3dValidateTextureStageState`, `NtGdiDdAddAttachedSurface`, `NtGdiDdAlphaBlt`, `NtGdiDdAttachSurface`, `NtGdiDdBeginMoCompFrame`, `NtGdiDdBlt`, `NtGdiDdCanCreateD3DBuffer`, `NtGdiDdCanCreateSurface`, `NtGdiDdColorControl`, `NtGdiDdCreateD3DBuffer`, `NtGdiDdCreateDirectDrawObject`, `NtGdiDdCreateFullscreenSprite`, `NtGdiDdCreateMoComp`, `NtGdiDdCreateSurface`, `NtGdiDdCreateSurfaceEx`, `NtGdiDdCreateSurfaceObject`, `NtGdiDdDeleteDirectDrawObject`, `NtGdiDdDeleteSurfaceObject`, `NtGdiDdDestroyD3DBuffer`, `NtGdiDdDestroyFullscreenSprite`, `NtGdiDdDestroyMoComp`, `NtGdiDdDestroySurface`, `NtGdiDdEndMoCompFrame`, `NtGdiDdFlip`, `NtGdiDdFlipToGDISurface`, `NtGdiDdGetAvailDriverMemory`, `NtGdiDdGetBltStatus`, `NtGdiDdGetDC`, `NtGdiDdGetDriverInfo`, `NtGdiDdGetDriverState`, `NtGdiDdGetDxHandle`, `NtGdiDdGetFlipStatus`, `NtGdiDdGetInternalMoCompInfo`, `NtGdiDdGetMoCompBuffInfo`, `NtGdiDdGetMoCompFormats`, `NtGdiDdGetMoCompGuids`, `NtGdiDdGetScanLine`, `NtGdiDdLock`, `NtGdiDdLockD3D`, `NtGdiDdNotifyFullscreenSpriteUpdate`, `NtGdiDdQueryDirectDrawObject`, `NtGdiDdQueryMoCompStatus`, `NtGdiDdQueryVisRgnUniqueness`, `NtGdiDdReenableDirectDrawObject`, `NtGdiDdReleaseDC`, `NtGdiDdRenderMoComp`, `NtGdiDdResetVisrgn`, `NtGdiDdSetColorKey`, `NtGdiDdSetExclusiveMode`, `NtGdiDdSetGammaRamp`, `NtGdiDdSetOverlayPosition`, `NtGdiDdUnattachSurface`, `NtGdiDdUnlock`, `NtGdiDdUnlockD3D`, `NtGdiDdUpdateOverlay`, `NtGdiDdWaitForVerticalBlank`, `NtGdiDvpAcquireNotification`, `NtGdiDvpCanCreateVideoPort`, `NtGdiDvpColorControl`, `NtGdiDvpCreateVideoPort`, `NtGdiDvpDestroyVideoPort`, `NtGdiDvpFlipVideoPort`, `NtGdiDvpGetVideoPortBandwidth`, `NtGdiDvpGetVideoPortConnectInfo`, `NtGdiDvpGetVideoPortField`, `NtGdiDvpGetVideoPortFlipStatus`, `NtGdiDvpGetVideoPortInputFormats`, `NtGdiDvpGetVideoPortLine`, `NtGdiDvpGetVideoPortOutputFormats`, `NtGdiDvpGetVideoSignalStatus`, `NtGdiDvpReleaseNotification`, `NtGdiDvpUpdateVideoPort`, `NtGdiDvpWaitForVideoPortSync`

### graphics-driver DDI callback; only valid when called from inside a display or printer driver (132)

`BRUSHOBJ_hGetColorTransform`, `BRUSHOBJ_pvAllocRbrush`, `BRUSHOBJ_pvGetRbrush`, `BRUSHOBJ_ulGetBrushColor`, `CLIPOBJ_bEnum`, `CLIPOBJ_cEnumStart`, `CLIPOBJ_ppoGetPath`, `EngAcquireSemaphore`, `EngAlphaBlend`, `EngAssociateSurface`, `EngBitBlt`, `EngCheckAbort`, `EngComputeGlyphSet`, `EngCopyBits`, `EngCreateBitmap`, `EngCreateClip`, `EngCreateDeviceBitmap`, `EngCreateDeviceSurface`, `EngCreatePalette`, `EngCreateSemaphore`, `EngDeleteClip`, `EngDeletePalette`, `EngDeletePath`, `EngDeleteSemaphore`, `EngDeleteSurface`, `EngEraseSurface`, `EngFillPath`, `EngFindResource`, `EngFreeModule`, `EngGetCurrentCodePage`, `EngGetDriverName`, `EngGetPrinterDataFileName`, `EngGradientFill`, `EngLineTo`, `EngLoadModule`, `EngLockSurface`, `EngMarkBandingSurface`, `EngMultiByteToUnicodeN`, `EngMultiByteToWideChar`, `EngPaint`, `EngPlgBlt`, `EngQueryEMFInfo`, `EngQueryLocalTime`, `EngReleaseSemaphore`, `EngStretchBlt`, `EngStretchBltROP`, `EngStrokeAndFillPath`, `EngStrokePath`, `EngTextOut`, `EngTransparentBlt`, `EngUnicodeToMultiByteN`, `EngUnlockSurface`, `EngWideCharToMultiByte`, `FONTOBJ_cGetAllGlyphHandles`, `FONTOBJ_cGetGlyphs`, `FONTOBJ_pQueryGlyphAttrs`, `FONTOBJ_pfdg`, `FONTOBJ_pifi`, `FONTOBJ_pvTrueTypeFontFile`, `FONTOBJ_pxoGetXform`, `FONTOBJ_vGetInfo`, `NtGdiBRUSHOBJ_DeleteRbrush`, `NtGdiBRUSHOBJ_hGetColorTransform`, `NtGdiBRUSHOBJ_pvAllocRbrush`, `NtGdiBRUSHOBJ_pvGetRbrush`, `NtGdiBRUSHOBJ_ulGetBrushColor`, `NtGdiCLIPOBJ_bEnum`, `NtGdiCLIPOBJ_cEnumStart`, `NtGdiCLIPOBJ_ppoGetPath`, `NtGdiEngAlphaBlend`, `NtGdiEngAssociateSurface`, `NtGdiEngBitBlt`, `NtGdiEngCheckAbort`, `NtGdiEngComputeGlyphSet`, `NtGdiEngCopyBits`, `NtGdiEngCreateClip`, `NtGdiEngDeleteClip`, `NtGdiEngDeletePath`, `NtGdiEngEraseSurface`, `NtGdiEngFillPath`, `NtGdiEngGradientFill`, `NtGdiEngLineTo`, `NtGdiEngLockSurface`, `NtGdiEngMarkBandingSurface`, `NtGdiEngPaint`, `NtGdiEngPlgBlt`, `NtGdiEngStretchBlt`, `NtGdiEngStretchBltROP`, `NtGdiEngStrokeAndFillPath`, `NtGdiEngStrokePath`, `NtGdiEngTextOut`, `NtGdiEngTransparentBlt`, `NtGdiEngUnlockSurface`, `NtGdiFONTOBJ_cGetAllGlyphHandles`, `NtGdiFONTOBJ_cGetGlyphs`, `NtGdiFONTOBJ_pQueryGlyphAttrs`, `NtGdiFONTOBJ_pfdg`, `NtGdiFONTOBJ_pifi`, `NtGdiFONTOBJ_pvTrueTypeFontFile`, `NtGdiFONTOBJ_pxoGetXform`, `NtGdiFONTOBJ_vGetInfo`, `NtGdiPATHOBJ_bEnum`, `NtGdiPATHOBJ_bEnumClipLines`, `NtGdiPATHOBJ_vEnumStart`, `NtGdiPATHOBJ_vEnumStartClipLines`, `NtGdiPATHOBJ_vGetBounds`, `NtGdiSTROBJ_bEnum`, `NtGdiSTROBJ_bEnumPositionsOnly`, `NtGdiSTROBJ_bGetAdvanceWidths`, `NtGdiSTROBJ_dwGetCodePage`, `NtGdiSTROBJ_vEnumStart`, `NtGdiXFORMOBJ_bApplyXform`, `NtGdiXFORMOBJ_iGetXform`, `NtGdiXLATEOBJ_cGetPalette`, `NtGdiXLATEOBJ_hGetColorTransform`, `NtGdiXLATEOBJ_iXlate`, `PATHOBJ_bEnum`, `PATHOBJ_bEnumClipLines`, `PATHOBJ_vEnumStart`, `PATHOBJ_vEnumStartClipLines`, `PATHOBJ_vGetBounds`, `STROBJ_bEnum`, `STROBJ_bEnumPositionsOnly`, `STROBJ_bGetAdvanceWidths`, `STROBJ_dwGetCodePage`, `STROBJ_vEnumStart`, `XFORMOBJ_bApplyXform`, `XFORMOBJ_iGetXform`, `XLATEOBJ_cGetPalette`, `XLATEOBJ_hGetColorTransform`, `XLATEOBJ_iXlate`, `XLATEOBJ_piVector`

### undocumented internal initialisation, handle-table or spool helper; no stable contract, exercised implicitly by every public call (97)

`Gdi32DllInitialize`, `GdiAddFontResourceW`, `GdiAddGlsBounds`, `GdiAddGlsRecord`, `GdiAddInitialFonts`, `GdiArtificialDecrementDriver`, `GdiComment`, `GdiConsoleTextOut`, `GdiConvertAndCheckDC`, `GdiConvertBitmap`, `GdiConvertBitmapV5`, `GdiConvertBrush`, `GdiConvertDC`, `GdiConvertEnhMetaFile`, `GdiConvertFont`, `GdiConvertMetaFilePict`, `GdiConvertPalette`, `GdiConvertRegion`, `GdiConvertToDevmodeW`, `GdiCreateLocalEnhMetaFile`, `GdiCreateLocalMetaFilePict`, `GdiCurrentProcessSplWow64`, `GdiDeleteLocalDC`, `GdiDeleteSpoolFileHandle`, `GdiDllInitialize`, `GdiDllInitializeWrapper`, `GdiDrawStream`, `GdiEndDocEMF`, `GdiEndPageEMF`, `GdiEntry1`, `GdiEntry10`, `GdiEntry11`, `GdiEntry12`, `GdiEntry13`, `GdiEntry14`, `GdiEntry15`, `GdiEntry16`, `GdiEntry2`, `GdiEntry3`, `GdiEntry4`, `GdiEntry5`, `GdiEntry6`, `GdiEntry7`, `GdiEntry8`, `GdiEntry9`, `GdiFixUpHandle`, `GdiFullscreenControl`, `GdiGetBatchLimit`, `GdiGetBitmapBitsSize`, `GdiGetDC`, `GdiGetDevmodeForPage`, `GdiGetEntry`, `GdiGetLocalBrush`, `GdiGetLocalDC`, `GdiGetLocalFont`, `GdiGetPageCount`, `GdiGetPageHandle`, `GdiGetSpoolFileHandle`, `GdiGetSpoolMessage`, `GdiGetVariationStoreDelta`, `GdiHandleBeingTracked`, `GdiInitSpool`, `GdiInitializeLanguagePack`, `GdiIsMetaFileDC`, `GdiIsMetaPrintDC`, `GdiIsPlayMetafileDC`, `GdiIsScreenDC`, `GdiIsTrackingEnabled`, `GdiIsUMPDSandboxingEnabled`, `GdiLoadType1Fonts`, `GdiPlayDCScript`, `GdiPlayEMF`, `GdiPlayJournal`, `GdiPlayPageEMF`, `GdiPlayPrivatePageEMF`, `GdiPlayScript`, `GdiProcessSetup`, `GdiQueryFonts`, `GdiQueryTable`, `GdiRealizationInfo`, `GdiReleaseDC`, `GdiReleaseLocalDC`, `GdiResetDCEMF`, `GdiSetAttrs`, `GdiSetBatchLimit`, `GdiSetLastError`, `GdiSetPixelFormat`, `GdiSetServerAttr`, `GdiStartDocEMF`, `GdiStartPageEMF`, `GdiSupportsFontChangeEvent`, `GdiTrackHCreate`, `GdiTrackHDelete`, `GdiValidateHandle`, `GditGetCallerTLStorage`, `GditPopCallerInfo`, `GditPushCallerInfo`

### internal helper behind a public wrapper; no stable contract, covered through the wrapper (71)

`AddFontResourceTracking`, `AddFontResourceWImpl`, `BeginPathImpl`, `CloseFigureImpl`, `CreateDCAImpl`, `CreateFontIndirectWImpl`, `CreateScalableFontResourceWImpl`, `DwmGetDirtyRgnImpl`, `EndPathImpl`, `ExtSelectClipRgnImpl`, `ExtTextOutAImpl`, `ExtTextOutWImpl`, `FillPathImpl`, `GdiBatchLimit`, `GdiCleanCacheDC`, `GdiDescribePixelFormat`, `GdiSwapBuffers`, `GetClipBoxImpl`, `GetFontDataImpl`, `GetHFONT`, `GetTextExtentExPointWPri`, `GetTextMetricsAImpl`, `IcmDeleteLocalDC`, `IcmReleaseCachedColorSpace`, `InternalDeleteDC`, `InternalDeleteObject`, `IntersectClipRectImpl`, `LineToImpl`, `MF16_DeleteObject`, `MF_DeleteObject`, `ModerncoreCreateICW`, `ModerncoreDeleteDC`, `ModerncoreGdiInit`, `MoveToExImpl`, `PolyBezierToImpl`, `PolyDrawImpl`, `PolylineToImpl`, `RectangleImpl`, `RemoveFontResourceTracking`, `RemoveFontResourceWImpl`, `RestoreDCImpl`, `SaveDCImpl`, `ScaleValues`, `SelectBrushLocal`, `SelectClipPathImpl`, `SelectClipRgnImpl`, `SelectFontLocal`, `SelectObjectImpl`, `SetBkModeImpl`, `SetICMModeImpl`, `SetMiterLimitImpl`, `SetPolyFillModeImpl`, `SetStretchBltModeImpl`, `SetTextAlignImpl`, `SetTextColorImpl`, `SetVirtualResolution`, `SetWorldTransformImpl`, `StretchDIBitsImpl`, `StrokePathImpl`, `bCreateDCW`, `bDeleteLDC`, `bInitSystemAndFontsDirectoriesW`, `bMakePathNameW`, `cGetTTFFromFOT`, `hGetPEBHandle`, `hdcCreateDCW`, `pldcGet`, `plinkGet`, `vDeleteLOCALFONT`, `vFreeUFIHashTable`, `vSetPldc`

### needs monitor or protected-output hardware, or changes display hardware state; no hardware in the oracle guest (56)

`ConfigureOPMProtectedOutput`, `CreateOPMProtectedOutput`, `CreateOPMProtectedOutputs`, `DDCCIGetCapabilitiesString`, `DDCCIGetCapabilitiesStringLength`, `DDCCIGetTimingReport`, `DDCCIGetVCPFeature`, `DDCCISaveCurrentSettings`, `DDCCISetVCPFeature`, `DestroyOPMProtectedOutput`, `DestroyPhysicalMonitorInternal`, `GetCOPPCompatibleOPMInformation`, `GetCertificate`, `GetCertificateByHandle`, `GetCertificateSize`, `GetCertificateSizeByHandle`, `GetDeviceGammaRamp`, `GetNumberOfPhysicalMonitors`, `GetOPMInformation`, `GetOPMRandomNumber`, `GetPhysicalMonitorDescription`, `GetPhysicalMonitors`, `GetSuggestedOPMProtectedOutputArraySize`, `NtGdiConfigureOPMProtectedOutput`, `NtGdiCreateOPMProtectedOutput`, `NtGdiCreateOPMProtectedOutputs`, `NtGdiDDCCIGetCapabilitiesString`, `NtGdiDDCCIGetCapabilitiesStringLength`, `NtGdiDDCCIGetTimingReport`, `NtGdiDDCCIGetVCPFeature`, `NtGdiDDCCISaveCurrentSettings`, `NtGdiDDCCISetVCPFeature`, `NtGdiDestroyOPMProtectedOutput`, `NtGdiDestroyPhysicalMonitor`, `NtGdiGetAppliedDeviceGammaRamp`, `NtGdiGetCOPPCompatibleOPMInformation`, `NtGdiGetCertificate`, `NtGdiGetCertificateByHandle`, `NtGdiGetCertificateSize`, `NtGdiGetCertificateSizeByHandle`, `NtGdiGetDeviceGammaRamp`, `NtGdiGetGammaRampCapability`, `NtGdiGetMonitorID`, `NtGdiGetNumberOfPhysicalMonitors`, `NtGdiGetOPMInformation`, `NtGdiGetOPMRandomNumber`, `NtGdiGetPhysicalMonitorDescription`, `NtGdiGetPhysicalMonitors`, `NtGdiGetSuggestedOPMProtectedOutputArraySize`, `NtGdiSetDeviceGammaRamp`, `NtGdiSetMagicColors`, `NtGdiSetOPMSigningKeyAndSequenceNumbers`, `NtGdiSetPrivateDeviceGammaRamp`, `SetDeviceGammaRamp`, `SetMagicColors`, `SetOPMSigningKeyAndSequenceNumbers`

### printer-only or spooler path; the oracle guest has no printer device (46)

`AbortDoc`, `AbortDocImpl`, `DeviceCapabilitiesExA`, `DeviceCapabilitiesExW`, `DocumentEventEx`, `DrawEscape`, `EndDoc`, `EndDocImpl`, `EndFormPage`, `EndPage`, `EndPageImpl`, `Escape`, `EscapeImpl`, `ExtEscape`, `ExtEscapeImpl`, `GdiPrinterThunk`, `NamedEscape`, `NtGdiAbortDoc`, `NtGdiDoBanding`, `NtGdiDrawEscape`, `NtGdiEndDoc`, `NtGdiEndPage`, `NtGdiExtEscape`, `NtGdiGetPerBandInfo`, `NtGdiGetSpoolMessage`, `NtGdiInitSpool`, `NtGdiResetDC`, `NtGdiSetPUMPDOBJ`, `NtGdiSetUMPDSandboxState`, `NtGdiStartDoc`, `NtGdiStartPage`, `NtGdiUMPDEngFreeUserMem`, `NtGdiUnloadPrinterDriver`, `ResetDCA`, `ResetDCW`, `ResetDCWImpl`, `SetAbortProc`, `StartDocA`, `StartDocW`, `StartDocWImpl`, `StartFormPage`, `StartPage`, `StartPageImpl`, `UnloadUserModePrinterDriver`, `fpClosePrinter`, `gdiPlaySpoolStream`

### forwarded to the Uniscribe module; Uniscribe is outside the scope of this corpus, candidate for a separate shaping corpus (44)

`ScriptApplyDigitSubstitution`, `ScriptApplyLogicalWidth`, `ScriptBreak`, `ScriptCPtoX`, `ScriptCacheGetHeight`, `ScriptFreeCache`, `ScriptGetCMap`, `ScriptGetFontAlternateGlyphs`, `ScriptGetFontFeatureTags`, `ScriptGetFontLanguageTags`, `ScriptGetFontProperties`, `ScriptGetFontScriptTags`, `ScriptGetGlyphABCWidth`, `ScriptGetLogicalWidths`, `ScriptGetProperties`, `ScriptIsComplex`, `ScriptItemize`, `ScriptItemizeOpenType`, `ScriptJustify`, `ScriptLayout`, `ScriptPlace`, `ScriptPlaceOpenType`, `ScriptPositionSingleGlyph`, `ScriptRecordDigitSubstitution`, `ScriptShape`, `ScriptShapeOpenType`, `ScriptStringAnalyse`, `ScriptStringCPtoX`, `ScriptStringFree`, `ScriptStringGetLogicalWidths`, `ScriptStringGetOrder`, `ScriptStringOut`, `ScriptStringValidate`, `ScriptStringXtoCP`, `ScriptString_pLogAttr`, `ScriptString_pSize`, `ScriptString_pcOutChars`, `ScriptSubstituteSingleGlyph`, `ScriptTextOut`, `ScriptXtoCP`, `UspAllocCache`, `UspAllocTemp`, `UspFreeMem`, `ftsWordBreak`

### language-pack callback invoked only by user32 text routines; no meaningful standalone call (13)

`LpkDrawTextEx`, `LpkEditControl`, `LpkExtTextOut`, `LpkGetCharacterPlacement`, `LpkGetEditControl`, `LpkGetTextExtentExPoint`, `LpkInitialize`, `LpkPSMTextOut`, `LpkPresent`, `LpkTabbedTextOut`, `LpkUseGDIWidthCache`, `LpkpEditControlSize`, `LpkpInitializeEditControl`

### exported data symbol, not a function (9)

`DxTrimNotificationListHead`, `gMaxGdiHandleCount`, `gW32PID`, `g_systemCallFilterId`, `ghICM`, `pGdiDevCaps`, `pGdiSharedHandleTable`, `pGdiSharedMemory`, `semDxTrimNotification`

### needs a DWM or Direct3D surface or session-mapped memory; not callable as plain GDI (9)

`BeginGdiRendering`, `CreateBitmapFromDxSurface`, `CreateBitmapFromDxSurface2`, `CreateDPIScaledDIBSection`, `CreateScaledCompatibleBitmap`, `CreateSessionMappedDIBSection`, `DwmCreatedBitmapRemotingOutput`, `EndGdiRendering`, `GetRandomRgn`

### end-user-defined-character state is system-wide registry and font state; destructive, belongs in an opt-in dx_ area (8)

`EnableEUDC`, `EudcLoadLinkW`, `EudcUnloadLinkW`, `GetEUDCTimeStamp`, `GetEUDCTimeStampExW`, `NtGdiEnableEudc`, `NtGdiEudcLoadUnloadLink`, `NtGdiGetEudcTimeStampEx`

### halftone helper of the driver DDI; only meaningful inside a driver (4)

`HT_Get8BPPFormatPalette`, `HT_Get8BPPMaskPalette`, `NtGdiHT_Get8BPPFormatPalette`, `NtGdiHT_Get8BPPMaskPalette`

### session or network font state not reachable without privileged setup (4)

`GetProcessSessionFonts`, `NtGdiAddEmbFontToDC`, `NtGdiGetEmbedFonts`, `UnloadNetworkFonts`

## Uncovered, no reason recorded (1)

`GetGlyphOutline`

## Consistency of the hand-maintained files

- Entries of `tools/uncovered_justified.txt` for names that a test now calls (stale, remove them): none.
- Entries of `tools/uncovered_justified.txt` that are not in the export lists: none.
- Entries of `tools/coverage_observe_only.txt` that no test calls: none.
