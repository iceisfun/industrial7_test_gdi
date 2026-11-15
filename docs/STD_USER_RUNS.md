# Standard-user (non-elevated) runs on the oracle guest

Status: feasible with a restricted token; one recording exists (`data/win10/ui_metrics/stduser.rec.gz`). Every statement
below is FACT from guest runs unless marked *inference*. Two boots were used for the final configuration; the
configuration search used three boots with several configurations per boot.

## 1. Starting point

The guest logs on the built-in `Administrator` account automatically. The session token is a full, elevated administrator
token: `TokenIsElevated` 1, integrity High (S-1-16-12288), `BUILTIN\Administrators` enabled, all administrator privileges
present. Probe ENV row `host.elevated` is 1. `net user` lists only `Administrator`, `DefaultAccount`, `Guest` and
`WDAGUtilityAccount`, so there is no ordinary second account to log on as (and creating one would modify the guest, which these
runs avoid). `net session` succeeds in the guest session (it needs an elevated token).

## 2. What works: a restricted copy of the session token

`vmrunner` has a verb `runstd[:FLAGS] <timeout_s> <name> <exe> [args]` (plan syntax in `tools/vmrunner/vmrunner.c`). It behaves
like `run` (same inherited stdout, stderr relay, timeout, job object) but starts the child with
`CreateProcessAsUser` and a token made from the runner's own token:

1. `CreateRestrictedToken(token, LUA_TOKEN | DISABLE_MAX_PRIVILEGE, ...)` (flags `L`, `P`).
2. `SetTokenInformation(TokenOwner)` = the user SID and `SetTokenInformation(TokenDefaultDacl)` = allow GENERIC_ALL to the
   user SID and SYSTEM (flag `S`).
3. `SetTokenInformation(TokenIntegrityLevel)` = Medium, S-1-16-8192 (flag `M`).

The default flags are `LPMS`. No extra privilege is needed (a restricted version of the caller's own primary token may be used
for `CreateProcessAsUser`), and the child runs in the same session and on the same desktop, so probes that create windows,
message boxes and menus work.

