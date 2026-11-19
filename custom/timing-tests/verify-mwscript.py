import csv
import sys
from pathlib import Path

rows = list(csv.DictReader(Path(sys.argv[1]).open(newline="", encoding="utf-8")))
data = {(r["category"], r["source"], r["callback"]): r for r in rows}


def row(category, source, callback):
    return data[category, source, callback]


def total(category, source, callback):
    return float(row(category, source, callback)["total_us"])


run = total("mwscript", "outer", "run")
compile_time = total("mwscript_phase", "outer", "compile")
execute = total("mwscript_phase", "outer", "execute")
search = total("mwscript_native", "outer", "world.searchPtr")
inactive = total("mwscript_native", "outer", "world.findInactive")
load = total("mwscript_native", "outer", "cell.load")
assert run >= compile_time + execute
assert execute >= search >= inactive >= load > 0
assert abs(run - compile_time - execute - float(row("mwscript", "outer", "run")["self_us"])) < .01
assert row("mwscript_native", "inner", "world.disable")["calls"] == "1"
assert row("mwscript_native", "outer", "world.disable")["calls"] == "1"
assert not any(r["callback"] in {"outside.script", "after.script", "other.thread"} for r in rows)
assert all(float(r["total_us"]) >= float(r["self_us"]) >= 0 for r in rows)
print("PASS: MWScript phases, native nesting, exception restoration, thread isolation and inactive gating")
