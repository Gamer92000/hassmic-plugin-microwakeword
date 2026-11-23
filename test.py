#!/usr/bin/env python3
"""The plugin on the PC, in hassmic's PC build: built for the PC as it is built for an Echo (static, against the
SDK's client: make host), packed with this repository's own package tool and installed into hassmic's
build/hassmic-host under its build/plugrun-host, with pymicro-wakeword's models uploaded through the settings page
and hassmic's testdata/alexa_espeak.raw as the microphone.

Needs a hassmic tree (HASSMIC, default ../echo-dot-assist) with `make host` done; run with its .venv's python
(make test).  The ports are fixed: one run at a time.

The package as this repository releases it carries the author's signature only: a sideload.  The same package with
the listing the plugin index adds to it (plugcheck.py list, by a stand-in index key here, whose public half the
hassmic under test has): official.

A model with its manifest and one without (named after its file, the defaults), an upload that is no model (refused
with the interpreter's own reason), the engine switched to the plugin, a real detection that starts a run in Home
Assistant under the model's name, the threshold changed by the owner taking effect without a reload, and the models
kept in the plugin's own list (state/plugins/microwakeword/files/models/<id>/manifest.json, model.tflite), not in
the folder of the engine built into hassmic (state/mww, its own alone: hassmic's docs/plugins.md 5.9)."""
import asyncio, hashlib, json, os, signal, sys, tempfile
import pymicro_wakeword
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.environ.get("HASSMIC") or os.path.join(HERE, "..", "echo-dot-assist"))
if not os.path.exists(os.path.join(ROOT, "build", "hassmic-host")):
    sys.exit(f"no hassmic PC build in {ROOT}: set HASSMIC to a hassmic tree and run `make host` there")
sys.path.insert(0, os.path.join(HERE, "sdk"))
import plugcheck                                # this repository's copy: hassmic's tests then use it too
sys.path.insert(0, os.path.join(ROOT, "tests"))
from webclient import wait_port, Browser
import fake_plugins as fp                       # its helpers; guarded below from running its own main()
MODELS = os.path.join(os.path.dirname(pymicro_wakeword.__file__), "models")
check = fp.check


