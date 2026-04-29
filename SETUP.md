# Public repo — initial setup notes

> One-time setup procedure for `gluon/Void-LinkAudio` public repo.
> Once done, subsequent updates use `tools/sync-from-rd.sh`.

## 1. Create the empty public repo on GitHub

GitHub UI → New repository:
- Owner: `gluon`
- Name: `Void-LinkAudio`  (note the hyphen — `VoidLinkAudio` is taken by the private R&D repo)
- Description: `Multi-host integration of Ableton Link Audio — TouchDesigner, Max, VCV Rack`
- **Public** (or Private to test the workflow first; flip later)
- ☐ Add README · ☐ Add .gitignore · ☐ Add LICENSE — all unchecked, we have ours
- Create

## 2. Clone it locally — alongside the R&D

```bash
cd ~/DATA/WORK/RESEARCH_DEV/20260425_LinkAudio/
git clone https://github.com/gluon/Void-LinkAudio.git VoidLinkAudio-public
cd VoidLinkAudio-public
```

> Local folder is named `VoidLinkAudio-public` for clarity. The remote name
> on GitHub is `Void-LinkAudio` (hyphenated, because `VoidLinkAudio` is the
> private R&D's GitHub name). The product name everywhere in code, docs,
> and branding stays `VoidLinkAudio` (no hyphen).

## 3. Drop the bootstrap files

Extract the `voidlinkaudio-public-setup.zip` into this directory.
You should now have:

```
VoidLinkAudio-public/
├── .gitignore
├── LICENSE
├── README.md
└── tools/
    └── sync-from-rd.sh
```

Make the script executable:

```bash
chmod +x tools/sync-from-rd.sh
```

## 4. Add the Ableton/link submodule

The R&D repo and the public repo each declare their own submodule pointing
at the same upstream `Ableton/link`. **Do not sync `thirdparty/` from R&D.**
The `sync-from-rd.sh` script explicitly skips both `thirdparty/` and
`.gitmodules` for this reason.

```bash
git submodule add https://github.com/Ableton/link.git thirdparty/link
git submodule update --init --recursive
```

This creates:
- `.gitmodules` (declaring the submodule)
- `thirdparty/link/` (with the actual Link source)
- `thirdparty/link/modules/asio-standalone/` (Link's own transitive submodule)

To **bump** the Link version later:

```bash
cd thirdparty/link
git fetch
git checkout <new-tag-or-sha>
cd ../..
git add thirdparty/link
git commit -m "Bump Ableton/link to <ref>"
```

## 5. First sync from R&D

```bash
./tools/sync-from-rd.sh
```

The script runs in 3 phases:

1. **Dry-run preview** — shows what would change, without touching anything
2. **Confirmation prompt** — review and decide whether to proceed
3. **Actual sync + leak check + binary check**

If any leak pattern matches (e.g. `sv_ui`, `/Users/julien`, `8UX77EF9ZR`),
investigate before committing. The script flags but does not abort — the
final decision is yours.

## 6. Verify the result

```bash
git status
ls -la
# .gitignore  .gitmodules  LICENSE  README.md  ACKNOWLEDGEMENTS.md  core/  td/  max/  vcv/  thirdparty/  tools/

# Optional fresh-clone build smoke test:
cd /tmp
rm -rf VoidLinkAudio-test
git clone ~/DATA/WORK/RESEARCH_DEV/20260425_LinkAudio/VoidLinkAudio-public VoidLinkAudio-test
cd VoidLinkAudio-test
git submodule update --init --recursive
cd max
cmake -B build -G Xcode
cmake --build build --config Release
ls externals/   # → void.linkaudio.in~.mxo, void.linkaudio.out~.mxo
```

## 7. First commit & push

```bash
cd ~/DATA/WORK/RESEARCH_DEV/20260425_LinkAudio/VoidLinkAudio-public
git add .
git status   # last review

git commit -m "Initial public research release

Multi-host integration of Ableton Link Audio.
Sources for:
- TouchDesigner CHOPs (LinkAudioInCHOP, LinkAudioOutCHOP)
- Max externals + package (void.linkaudio.in~, void.linkaudio.out~)
- Shared C++ core (LinkAudioManager, AudioRingBuffer)
- VCV Rack module (work in progress)

openFrameworks support: https://github.com/gluon/ofxAbletonLinkAudio
VST/AU/CLAP plugins:    https://structure-void.com

Built on Ableton's open-source Link library (MIT).
Early research release — APIs may change, Windows builds in progress."

git push -u origin main
```

## 8. Routine updates (after initial setup)

```bash
cd ~/DATA/WORK/RESEARCH_DEV/20260425_LinkAudio/VoidLinkAudio-public
./tools/sync-from-rd.sh
git status
git diff --stat
git add .
git commit -m "Update: <human-readable summary>"
git push
```

The leak check and binary check run automatically at every sync.

---

## Why these decisions

- **`of/` not synced**: openFrameworks support has its own canonical repo
  (`gluon/ofxAbletonLinkAudio`), following the ofxAddon convention. Syncing
  it here too would create two divergent public copies.
- **`thirdparty/` not synced**: rsync of a git submodule produces a broken
  result (gitlink referencing R&D's `.git`). The public repo declares its
  own submodule explicitly. Bumping happens via `git submodule update`,
  not via the sync script.
- **`.gitmodules` not synced**: the public `.gitmodules` lists only the
  public submodule (`thirdparty/link`); R&D may have additional internal
  submodules that have no business being mentioned here.
- **`README.md` not synced**: the public README is written from scratch for
  external readers; the R&D README contains internal notes (RDV mentions,
  internal strategy, etc.).
- **VST physically absent from R&D**: the VST/AU/CLAP plugins live in a
  separate private repo (`SV_VST`), so they cannot leak through this sync
  even by accident.
