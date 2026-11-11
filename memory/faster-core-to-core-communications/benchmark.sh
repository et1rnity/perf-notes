#!/usr/bin/env bash

set -eo pipefail

strategies=(
  classic
  piggyback
  batch
  final
)

{
  for _ in {1..10}; do
    for strategy in "${strategies[@]}"; do
      ./build/release/memory/faster-core-to-core-communications/benchmark "$strategy" 1 1 0
    done
  done
} | jq -s '.' > result.json
