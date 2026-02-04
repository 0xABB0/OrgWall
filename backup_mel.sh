#!/bin/bash

BACKUP_DIR=".."
PREFIX="mel.bak"
EXT=".zip"

HIGHEST=0
for f in "$BACKUP_DIR"/${PREFIX}[0-9][0-9][0-9]${EXT}; do
    [[ -e "$f" ]] || continue
    NUM=$(basename "$f" | sed "s/${PREFIX}//" | sed "s/${EXT}//")
    NUM=$((10#$NUM))
    (( NUM > HIGHEST )) && HIGHEST=$NUM
done

NEXT=$((HIGHEST + 1))
NEXT_PADDED=$(printf "%03d" $NEXT)
BACKUP_NAME="${PREFIX}${NEXT_PADDED}${EXT}"

zip -r "$BACKUP_DIR/$BACKUP_NAME" . -x "build/*" -x ".claude/*" -x ".cache/*" -x "*.zip"

echo "Created backup: $BACKUP_DIR/$BACKUP_NAME"
