# R2.3 — Xbox Series S public runtime validation

CI evidence: https://github.com/romadrugov/nsmbu-xbox-uwp/actions/runs/42839372128

This independent signed 0.8.3.0 UWP package includes only publicly authored
three-instruction PowerPC code and unchanged upstream Cafe OS HLE. No
Nintendo game assets, NSMBU RPX or private recompiled game code is included.
The package identity is separate from the stable Xbox 0.7.0 smoke.

## Install

1. Open the successful R2.3 GitHub Actions run above. Download and extract
   the artifact named nsmbu-xbox-r23-public-runtime-probe.
2. Its three files are:
   - NSMBUR23XboxProbe_0.8.3.0_x64.appx — signed device test.
   - NSMBUR23XboxProbe.cer — the public self-signed certificate.
   - Microsoft.VCLibs.x64.14.00.appx — UWP C++ framework dependency.
3. On Series S in Developer Mode, open Xbox Device Portal in your browser
   at https://<xbox-ip>:11443. Install the new AppX via Apps. Add the
   certificate/dependency if prompted. Do not remove stable 0.7.0.
4. Launch NSMBU R2.3 Public Runtime Probe from the Xbox app list.

## Expected screen

* Blue, briefly: WinRT/D3D12 initial frame (may be too quick to notice).
* Solid green: all R2.3 public-runtime tests returned PASS and the final
  D3D12 presentation worked.
* Solid red: at least one runtime test failed; inspect the diagnostic log.
* Black, splash only, or sudden exit: NOT A PASS. Inspect the app log,
  package dependencies and Xbox Device Portal crash diagnostics.

## Get the exact results

Xbox Device Portal → File Explorer → LocalAppData →
Romadrugov.NSMBUR23XboxProbe (possibly followed by a package suffix)
→ LocalState → nsmbu_r23_diag.txt.

The log contains [r23/0] initial GPU frame, [r23/boot] fixed 4-GiB
VIRTUAL reservation and one 64-KiB physical commit, [r23/1] generated
PowerPC function returning 42, [r23/2] actual production HLE dispatcher,
[r23/3] sparse-memory readable vs reserved-only checks, [r23/4] real
Cafe OS heap create/allocate/free/destroy, [r23/5] UWP dirty-page
tracking, followed by [r23] FINAL PASS or FINAL FAIL.

If red, send the last successful [r23] line and the failure code. If
no log exists, startup failed before logging — check Device Portal.

## Technical limits

* CI established x64 GUI AppContainer linking, signed AppX creation,
  WinRT activation metadata packaging, archive integrity, and the
  separate Windows host write-watch behavior test.
* The actual Xbox device execution of this R2.3 AppX is NOT VERIFIED
  until someone runs it and supplies its screenshot/log.
* Memory reserves 4 GiB of virtual address space but commits only a
  64-KiB slice. It does not call the desktop core.cpp mem::init().
* When MEM_WRITE_WATCH is unavailable, dirty queries remain conservative
  (potentially always dirty), never reporting a false clean. A green
  screen is not proof that hardware write-watch support was enabled;
  inspect [r23/3] to know which mode the console used.
* A green screen proves only selected synthetic PowerPC + real Cafe OS
  functions, NOT an actual Nintendo game function or playable graphics.
* Signing certificates are new per CI run. If upgrading THIS diagnostic
  fails due to signing identity conflict, uninstall the previous R2.3
  probe only, not stable 0.7.0.