Resulting token of the child (`whoami /groups /priv`, and the runner's own `I7P TOKEN` line):

| property | elevated session | restricted child |
|---|---|---|
| `TokenIsElevated` / ENV `host.elevated` | 1 | 0 |
| `TokenElevationType` | 1 (default) | 1 (default; a UAC-filtered token would report 3) |
| integrity level | High | Medium |
| `BUILTIN\Administrators` | enabled, group owner | `Group used for deny only` |
| `NT AUTHORITY\Local account and member of Administrators group` | enabled | `Group used for deny only` |
| other groups (Everyone, Users, INTERACTIVE, Authenticated Users, ...) | enabled | enabled |
| privileges | the administrator set | only `SeChangeNotifyPrivilege` |
| `net session` | succeeds | `System error 5 ... Access is denied.` (rc 2) |

So the process is not elevated, is not a member of Administrators for access checks (deny-only), and has the privilege set of
a standard user. `IsUserAnAdmin()` was not called directly; *inference*: it returns FALSE because the Administrators SID is
deny-only (CheckTokenMembership only counts enabled groups).

## 3. What did not work (and why the extra flag exists)

Configurations tried (three boots: one with the first default flags, without `S`, then 7 and 6 configurations) (each started `whoami.exe` and `hello.exe`; exit code `0xC0000142`
(STATUS_DLL_INIT_FAILED) means the process died during loader initialisation, after about 15 ms):

| flags | result |
|---|---|
| `P` (DISABLE_MAX_PRIVILEGE only) | runs, but still elevated (`TokenIsElevated` 1) |
| `L`, `LP`, `LPM`, `LM`, `D`, `DP`, `DPM` (any token whose Administrators group is deny-only) | process starts and dies with `0xC0000142` |
| `LPMS`, `LPMSK`, `LSK`, `LPSK` (plus owner = user SID and a default DACL that names the user) | runs, `TokenIsElevated` 0 (`LPMS` and `LPMSK` Medium, `LSK` and `LPSK` High) |
| `DPSK` (explicit deny-only Administrators, no `L`, with `S`) | runs, but `TokenIsElevated` stays 1 |
| `LPMK` (`K` only: token object DACL via `SetKernelObjectSecurity`) | `SetKernelObjectSecurity` fails with error 5 (access denied); child still dies |

(The experimental flag `K` is not in the runner any more; the table lists what it showed.)

*Inference* for the failure: the restricted token keeps `Administrators` as its owner and as the principal of its default DACL.
With that group deny-only, the new process cannot open its own token during loader start-up. Making the user SID the owner
and default-DACL principal removes the failure. `LUA_TOKEN` is what clears `TokenIsElevated`; `D` alone does not.

## 4. The recording

`tools/vm_run.sh ui_metrics --dpi 96 --args "--full" --plan-extra F` with `F` containing

    runstd 300 ui_metrics ui_metrics.exe --tag stduser --full

writes `data/win10/ui_metrics/stduser.rec` next to the ordinary elevated `dpi96.rec` of the same boot. The recording is valid
(13036 lines, `host.elevated 0`, `host.user Administrator`, same row counts as the elevated one; all message boxes and the
popup menu were measured).

Determinism over two fresh boots (`python3 tools/rec.py diff B A`): identical except the volatile `GetSystemMetrics` index 93
(`i93`, 14 rows), exactly as for the elevated recording.

Difference to the elevated recording of the same boot (both boots, 13 rows, one cell family): `SystemParametersInfoForDpi`
(`SPI_GETNONCLIENTMETRICS`, 96 dpi) in `ctx=proc` (the first call of the process) returns `lfCaptionFont.lfHeight` 0 for the
restricted child, versus -12 for the elevated process (in the five other thread contexts the elevated process also gets 0, so
the elevated `ctx=proc` value is the odd one); the corresponding `ftm`/`ftm_cw` rows (`src=ncm96.caption`) follow from the font
that results (restricted: cell height 21, the lfHeight-0 default font; elevated: 15, etc.). All other `ncm`/`ncm96`/`ncm120`... sources, the `SystemParametersInfo` (non-ForDpi) result and every other table
are identical to the elevated run. The cause was not investigated (*inference*: a property of this API for this token, not
of the DPI). Treat it as a NOTE-level observation; it is stable over two boots.

## 5. Limits

- It is the same user: the SID, user name (`host.user Administrator`), profile directory, HKCU hive and file ACL ownership are
  those of the administrator account. A real standard-user logon (a separate account in `Users` only) was not available
  without changing the guest. Things that depend on the account itself (profile paths, per-user font installation under
  another profile, `CSIDL` locations, group membership beyond Administrators) are not covered.
- The token is a "filtered administrator" shape (deny-only groups, Medium integrity) rather than a token created by a
  logon. `TokenElevationType` is 1, not 3.
- Only the process started by `runstd` is restricted; the runner, the logoff/auto-logon stages and `cmd` steps stay elevated. Restricted
  runs inside a DPI sweep were not tested: `--plan-extra` appends to the last stage only, so per-stage restricted runs need a
  `vm_run.sh` extension.
- Do not use `runstd` for steps that need to change system state (display mode, HKLM, services): they fail by design.
- `S` is required for any token with a deny-only Administrators group; `K` is not usable (the DACL of the token object cannot be changed through the handle that `CreateRestrictedToken` returns) and not needed.

## 6. Using it for tests

`tools/vm_test.sh` does not use this yet (it runs the test exe with `run`). A test that must differ between an elevated and a
non-elevated process should read `TokenElevation`, assert both cases, and skip the one it cannot reach with a concrete reason
(for example "needs a standard-user logon account"). Behaviour that was identical in both token states for `ui_metrics`
(everything except the ncm96 caption row above) is evidence that the metrics families do not depend on elevation.
