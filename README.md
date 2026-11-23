# microWakeWord for hassmic

[microWakeWord](https://github.com/kahrendt/microWakeWord), the open wake word engine of ESPHome's voice
satellites, as a plugin for [hassmic](https://github.com/Gamer92000/echo-dot-assist), which turns Amazon Echos into
Home Assistant voice satellites. With it an Echo listens for wake words from models you add ("Okay Nabu", "Hey
Jarvis", your own), beside or instead of Amazon's.

It hears the wake word less reliably than Amazon's engine, which stays hassmic's default.

**State (2026-11-23):** version 8 is version 7 with a manifest that names the scopes of hassmic's plugin protocol
it speaks, `"requires": {"core": 1, "wake": 1, "files": 1}`, in place of the plugin API it was built for: from this
hassmic on a scope only ever adds, and the plugin runs on every later hassmic without a new build ("Versions").
Versions 1 to 7 were each the one before built for the next plugin API, 0.2 to 0.7. The plugin passes its test in hassmic's PC
build, and its ARM package passes the plugin index's checks. It has not run on an Echo yet, no version is released, and hassmic's plugin system is itself
experimental and unreleased.

## What it does

- Runs as a hassmic plugin: a program of its own in a sandbox, with no files, no network and none of hassmic's
  keys. It is given the microphone stream and the models you uploaded, and says when it heard a word. A model that
  gets past its checks can break the wake word and nothing else.
- Takes microWakeWord's streaming models (`.tflite`, with the `.json` manifest of the same name if you have it),
  up to 16 of them. ESPHome's own models are one click away on the plugin's card.
- Two wake words at a time (Home Assistant's "Wake word" and "Wake word 2"), a threshold and a window per model.
- Computes the features bit for bit as the models were trained on them, and the models as TensorFlow Lite's
  reference kernels do, so a model answers as it does on an ESPHome device.

It asks for two capabilities, which the Echo shows before you accept it with the action button: the microphone all
the time (`wake`), and the files you give it on its page (`files`, 4 MB).

## Versions

`requires` in `manifest.json` is the least version of each scope the plugin speaks: `core`, and the scope of each
capability (`wake`, `files`). hassmic runs it wherever it has each at that version or later, and speaks exactly those
versions with it; a hassmic older than that (one an Echo fell back to) keeps it and refuses to start it, "needs a
newer hassmic". `sdk/VERSION` lists what the SDK copied in speaks; `make sync` says where the manifest requires more.
The rules are hassmic's: its [`sdk/PROTOCOL.md`](https://github.com/Gamer92000/echo-dot-assist/blob/main/sdk/PROTOCOL.md) "Versions".

## Install

On the Echo's settings page: switch on **Plugins**, open the Plugins page, **Browse plugins**, and **Install** on
microWakeWord's row. The Echo fetches the package from the [plugin index](https://github.com/Gamer92000/hassmic-plugins),
shows what it may do, and waits for its action button. Then add a model on the plugin's card and choose the plugin
under **Wake word engine**. An Echo kept off the internet: download `microwakeword-<version>.hmp` from the index's
page on a computer and use **Install from file** instead.

The package from the index carries the index's listing and installs as official. The same package as this
repository's release publishes it has no listing: from a file it installs as a sideload (the page says "From a file,
not reviewed"), and over a version installed from the index only with the action button (where a package comes from
is part of what was accepted). The index's revocation list can withdraw any version of it, official or not: an Echo
then stops it, and its owner can remove it or run it anyway with the action button.

## Build

One static program for 32-bit ARM, linked against musl. Never the Android NDK: a static bionic program does not
start in the sandbox.

```sh
make key                 # once: secrets/author.key (keep it; never in git) and keys/author.pub
make                     # build/microwakeword-<version>.hmp; needs zig, or PLUGIN_CC=<a compiler that links musl statically>
python3 sdk/plugcheck.py show build/microwakeword-2.hmp
```

`make host` builds the same program for the PC, and `make test` runs it inside hassmic's PC build: models through
the settings page, a real "Alexa" in a recording, a run in a stand-in Home Assistant. That needs a hassmic tree
beside this one (`HASSMIC=<path>`, default `../echo-dot-assist`) with `make host` done there. `make release-test`
runs `tools/release.sh` as the release does, with a throwaway key, and each of its refusals, and reads both workflows
for their rules (`tools/workflow_test.py`, the template's: the release job step for step as it allows them and
nothing else, the attestation in `release.yml`'s own job, the program built there by the pinned zig, the Package and
"Which hassmic" steps run as GitHub runs them, `bash -e`, against a SHA-256 and an `sdk/VERSION` that are none, every
action by the one commit `ACTIONS` names, its release tag's, `build.yml` never naming a secret or an environment nor
going on past a failure, both read by the index's own strict reader, held to the index's text where
`../hassmic-plugins` is beside this tree, and the rest of "Releases" below), and tests that test
(`tools/workflow_breaks.py`: broken copies, each failing with the FAIL line it names; each rule of the test a check
of its own, never one operand of another check's `and`; and each check switched off in turn, where some broken copy
has to pass; some 20 to 40 s); CI runs it.
The index checks the release itself all the same: its attestation, and every action both workflows use at the tag's
commit by a commit of the action's own repository (one of a fork runs under the original's name too: an impostor
commit), all the way down: each action's metadata read at its commit, a composite action's steps, another
repository's reusable workflow, an action of its own repository by `$/<path>` and a docker action's image and
Dockerfile `FROM` held to the same (a Dockerfile read only where Docker's builder cannot read it another way). Raising
an action is its commit in `ACTIONS` too, taken from the action's own tags.

## Layout

| | |
|---|---|
| `src/main.c` | the plugin: models as uploaded files, the stream, detections |
| `src/mww_features.c`, `mww_model.c`, `mww.h` | the engine: TFLite Micro's audio front end and an int8 interpreter for the 13 operators microWakeWord's models use |
| `src/json.c`, `json.h` | a JSON reader, for the models' manifests |
| `manifest.json` | what the plugin is, what it asks for, its settings |
| `sdk/` | hassmic's plugin SDK, copied in (`sdk/VERSION` says from where): client library, wire format, `plugcheck.py` |
| `tools/sync.sh` | takes `sdk/` and the engine from a hassmic tree, or says where they differ (`make sync`, `make drift`) |
| `tools/release.sh` | the release's package as `release.yml` makes it (`make release-test`: `tools/release_test.py`, `tools/workflow_test.py`, `tools/workflow_breaks.py`) |
| `.github/zig.txt` | the zig both the build and the release job build with, by its SHA-256 |
| `test.py` | the plugin in hassmic's PC build |

Until hassmic's own microWakeWord engine is removed from its core, the engine files here and there are the same
files: hassmic keeps its engine until the plugin has been measured against it on an Echo. `make drift` says when
they are not. hassmic's `tests/unit/mww_ref.py` holds the engine to pymicro-features and TensorFlow Lite.

## Releases

1. Raise `version` in `manifest.json`, tag `v<version>`. The tag's push runs `.github/workflows/release.yml` (and
   only that: the file runs for a tag's push and nothing else), which first calls CI (`.github/workflows/build.yml`:
   the program built twice, it has to come out the same; the release's packing test; the plugin in hassmic's PC
   build, hassmic at the commit `sdk/VERSION` names), then in its own job `release` builds the program again from the
   tag's commit with the same zig (`.github/zig.txt`, by its SHA-256) and refuses it unless it is the build job's,
   byte for byte (`tools/release.sh`: only the build job's SHA-256 comes from that job, never its program, which
   another job of the run could have replaced), packs it, signs the manifest with the author's key (secret
   `PLUGIN_SIGNING_KEY` of the environment `release`: base64 of `secrets/author.key`; the environment has to admit
   `v*` tags), attests the package (`actions/attest-build-provenance`: GitHub's word, signed with Sigstore, that
   `release.yml`, run for the push of the tag, at the tag's commit, on a runner GitHub hosts, built it) and publishes
   `microwakeword-<version>.hmp` with the attestation's bundle beside it. `gh attestation verify
   microwakeword-<version>.hmp --repo Gamer92000/hassmic-plugin-microwakeword --source-ref refs/tags/v<version>
   --cert-identity-regex
   '^https://github\.com/(?i:Gamer92000/hassmic-plugin-microwakeword)/\.github/workflows/release\.yml@refs/tags/v<version>$'
   --source-digest <the tag's commit> --signer-digest <the tag's commit> --deny-self-hosted-runners` checks it as the
   index does (the index's page prints the command whole per version). The attestation step stays in `release.yml`'s
   own job: one in `build.yml` would name `build.yml` as its signer, and the index refuses it. The index's maintainer
   reads `release.yml` and all it runs (`build.yml`, `tools/release.sh`, the `Makefile`, the actions by their
   commits). Nothing of what goes into the package may come from outside that commit: no action (`uses:`) not pinned by
   a commit of the action's own repository (one on its tags or branches: GitHub runs a fork's commit under the
   original's name too, an impostor commit, which the index refuses), all the way down (a composite action's own
   steps, a called workflow's, a docker action's image and its Dockerfile's `FROM`, by the same rule), no artifact of
   another job or run (one only to compare a build of its own with), no other job's outputs (`needs.*.outputs`) but a
   SHA-256 to compare with, nothing of the event's payload (`github.event.*`; a tag's annotation is not in the
   commit) but the tag's name, no other repository checked out at a ref that moves (a branch or a tag; a commit's id
   stays), no container image (`container:`, `docker://`) not pinned by its digest, no cache (`actions/cache`, the
   caches `setup-*` actions keep, which other runs write), no `vars` and no secret as an input (the signing key
   aside, which only the release job's environment holds: no other job names an environment, whose secrets it would
   get), no release asset already there, no toolchain resolved online by a channel (rustup's `stable`, `beta` or
   `nightly`, in a `rust-toolchain.toml` or on its command line), and no download not pinned by its hash; and a
   re-run of an old run, which GitHub attests as that tag's push again, has to make the same bytes.
2. An Echo takes updates of an installed plugin only from the key it was installed with (`keys/author.pub`). Keep
   the key; a new one is a new install on every Echo.
3. Listed: a pull request to the [plugin index](https://github.com/Gamer92000/hassmic-plugins) adds the version to
   `plugins/microwakeword.json`: its commit, its workflow (`.github/workflows/release.yml`), the manifest's digest
   (`plugcheck.py show` prints it) and this release's asset as its package (`https://github.com/Gamer92000/hassmic-plugin-microwakeword/releases/download/v<version>/microwakeword-<version>.hmp`).
   The index fetches that very package, checks it, verifies its attestation (this repository, that commit, a
   GitHub-hosted runner, `release.yml`'s own run for the push of that tag: official plugins take the same path as
   community ones, and a package uploaded by hand, or attested by another workflow or a run by hand, is not listed), adds its listing signed by the index key (official, since this is the
   project's own) and publishes it; that is the package Echos install. The index key never comes here, and
   nothing published here is listed by itself.

The index is built and signs (its `make check`, `make site`); neither repository is on GitHub yet.

## Licence

MIT, see `LICENSE`. The engine reimplements TensorFlow Lite Micro's audio front end and reference kernels
(Apache 2.0) and reads models trained with microWakeWord; the models themselves have their own licences.
