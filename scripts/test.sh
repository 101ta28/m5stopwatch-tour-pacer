#!/usr/bin/env bash
script_dir=$(dirname "$0")
exec uv run --project "$script_dir/.." "$script_dir/test.py" "$@"
