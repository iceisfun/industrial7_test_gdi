# data/win10/kerning_pairs

`GetKerningPairsW` / `GetKerningPairsA` recorder (probe `kerning_pairs`, default full grid).
File: `dpi96.rec` (646 lines: R=318, H=156). Probe time 3 s. Boots: 2 fresh boots, identical.
Determinism: IDENTICAL (`python3 tools/rec.py diff A B` -> `diffs=0`; the second recording is not stored).
Known limits: DPI 96 only, regular and bold only (no italic), two heights.

## Rows

Faces: Segoe UI, MS Shell Dlg, MS Shell Dlg 2, Tahoma, Arial, Courier New, Microsoft Sans Serif, Times New Roman, Verdana, Consolas,
Calibri, Lucida Console, Segoe UI Symbol, Georgia, Trebuchet MS, Cambria (TrueType), System, Terminal, Fixedsys (bitmap faces), and the absent
name `NoSuchFace I7` (records what the mapper substitutes). Heights -12 (main) and -32, weights 400 and 700. Key: `face`, `req_h`
(lfHeight as passed), `wt`, `it` (always 0), `api` (`W` = GetKerningPairsW, `A` = GetKerningPairsA).

| table | content |
|---|---|
| `kern` | `got` (realised face), `count` (size query: `lpkrnpair=NULL`, `nNumPairs=0`), `ret` (full read of `count` pairs), `sorted` (1 if the returned order is ascending by (first, second)), min / max / sum of `iKernAmount`, `nonzero` (pairs with a non-zero amount), `r0..r3` first four pairs and `r4..r7` last four pairs in the order returned (`first/second/amount`, code units in hex, amount signed decimal) |
| `kernsorted` | the same eight samples after sorting the pairs by (first, second, amount); only for faces that have pairs |
| `kerncall` | contract (W): `count`, return for a buffer one pair too small (`ret_small`, the buffer itself is large enough), return for `lpkrnpair=NULL` with `nNumPairs` = count (`ret_nullbuf`) |
| `H h_ret`, `H h_sorted` | first 64 bits of the SHA-256 and the byte length of all pairs, in the order returned and sorted, each pair packed as three little-endian int32 (first, second, amount) |

`iKernAmount` is in logical units of the realised font, so it differs between heights; the pair set does not.

## Observations (FACT from this recording)

- Pair counts at -12/400 (W): Segoe UI 8292, Calibri 26706, Cambria 29715, Segoe UI Symbol 1361, Arial 909, Times New Roman 867,
  Tahoma 674 (also MS Shell Dlg 2), Verdana 574, Trebuchet MS 118. Zero pairs: MS Shell Dlg / Microsoft Sans Serif, Courier New, Consolas,
  Lucida Console, Georgia, System, Terminal, Fixedsys. For a bold request (700), Tahoma and MS Shell Dlg 2 return 0 pairs, while Arial (908), Segoe UI (8319)
  and Times New Roman (872) return pairs.
- The `A` count differs from the `W` count (Arial: 909 W, 120 A at -12/400): the A entry point returns far fewer pairs
  (inference: pairs are converted to ANSI code page bytes; the A samples show single-byte values, and the sorted A samples contain
  repeated identical pairs, e.g. Segoe UI -12/400 `22/72/0` four times, so several Unicode pairs can map to the same byte pair).
- The size query, the full read and the NULL-buffer-with-count call agree; a buffer that is one pair too small returns `count - 1`
  (it copies what fits and does not return 0) when the pairs exist.

## Undefined / excluded cells

- Faces with no pairs have `!` in all columns after `ret`, and no `kernsorted` row and no H rows with data.
- `got` for a face that cannot be created would be `!`; none occurred.
- The order of pairs in the buffer is as returned by the system (not asserted sorted; see `sorted`).

## Environment

Windows 10 Home 22H2, 10.0.19045 UBR 2965, user Administrator (elevated), session 1, en-US, 1024x768x32, one monitor, std VGA, qemu/KVM
guest. Full list in the `ENV` rows. Produced by `tools/vm_run.sh kerning_pairs` (docs/VM_RUN.md); provenance in
`data/win10/_runs/dpi96.kerning_pairs.log`.
