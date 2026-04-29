#!/usr/bin/env bash
# tools/sync-from-rd.sh
# Sync the VoidLinkAudio R&D private repo into this public repo.
#
# Usage:
#   ./tools/sync-from-rd.sh                    # interactive: dry-run preview, then asks
#   ./tools/sync-from-rd.sh --yes              # non-interactive (CI / scripts)
#   ./tools/sync-from-rd.sh --dry-run          # only show what would happen, never write
#
# To override the R&D repo path:
#   RD_REPO=/some/other/path ./tools/sync-from-rd.sh
#
# This OVERWRITES whitelisted directories with what's in the R&D repo.
# Uses --delete: files removed from R&D are also removed here.
# Always review with `git status` and `git diff` before committing.

set -euo pipefail

# ── CLI flags ─────────────────────────────────────────────────────────────────

ASSUME_YES=0
DRY_RUN_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --yes|-y)      ASSUME_YES=1 ;;
        --dry-run|-n)  DRY_RUN_ONLY=1 ;;
        --help|-h)
            sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

# ── Config ────────────────────────────────────────────────────────────────────

# Default path to your R&D repo. Override via env var if it moves.
RD_REPO="${RD_REPO:-/Users/julien/DATA/WORK/RESEARCH_DEV/20260425_LinkAudio/td-linkaudio}"

# Directories whitelisted for sync.
# IMPORTANT NOTES:
# - 'of'  is intentionally EXCLUDED — lives in gluon/ofxAbletonLinkAudio.
# - 'thirdparty' is intentionally EXCLUDED — submodules are managed by THIS
#   public repo's own .gitmodules, never copied from R&D.
DIRS_TO_SYNC=(
    "core"
    "td"
    "max"
    "vcv"
)

# Top-level files we also want to keep synced from R&D.
# .gitmodules is NOT in this list — it's maintained independently in the public
# repo so submodule URLs and pinning are explicit.
FILES_TO_SYNC=(
    "LICENSE"
    "ACKNOWLEDGEMENTS.md"
)

# rsync excludes — applied to every sync'd directory.
RSYNC_EXCLUDES=(
    # ── Git internals ────────────────────────────────────────────────────────
    --exclude='.git'
    --exclude='.git/**'
    --exclude='.gitmodules'

    # ── Build directories ────────────────────────────────────────────────────
    --exclude='build'
    --exclude='build/**'
    --exclude='Build'
    --exclude='Build/**'
    --exclude='**/build'
    --exclude='**/build/**'
    --exclude='**/Build'
    --exclude='**/Build/**'
    --exclude='cmake-build-*'
    --exclude='CMakeCache.txt'
    --exclude='CMakeFiles'
    --exclude='CMakeFiles/**'
    --exclude='cmake_install.cmake'

    # ── macOS binaries ───────────────────────────────────────────────────────
    --exclude='*.mxo'
    --exclude='*.mxo/**'
    --exclude='*.dylib'
    --exclude='*.so'
    --exclude='*.plugin'
    --exclude='*.plugin/**'
    --exclude='*.vst3'
    --exclude='*.vst3/**'
    --exclude='*.component'
    --exclude='*.component/**'
    --exclude='*.app'
    --exclude='*.app/**'

    # ── Windows binaries ─────────────────────────────────────────────────────
    --exclude='*.mxe64'
    --exclude='*.dll'
    --exclude='*.exe'
    --exclude='*.exp'
    --exclude='*.lib'
    --exclude='*.pdb'
    --exclude='*.idb'
    --exclude='*.ilk'
    --exclude='*.obj'
    --exclude='*.map'

    # ── VCV Rack release format ──────────────────────────────────────────────
    --exclude='*.vcvplugin'

    # ── Object / intermediate files (generic) ────────────────────────────────
    --exclude='*.o'
    --exclude='*.a'

    # ── macOS metadata ───────────────────────────────────────────────────────
    --exclude='.DS_Store'
    --exclude='**/.DS_Store'
    --exclude='DerivedData'
    --exclude='**/DerivedData/**'
    --exclude='*.xcuserstate'
    --exclude='xcuserdata'
    --exclude='xcuserdata/**'

    # ── Editor / cache ───────────────────────────────────────────────────────
    --exclude='.vscode'
    --exclude='.idea'
    --exclude='__pycache__'
    --exclude='*.pyc'

    # ── openFrameworks per-dev local config (in case they ever leak) ─────────
    --exclude='config.make'

    # ── Notes / WIP files ────────────────────────────────────────────────────
    --exclude='notes.local.md'
    --exclude='*.scratch.md'
    --exclude='TODO.local.md'
)

