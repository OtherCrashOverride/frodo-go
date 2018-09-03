#!/bin/bash
rm Frodo.fw
TITLE="Frodo ($(date +%Y%m%d))"
./mkfw "$TITLE" tile.raw 0 16 2097152 frodo build/frodo-go.bin
mv firmware.fw Frodo.fw

