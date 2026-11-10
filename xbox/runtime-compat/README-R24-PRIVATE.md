# Xbox R2.4: select one real USA_V0 function (private local only)

R2.3 is confirmed by a physical Xbox Series S log: public generated PPC
returned 42 through production dispatch, Cafe OS HLE and sparse guest memory
passed. R2.4's goal is to prepare the **first genuine game-origin PPC
function** for that existing UWP dispatch path.

**No game-derived source, RPX, COFF objects, private archives or logs may
be committed to this public repository or uploaded as public GitHub artifacts.**

## 1. Find a strictly independent function on your Mac

The prior private workspace already holds the exact USA_V0 recompilation:
93 generated code_NNN.c chunks in ~/NSMBU-Xbox-private/USA_V0/generated.

From a checkout on the xbox-r24-private-leaf-probe branch:

    python3 tools/xbox/r24_leaf_select.py scan \
      --generated "$HOME/NSMBU-Xbox-private/USA_V0/generated" \
      --out "$HOME/NSMBU-Xbox-private/USA_V0/r24"

The script reads those files only; never copies function bodies into the repo.
It prints a shortlist of guest addresses, PPC instruction counts and C
chunk names. Its candidates.json, outside checkout, contains only that
metadata and no generated C body. Share only this metadata if necessary.

Selection is intentionally strict: a short 2–24-instruction straight-line
function ending in unconditional blr, writing r3, with no memory loads/stores,
branches, imports, other-function calls, hooks or unimplemented instructions.
It explicitly removes a **provably unreachable** generated fallthrough
after the final unconditional blr from its isolated test source.

If the shortlist is empty, stop and revise the conservative selector;
do NOT pick an unchecked function.

## 2. Create ONE private C object candidate and local CPU witness

Choose a returned address (example below is PLACEHOLDER, not verified):

    python3 tools/xbox/r24_leaf_select.py extract \
      --generated "$HOME/NSMBU-Xbox-private/USA_V0/generated" \
      --out "$HOME/NSMBU-Xbox-private/USA_V0/r24" \
      --address 0x02001234

    python3 tools/xbox/r24_leaf_select.py reference \
      --out "$HOME/NSMBU-Xbox-private/USA_V0/r24" \
      --compiler clang

These steps create, INSIDE THE PRIVATE FOLDER ONLY:

* r24_selected.c — the actual selected game instruction body (keep private)
* selected.json — source chunk and selected guest address metadata
* r24_reference.c, r24_reference_host — local native CPU reference
* r24_private_binding.c — C ABI pointer, guest address and expected registers
* r24_reference_result.json — CPU reference r3, r4 and lr

The local reference initializes r3=0x13579BDF, r4=0x2468ACE0,
r5=0x10203040, r6=7, r7=3 and calls this one extracted function.
Its expected r3/r4/lr values must be compared to the Xbox result.

**Reference execution on the Mac is NOT a physical Xbox test.**

## 3. How the one-function integration will work

The public common module xbox/runtime-compat/r24_private_dispatch.cpp
expects g_r24_private_function, g_r24_guest_address and expected registers
from the PRIVATE binding. After R2.3 calls dispatch::init(), it will call
production dispatch::set(guest_addr, fn), then ppc_dispatch(&cpu), and
compare the resulting CPU registers with the local reference.

The public CI verifies this mechanism with a separately authored synthetic
function. Its address/value are not genuine NSMBU code.

The PRIVATE C object and binding need to be compiled for Windows UWP x64
with clang-cl/MSVC ABI and linked into an isolated signed AppX **locally**.
Mac/Linux compilation or the older 2.1-GB x86_64-windows-gnu archive alone
is NOT proof that the private objects link under MSVC UWP. Do not upload
private code to GitHub CI for this.

The Xbox runtime test is not complete until an offline private UWP package
has been linked and the physical Series S reports a matching register
witness. Never replace stable Xbox 0.7.0 or the verified R2.3 AppX.

## 4. Verification tiers

* Public GitHub: strict selector unit tests; real public-fake RPX
  recompilation + extraction + local C execution; actual runtime
  dispatch::set with synthetic entry and known CPU signature.
* Private Mac: scan exact USA_V0 generated output and run isolated native
  CPU reference without dependencies. No game files move to GitHub.
* Private Windows UWP build: clang-cl compile one genuine selected function
  and bind it into the R2.3-derived AppContainer target — NOT YET VERIFIED.
* Physical Xbox Developer Mode: start the separately signed R2.4 diagnostic,
  inspect LocalState result log; real game function should return the same
  r3/r4/lr as the private local reference — NOT YET VERIFIED.

Once a real candidate exists, the next necessary engineering task is a
local-only UWP packaging flow for the two private object files.