# Strings that MUST NEVER appear in the synced output. If grep finds any of
# them, the script aborts and asks the user to investigate.
LEAK_PATTERNS=(
    'sv_ui'
    'sv_dsp'
    'sv_linkaudio'
    'StructureVoid'
    'structurevoid'
    'SV_VST'
    'VlaR'
    'VlaS'
    'VoidLinkAudioReceive'
    'VoidLinkAudioSend'
    '8UX77EF9ZR'                # Apple Developer ID
    'SV_NOTARY'                 # notarytool keychain profile
    'sign_and_notarize'
    '/Users/julien'
)

# ── Sanity checks ─────────────────────────────────────────────────────────────

# (a) Refuse to run unless we're in the public repo root.
if [ ! -f "tools/sync-from-rd.sh" ]; then
    echo "ERROR: Run from the public repo root (where tools/sync-from-rd.sh lives)."
    exit 1
fi

# (b) R&D path must exist and be a git repo.
if [ ! -d "$RD_REPO" ]; then
    echo "ERROR: R&D repo not found at:"
    echo "  $RD_REPO"
    echo
    echo "Override with the RD_REPO environment variable:"
    echo "  RD_REPO=/path/to/your/rd ./tools/sync-from-rd.sh"
    exit 1
fi
if [ ! -d "$RD_REPO/.git" ]; then
    echo "ERROR: $RD_REPO does not look like a git repo (no .git/)."
    exit 1
fi

# (c) Refuse to sync to itself if someone misconfigures.
if [ "$(cd "$RD_REPO" && pwd)" = "$(pwd)" ]; then
    echo "ERROR: RD_REPO points to the public repo itself. Aborting."
    exit 1
fi