async def main():
    top = tempfile.mkdtemp(); state = os.path.join(top, "state"); os.mkdir(state); log = os.path.join(top, "log")
    for k in ("author", "index"): plugcheck.otatool.keygen(os.path.join(top, k + ".key"), os.path.join(top, k + ".pub"))
    side, out = os.path.join(top, "sideload.hmp"), os.path.join(top, "microwakeword.hmp")
    plugcheck.pack(f"{HERE}/manifest.json", f"{HERE}/build/microwakeword-host", os.path.join(top, "author.key"), side,
                   license_=f"{HERE}/LICENSE", arch="x86_64")
    plugcheck.list_package(side, os.path.join(top, "index.key"), out, arch="x86_64")
    # (community plugins on, as the owner switches them on an Echo's page, a setting as any since hassmic 00d492a:
    # the package as released here has no listing, a sideload, and since hassmic 3326cd4 none such is installed or
    # runs while they are off)
    open(os.path.join(state, "config"), "w").write("plugins=on\nplugins_community=on\n")
    proc = fp.start(state, log)
    try:
        wait_port(fp.PORT, fp.WEB)
        b = Browser(fp.WEB)
        check(b.login_with_button(proc), "logged in with the button")
        st, r = fp.upload(b, open(side, "rb").read())
        check(st == 200 and r.get("tier") == "sideload", f"the package as released here: a sideload: {st} {r.get('tier')} {r.get('error')}")
        fp.post(b, "/api/plugins/cancel")
        st, r = fp.upload(b, open(out, "rb").read())
        lines = [c["line"] for c in r.get("caps", [])]
        check(st == 200 and r.get("tier") == "official" and len(lines) == 2 and any("microphone all the time" in l for l in lines), f"the package: official, two capabilities: {lines}")
        fp.post(b, "/api/plugins/install"); proc.send_signal(signal.SIGUSR2)
        check(fp.until(lambda: (fp.plug(b, "microwakeword") or {}).get("state") == "running", 20), "installed with the button, running")

        tfl = lambda n: open(os.path.join(MODELS, n + ".tflite"), "rb").read()
        st, r = fp.item(b, "alexa", {".tflite": tfl("alexa"), ".json": open(os.path.join(MODELS, "alexa.json"), "rb").read()}, "microwakeword")
        st2, r2 = fp.item(b, "hey_jarvis", {".tflite": tfl("hey_jarvis")}, "microwakeword")
        items = {x["id"]: x for x in {y["name"]: y for y in fp.plug(b, "microwakeword")["settings"]}["models"]["items"]}
        check(st == 200 and r.get("checked") and items["alexa"]["title"] == "Alexa" and items["alexa"]["fields"] == {"cutoff": "0.90", "window": "5"},
              f"a model with its manifest: named and tuned by it: {items.get('alexa')}")
        check(st2 == 200 and items["hey_jarvis"]["title"] == "Hey Jarvis" and items["hey_jarvis"]["fields"]["cutoff"] == "0.97",
              f"one without: named after its file, the defaults: {items.get('hey_jarvis')}")
        st, r = fp.item(b, "noise", {".tflite": os.urandom(4000)}, "microwakeword")
        check(st == 400 and "TensorFlow Lite" in r.get("error", ""), f"not a model: refused, with the interpreter's reason: {r}")
        st, r = fp.item(b, "twenty", {".tflite": tfl("alexa"), ".json": b'{"type":"micro","wake_word":"Alexa","micro":{"probability_cutoff":0.9,"feature_step_size":20}}'}, "microwakeword")
        check(st == 400 and "every 20 ms" in r.get("error", ""), f"a manifest for 20 ms features: refused: {r}")
        d = os.path.join(state, "plugins", "microwakeword", "files", "models", "alexa")
        check(sorted(os.listdir(d)) == ["checked", "fields", "manifest.json", "model.tflite", "title"] and open(os.path.join(d, "model.tflite"), "rb").read() == tfl("alexa")
              and not os.path.exists(os.path.join(state, "mww", "alexa")),
              f"the models are in the plugin's own list, under the built-in engine's file names; nothing in its folder (state/mww): {sorted(os.listdir(d))}")

        n0 = len(open(log).read())
        r = b.set(wake_engine="plugin:microwakeword")
        check(r.get("applied") == 1 and fp.logged(log, "\"alexa\" listens", n0), f"the engine switched to the plugin, its first word loaded: {r}")
        ha, avail, active = await fp.ha_config()
        check(avail == [("alexa", "Alexa"), ("hey_jarvis", "Hey Jarvis")] and active == ["alexa"], f"Home Assistant lists its models: {avail} {active}")
        started = asyncio.Event(); phrases = []
        async def handle_start(conv_id, flags, settings, phrase): phrases.append(phrase); started.set(); return 0
        async def nothing(*a, **k): pass
        ha.subscribe_voice_assistant(handle_start=handle_start, handle_stop=nothing, handle_audio=nothing)
        try: await asyncio.wait_for(started.wait(), 25)
        except asyncio.TimeoutError: pass
        check(started.is_set() and phrases[-1] == "Alexa" and fp.logged(log, "wake: alexa (plugin microwakeword, score"),
              f"\"Alexa\" in the audio, heard by the plugin: a run for Home Assistant: {phrases}")
        check("a detection that cannot be" not in open(log).read(), "its detections pass hassmic's checks (in time, a second long)")

        n0 = len(open(log).read())
        st, r = fp.post(b, "/api/plugins/microwakeword/item/models/alexa/field", "cutoff=0.6")
        check(st == 200 and fp.logged(log, "\"alexa\": cutoff 0.60 over 5 x 30 ms", n0), "the owner's threshold takes effect in the open word, without a reload")
        check("\"alexa\" listens" not in open(log).read()[n0:], "(the word was not loaded again)")
        await ha.set_voice_assistant_configuration(["alexa", "hey_jarvis"])
        check(fp.logged(log, "\"hey_jarvis\" listens (second wake word)", n0), "a second word in the second slot")
        try: await ha.disconnect()
        except Exception: pass
    finally:
        proc.terminate()
        try: proc.wait(5)
        except Exception: proc.kill()
    if check.failed:
        print("---- log"); print(open(log, errors="replace").read()[-5000:])
    print("microwakeword plugin: " + ("FAILED" if check.failed else "all passed"))
    sys.exit(1 if check.failed else 0)

asyncio.run(main())
