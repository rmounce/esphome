#!/bin/bash

set -euo pipefail

BASEVER=$1

git fetch upstream
git rebase "$BASEVER"
git diff "$BASEVER" > my-esphome.patch

cat << EOF | docker build -t my-esphome:latest -f - .
FROM esphome/esphome:$BASEVER
RUN apt update && apt install patch
ADD my-esphome.patch /tmp/
RUN cd /esphome && patch -p1 -i /tmp/my-esphome.patch
#RUN pip3 install --no-cache-dir /esphome
EOF