# (d) Refuse to run on a remote that isn't the public one (best-effort).
PUBLIC_REMOTE="$(git config --get remote.origin.url 2>/dev/null || echo '')"
case "$PUBLIC_REMOTE" in
    # Patterns that identify a PRIVATE remote — we must NEVER sync into them.
    # The public repo is gluon/Void-LinkAudio (with hyphen).
    # The private R&D is gluon/VoidLinkAudio (no hyphen) or gluon/td-linkaudio (legacy).
    *gluon/VoidLinkAudio.git*|*gluon/VoidLinkAudio/*\
    |*td-linkaudio*|*SV_VST*|*-private*)
        echo "ERROR: This repo's origin remote ($PUBLIC_REMOTE)"
        echo "       looks like a PRIVATE repo. This script only runs in the"
        echo "       public repo (gluon/Void-LinkAudio). Aborting."
        exit 1
        ;;
esac

echo "═══════════════════════════════════════════════════════════════════════"
echo "  VoidLinkAudio: sync R&D → public"
echo "═══════════════════════════════════════════════════════════════════════"
echo "  source : $RD_REPO"
echo "  target : $(pwd)"
echo "  remote : ${PUBLIC_REMOTE:-(none)}"
if [ "$DRY_RUN_ONLY" -eq 1 ]; then
    echo "  mode   : DRY RUN ONLY (no files will change)"
fi
echo "═══════════════════════════════════════════════════════════════════════"
echo

# ── Phase 1 — Dry-run preview ─────────────────────────────────────────────────

echo "Phase 1/3 — Dry-run preview"
echo "----------------------------"

for d in "${DIRS_TO_SYNC[@]}"; do
    src="$RD_REPO/$d"
    if [ -d "$src" ]; then
        echo
        echo "[dry-run] $d/"
        rsync -avn --delete "${RSYNC_EXCLUDES[@]}" "$src/" "$d/" \
            | grep -vE '^(building file list|sent |total size|sending incremental)' \
            | head -50
        n_changes=$(rsync -avn --delete "${RSYNC_EXCLUDES[@]}" "$src/" "$d/" \
            | grep -cvE '^(building file list|sent |total size|sending incremental|^\s*$|^\./$)' \
            || true)
        echo "  ($n_changes change-ish lines, see above truncated to 50)"
    fi
done

echo
echo "Top-level files to copy:"
for f in "${FILES_TO_SYNC[@]}"; do
    src="$RD_REPO/$f"
    if [ -f "$src" ]; then
        echo "  $f  (would copy)"
    else
        echo "  $f  (skip — not in R&D)"
    fi
done

echo

if [ "$DRY_RUN_ONLY" -eq 1 ]; then
    echo "Dry-run only requested. Exiting before any change."
    exit 0
fi

# ── Phase 2 — Confirmation ────────────────────────────────────────────────────

if [ "$ASSUME_YES" -ne 1 ]; then
    echo "Phase 2/3 — Confirmation"
    echo "------------------------"
    echo "Review the dry-run output above."
    read -rp "Proceed with the actual sync? [y/N] " yn
    case "$yn" in
        [yY]|[yY][eE][sS]) ;;
        *) echo "Aborted."; exit 1 ;;
    esac
fi

# ── Phase 3 — Real sync ───────────────────────────────────────────────────────

echo
echo "Phase 3/3 — Syncing"
echo "-------------------"

for d in "${DIRS_TO_SYNC[@]}"; do
    src="$RD_REPO/$d"
    if [ -d "$src" ]; then
        echo "  -> $d/"
        mkdir -p "$d"
        rsync -a --delete "${RSYNC_EXCLUDES[@]}" "$src/" "$d/"
    else
        echo "  (skip $d/  -  not in R&D)"
    fi
done

for f in "${FILES_TO_SYNC[@]}"; do
    src="$RD_REPO/$f"
    if [ -f "$src" ]; then
        echo "  -> $f"
        cp "$src" "$f"
    else
        echo "  (skip $f  -  not in R&D)"
    fi
done

# ── Phase 4 — Leak detection ──────────────────────────────────────────────────

echo
echo "Phase 4/3 — Paranoid leak check"
echo "-------------------------------"

leak_found=0
for pattern in "${LEAK_PATTERNS[@]}"; do
    # Search only in synced dirs + top files. Skip thirdparty/ (Ableton/link
    # may legitimately mention things) and the .git/ dir.
    matches=$(grep -RIin --binary-files=without-match \
              --exclude-dir=.git \
              --exclude-dir=thirdparty \
              -- "$pattern" core/ td/ max/ vcv/ \
              "${FILES_TO_SYNC[@]}" 2>/dev/null \
              | head -5 || true)
    if [ -n "$matches" ]; then
        echo
        echo "  ⚠️  LEAK SUSPECT — pattern: '$pattern'"
        echo "$matches" | sed 's/^/      /'
        leak_found=1
    fi
done

if [ "$leak_found" -eq 1 ]; then
    echo
    echo "═══════════════════════════════════════════════════════════════════════"
    echo "  ⚠️  WARNING: leak patterns matched. INVESTIGATE before committing."
    echo "═══════════════════════════════════════════════════════════════════════"
fi

# ── Phase 5 — Binary leak check ───────────────────────────────────────────────

echo
echo "Phase 5/3 — Binary leak check"
echo "-----------------------------"

bin_leaks=$(find . \( \
    -name "*.mxo"      -o -name "*.mxe64"   -o -name "*.dylib"     \
 -o -name "*.so"        -o -name "*.dll"     -o -name "*.exe"      \
 -o -name "*.plugin"    -o -name "*.vst3"    -o -name "*.component" \
 -o -name "*.app"       -o -name "*.vcvplugin"                     \
 -o -name "*.lib"       -o -name "*.exp"     -o -name "*.pdb"       \
 -o -name "*.idb"       -o -name "*.ilk"     -o -name "*.obj"       \
 -o -name "*.o"         -o -name "*.a"                              \
\) -not -path "./thirdparty/*" -not -path "./.git/*" 2>/dev/null || true)

if [ -n "$bin_leaks" ]; then
    echo "  ⚠️  Binary leak suspects:"
    echo "$bin_leaks" | sed 's/^/      /'
else
    echo "  ✓ No binary artifacts found in tracked area."
fi

# ── Done ──────────────────────────────────────────────────────────────────────

echo
echo "═══════════════════════════════════════════════════════════════════════"
echo "  Sync complete."
echo "═══════════════════════════════════════════════════════════════════════"
echo
echo "Next steps:"
echo "  git status"
echo "  git diff --stat"
if [ "$leak_found" -eq 1 ] || [ -n "$bin_leaks" ]; then
    echo
    echo "  ⚠️  Address the warnings above BEFORE 'git add .'"
fi
echo "  git add ."
echo "  git commit -m 'Update from R&D'"
echo "  git push"
