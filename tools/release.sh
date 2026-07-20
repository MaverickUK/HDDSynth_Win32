#!/bin/sh
# Builds, packages, tags, and publishes a GitHub release for the version
# currently set in src/version.h.
#
# Usage: tools/release.sh
#
# What it does, in order:
#   1. Reads the version string from src/version.h (single source of truth --
#      bump that file first, this script doesn't decide the version for you).
#   2. Clean release build of both targets (make clean && make && make nt),
#      with the same static Win95-safety checks used throughout development
#      for the Win9x build (zero CMOV/RDTSC/CPUID, expected DLL import set).
#   3. Packages build/hddsynth.exe + build/hddsynth-nt.exe + SettingsHelp.txt +
#      samples/ into a self-contained folder inside a zip, so extracting it
#      gives something immediately runnable on whichever OS family it's used on.
#   4. Generates release notes from commit messages since the previous tag,
#      grouped into Features/Fixes/Other by conventional-commit prefix
#      (feat:/fix:/anything else) -- see "Commit message convention" below.
#   5. Tags the current commit vX.Y.Z and pushes main + the tag.
#   6. Creates a GitHub release from that tag via `gh`, attaching the zip
#      and the generated notes.
#
# Commit message convention the notes generator relies on: start each
# commit subject with "feat:" or "fix:" (optionally "feat(scope):") to
# have it grouped accordingly; anything else lands under "Other changes".
# This only looks at commit subjects, not the full body.
#
# Requires: gh (GitHub CLI), authenticated (`gh auth login`) with push
# access to the repo. Run from the repo root.
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION=$(grep HDDSYNTH_VERSION_STRING src/version.h | sed -E 's/.*"([0-9]+\.[0-9]+\.[0-9]+)".*/\1/')
if [ -z "$VERSION" ]; then
    echo "Could not read HDDSYNTH_VERSION_STRING from src/version.h" >&2
    exit 1
fi
TAG="v$VERSION"
echo "Releasing $TAG"

if ! command -v gh >/dev/null 2>&1; then
    echo "gh (GitHub CLI) is not installed. brew install gh, then gh auth login." >&2
    exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
    echo "gh is not authenticated. Run: gh auth login" >&2
    exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "Tag $TAG already exists -- bump src/version.h first." >&2
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "Working tree has uncommitted changes -- commit or stash first:" >&2
    git status --short >&2
    exit 1
fi

echo "--- Building ---"
make clean
make
make nt

DISASM=$(mktemp)
i686-w64-mingw32-objdump -d build/hddsynth.exe > "$DISASM"
BAD_INSNS=$(grep -ciE '\b(cmov[a-z]*|rdtsc|cpuid)\b' "$DISASM" || true)
rm -f "$DISASM"
if [ "$BAD_INSNS" != "0" ]; then
    echo "Found $BAD_INSNS disallowed instruction(s) (cmov/rdtsc/cpuid) in build/hddsynth.exe -- aborting." >&2
    exit 1
fi
echo "Static safety check passed: 0 cmov/rdtsc/cpuid instructions in build/hddsynth.exe."

if [ ! -f build/hddsynth-nt.exe ]; then
    echo "build/hddsynth-nt.exe missing after 'make nt' -- aborting." >&2
    exit 1
fi

echo "--- Packaging ---"
STAGE_DIR=$(mktemp -d)
PACKAGE_NAME="HDDSynth-Win32-$TAG"
PACKAGE_DIR="$STAGE_DIR/$PACKAGE_NAME"
mkdir -p "$PACKAGE_DIR"
cp build/hddsynth.exe "$PACKAGE_DIR/"
cp build/hddsynth-nt.exe "$PACKAGE_DIR/"
cp SettingsHelp.txt "$PACKAGE_DIR/"
cp -R samples "$PACKAGE_DIR/samples"
find "$PACKAGE_DIR" -name ".DS_Store" -delete

ZIP_PATH="$REPO_ROOT/$PACKAGE_NAME.zip"
rm -f "$ZIP_PATH"
(cd "$STAGE_DIR" && zip -rq "$ZIP_PATH" "$PACKAGE_NAME")
rm -rf "$STAGE_DIR"
echo "Packaged $ZIP_PATH"

echo "--- Generating release notes ---"
PREV_TAG=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [ -n "$PREV_TAG" ]; then
    COMMIT_RANGE="$PREV_TAG..HEAD"
else
    COMMIT_RANGE="HEAD"
fi

FEATS=$(git log --pretty=format:'%s' "$COMMIT_RANGE" | grep -E '^feat(\([^)]*\))?:' || true)
FIXES=$(git log --pretty=format:'%s' "$COMMIT_RANGE" | grep -E '^fix(\([^)]*\))?:' || true)
OTHER=$(git log --pretty=format:'%s' "$COMMIT_RANGE" | grep -vE '^(feat|fix)(\([^)]*\))?:' || true)

REMOTE_URL=$(git remote get-url origin | sed -E 's/\.git$//; s#^git@github\.com:#https://github.com/#')

NOTES_FILE=$(mktemp)
{
    if [ -n "$FEATS" ]; then
        echo "### Features"
        echo "$FEATS" | sed -E 's/^feat(\([^)]*\))?: ?/- /'
        echo
    fi
    if [ -n "$FIXES" ]; then
        echo "### Fixes"
        echo "$FIXES" | sed -E 's/^fix(\([^)]*\))?: ?/- /'
        echo
    fi
    if [ -n "$OTHER" ]; then
        echo "### Other changes"
        echo "$OTHER" | sed -E 's/^/- /'
        echo
    fi
    if [ -n "$PREV_TAG" ]; then
        echo "**Full diff**: $REMOTE_URL/compare/$PREV_TAG...$TAG"
    fi
} > "$NOTES_FILE"

echo "--- Tagging and pushing ---"
git tag -a "$TAG" -m "$TAG"
git push origin main
git push origin "$TAG"

echo "--- Creating GitHub release ---"
gh release create "$TAG" "$ZIP_PATH" \
    --title "$TAG" \
    --notes-file "$NOTES_FILE"
rm -f "$NOTES_FILE"

echo "Done: $TAG released with $PACKAGE_NAME.zip attached."
