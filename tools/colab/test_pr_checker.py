from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pr_checker as pc  # noqa: E402

A, B = "a" * 40, "b" * 40


def test_the_newest_real_check_is_the_last_report_not_a_skip_note():
    comments = [{"body": f"<!-- prcheck {A} -->\n**Проверка**"}, {"body": f"<!-- prcheck {B} -->\n⏸️ не проверялся заново"},
                {"body": "arena-pr=523 status=rework"}]
    assert pc.last_checked_head(comments) == A
    assert pc.last_checked_head([{"body": "nothing"}]) == ""


def test_a_restored_state_learns_checked_and_reworked_prs_from_github(monkeypatch):
    saved = []
    monkeypatch.setattr(pc, "save_state", lambda s: saved.append(1))
    views = {"1": [{"body": f"<!-- prcheck {A} -->\nok"}], "2": [{"body": f"<!-- prcheck {A} -->\nok"}], "3": []}
    monkeypatch.setattr(pc, "gh", lambda *a, **k: __import__("json").dumps({"comments": views[a[2]]}))
    state = {"checked": {}}
    for n, head in (("1", A), ("2", B), ("3", B)):
        pc.sync_from_comments({"number": int(n), "headRefOid": head, "isDraft": False}, state)
    assert state["checked"] == {"1": A, "2": A}   # 1: done on this head; 2: checked before, new head = rework first; 3: new
    calls = len(saved)
    pc.sync_from_comments({"number": 3, "headRefOid": B, "isDraft": False}, state)   # once per head
    assert len(saved) == calls
