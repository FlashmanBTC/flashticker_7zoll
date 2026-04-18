#!/bin/bash
# Usage: ./bump_version.sh 3.4
set -e

NEW=$1
if [ -z "$NEW" ]; then
  echo "Usage: $0 <new-version>  (e.g. $0 3.4)"
  exit 1
fi

OLD=$(grep -oP '(?<=#define FW_VERSION ")[^"]+' esp32s3/src/main.cpp)

if [ -z "$OLD" ]; then
  echo "Error: Could not detect current version in main.cpp"
  exit 1
fi

echo "Bumping $OLD → $NEW ..."

sed -i "s/#define FW_VERSION \"$OLD\"/#define FW_VERSION \"$NEW\"/" esp32s3/src/main.cpp
sed -i "s/\"version\": \"$OLD\"/\"version\": \"$NEW\"/" manifest.json
sed -i "s/v$OLD/v$NEW/g" index.html
sed -i "s/v$OLD-F7931A/v$NEW-F7931A/" README.md

echo ""
echo "Done. Updated 4 files:"
echo "  esp32s3/src/main.cpp  →  #define FW_VERSION \"$NEW\""
echo "  manifest.json         →  \"version\": \"$NEW\""
echo "  index.html            →  v$NEW (2 occurrences)"
echo "  README.md             →  badge v$NEW"
echo ""
echo "Next steps:"
echo "  1. Add changelog entry to README.md (## v$NEW section)"
echo "  2. Build + flash + test"
echo "  3. git add -p && git commit && git tag v$NEW && git push --tags"
