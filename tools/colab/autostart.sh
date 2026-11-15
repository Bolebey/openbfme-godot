#!/bin/bash
# Run by the Colab setup cell after the sandbox restore (the owner's two-line addition at the cell's end, 2026-11-15):
# starts every /content/sandbox/autostart.d/*.sh of this sandbox in the background, log /content/<name>.log.
# The folder is in /content/sandbox, so it survives restarts through the Drive backup; which jobs a sandbox runs is its own choice.
[ -f /root/.agent_env ] && source /root/.agent_env
mkdir -p /root/.vscode-server   # save_sandbox.sh tars it; a sandbox without VS Code had no backup at all (2026-11-15)
for f in /content/sandbox/autostart.d/*.sh; do
  [ -f "$f" ] || continue
  nohup bash "$f" > "/content/$(basename "$f" .sh).log" 2>&1 < /dev/null &
  echo "autostart: $f (pid $!)"
done
