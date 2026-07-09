#!/usr/bin/env bash
# deploy.sh — release a Neutrino tarball to GitHub, tagged with its version.
#   usage: ./deploy.sh ~/Downloads/neutrino.tar.gz [--no-test]
# Steps: extract the tarball's version, untar over this repo, build and run
# the full test suite, commit, push, and tag vX.Y.Z (same as version.h).
set -euo pipefail

TARBALL="${1:?usage: ./deploy.sh path/to/neutrino.tar.gz [--no-test]}"
RUN_TESTS=1
[[ "${2:-}" == "--no-test" ]] && RUN_TESTS=0

[[ -f "$TARBALL" ]] || { echo "deploy: no such file: $TARBALL" >&2; exit 1; }
[[ -d .git ]] || { echo "deploy: run from the repo root (no .git here)" >&2; exit 1; }

# 0. Read the version out of the tarball BEFORE touching the tree.
VERSION=$(tar xzf "$TARBALL" -O neutrino/version.h | sed -n 's/.*NEUTRINO_VERSION "\([0-9.]*\)".*/\1/p')
[[ -n "$VERSION" ]] || { echo "deploy: could not read NEUTRINO_VERSION from the tarball" >&2; exit 1; }
TAG="v$VERSION"
echo "deploy: releasing $TAG"

if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "deploy: tag $TAG already exists — bump the version in the tarball first" >&2
    exit 1
fi

# 1. Untar over the working tree (adds and overwrites; never deletes).
tar xzf "$TARBALL" --strip-components=1
echo "deploy: extracted over $(pwd)"

# 2. Build and verify before anything touches the remote.
if [[ $RUN_TESTS == 1 ]]; then
    make clean >/dev/null && make >/dev/null
    make test
    make test-asan
    echo "deploy: all tests green"
else
    echo "deploy: tests SKIPPED (--no-test)"
fi

# 3. Commit, push, tag.
git add -A
if git diff --cached --quiet; then
    echo "deploy: nothing to commit (tree already at this state)"
else
    git commit -m "release $TAG"
fi
git push origin main
git tag -a "$TAG" -m "Neutrino $VERSION"
git push origin "$TAG"

echo "deploy: done — $TAG is live (GitHub Pages redeploys docs/ automatically)"
