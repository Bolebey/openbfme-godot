#!/bin/bash
# Sandbox 3: the per-PR checker (tools/colab/pr_checker.py) from today's main; its state survives restarts in /content/sandbox.
set -e
cd /content/sandbox/openbfme-godot
git fetch -q origin
git show origin/main:tools/colab/pr_checker.py > /content/sandbox/pr_checker.py
export PRCHECK_STATE=/content/sandbox/prcheck-state.json
exec python3 /content/sandbox/pr_checker.py
