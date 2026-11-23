#!/bin/sh
# What this repository takes from hassmic's tree, copied in (sync.sh [HASSMIC]) or compared (sync.sh --check [HASSMIC]):
#
#   sdk/   the plugin SDK as hassmic's tree has it (its sdk/, and scripts/otatool.py for the signatures): the client
#          library, the wire format, the package tool.  A copy, so that this repository builds by itself and a build
#          names exactly the SDK it was made with (sdk/VERSION: the scopes and the version of each, and the
#          hassmic commit).  An older SDK stays good for good: hassmic's scopes only ever add (its
#          sdk/PROTOCOL.md "Versions"), and the plugin speaks what manifest.json "requires" names.
#   src/   the engine (mww_features.c, mww_model.c, mww.h) and the JSON reader (json.c, json.h).  hassmic keeps its
#          own microWakeWord engine until the plugin has been measured against it on an Echo (hassmic's
#          docs/plugins.md 5.9, "the cut"); until then the two are the same files, and --check says when they are
#          not.  After the cut they are this repository's alone and these lines go.
set -e
cd "$(dirname "$0")/.."
check=; [ "$1" = --check ] && { check=1; shift; }
H=${1:-../echo-dot-assist}
[ -f "$H/sdk/hassmic_plugin.h" ] || { echo "sync: no hassmic tree at $H (give its path)" >&2; exit 1; }
bad=0
each() {        # <file in hassmic's tree> <file here>
    if [ -n "$check" ]; then
        cmp -s "$H/$1" "$2" || { echo "differs: $2 (hassmic: $1)"; bad=1; }
    else
        cp "$H/$1" "$2"
    fi
}
for f in hassmic_plugin.h hmplugin.c plugmsg.h plugcheck.py; do each sdk/$f sdk/$f; done
each scripts/otatool.py sdk/otatool.py
for f in mww_features.c mww_model.c mww.h json.c json.h; do each src/hassmic/$f src/$f; done
[ -n "$check" ] && { [ $bad = 0 ] && echo "sync: as hassmic's tree at $H"; exit $bad; }
# the scopes and the version of each this SDK has, as its plugmsg.h makes them: "core 1 storage 1 ..."
scopes=$(python3 - <<'PY'
import re
h = open("sdk/plugmsg.h").read()
names = re.findall(r'case PM_SCOPE_\w+: return "([a-z0-9._]+)";', h)
vers = dict(re.findall(r"PM_SCOPE_VERSION_(\w+) = (\d+)", h))
print(" ".join(f"{n} {vers[n.upper().replace('.', '_')]}" for n in names))
PY
)
[ -n "$scopes" ] || { echo "sync: no scopes in sdk/plugmsg.h" >&2; exit 1; }
commit=$(git -C "$H" rev-parse HEAD 2>/dev/null || echo unknown)
[ -z "$(git -C "$H" status --porcelain -- sdk scripts/otatool.py src/hassmic/mww_features.c src/hassmic/mww_model.c src/hassmic/mww.h src/hassmic/json.c src/hassmic/json.h 2>/dev/null)" ] || commit="$commit with changes not committed there"
printf 'scopes %s\nhassmic %s\n' "$scopes" "$commit" > sdk/VERSION
cat sdk/VERSION
# what manifest.json requires that this SDK's client does not speak (a later version than its own, a scope it has not)
SCOPES="$scopes" python3 - manifest.json <<'PY' >&2 || true
import json, os, sys
have = dict(zip(*[iter(os.environ["SCOPES"].split())] * 2))
for k, v in (json.load(open(sys.argv[1])).get("requires") or {}).items():
    if not isinstance(v, int) or int(have.get(k, 0)) < v:
        print(f"sync: {sys.argv[1]} requires {k} {v}, which this SDK's client does not speak (README.md \"Versions\")")
PY
