# R5F — first actual picture on the owner's Xbox (NOT gameplay)

## Scope and what is verified

R5E has passed on the owner's Series S: MEM2 zero-fill, six decoded/staged
sections, the R5B CPU preflight and the R5D 532,197-entry RELA metadata
audit. The game entrypoint was NOT executed.

R5F is the next **visual** milestone, not a claim that the game runs.
It adds a stand-alone D3D12 textured fullscreen preview to the *same*
private native boot-shell AppX and keeps every R5E preflight check.

- The preview reads **only** \`LocalState/nsmbu_preview.png\`.
- The user supplies a **16:9 PNG**, e.g. a screenshot captured from their
  own legally dumped and locally running copy of the game. No media goes
  into GitHub, source control, or an AppX.
- It uses Windows Imaging Component (WIC) to decode PNG as RGBA8,
  allowing width/height 16–2048 and bounded GPU allocation.
- It uses the existing Xbox D3D12 texture upload helper and a static
  public full-screen shader. It does **not** call any game function,
  gx2 renderer, Cafe OS host function, or unsupported syscall.
- If the PNG is missing/invalid, it displays a synthetic blue checker
  screen and explicitly says so in the log. **This is not a game image.**
- Log: \`LocalState/nsmbu_r5f_visual_preview.txt\`.
- Private signed package version: **0.7.0.0**.

### Build (after the public branch passes review/build)

The owner-private workflow \`nsmbu-usa-v0-full-link.yml\` checks out a
PINNED public commit of this toolchain, compiles the existing owner-derived
game archive and builds the boot-shell only with opt-in
\`build_r4_appx=true\`. To test R5F, **pin an R5F source commit there** before
starting the workflow. Never change to an unpinned branch in the private
build. The owner input archive SHA256 and release tag are unchanged.

The \`try_private_game_link.ps1\` helper now compiles static shader headers
through the Windows SDK \`fxc\` tool when the private boot shell is requested.

### On the Xbox

1. Install the new 0.7.0.0 signed package after its build is validated.
2. Keep your existing \`red-pro2.rpx\` at \`LocalState/red-pro2.rpx\`.
3. Copy your own 1280×720 PNG screenshot to
   \`LocalState/nsmbu_preview.png\` through the same Device Portal file
   transfer used for the RPX.
4. Open the app. The full-screen image should remain visible once the
   R5E audit finishes. Inspect the R5F log for \`R5F PRESENT PASS\`.
5. The useful success condition is **an actual PNG image on the TV**
   together with \`R5E MEM2 ... PASS\` and \`6/6 sections\`, not a menu,
   60 FPS, or an interactable character.

### Rollback

If the GPU path fails, the earlier R5E private pin
\`349a608c75d503dee40313c5cde4dd6970099d79\` remains available,
and the R5E RPX preflight is unchanged.

### What R5F deliberately does NOT solve

No game logic, GX2 translation, Cafe OS imports, scheduler, filesystem,
audio, or native game frame execution. The un-staged import tables and
\`r13\` signed-window coverage from R5E remain separate execution blockers.
The first engine-rendered Mario frame remains a later milestone.
