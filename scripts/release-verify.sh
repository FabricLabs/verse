#!/usr/bin/env bash
# Full pre-release verification: native C checks + JS bundle + mocha.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> make release-check-native"
make release-check-native

if [[ -f package-lock.json ]]; then
  echo "==> npm ci"
  npm ci
  echo "==> npm run build"
  npm run build
  echo "==> npm run test:ci (headless; use npm test locally for full Puppeteer suite)"
  npm run test:ci
fi

echo "==> release-verify: OK"
