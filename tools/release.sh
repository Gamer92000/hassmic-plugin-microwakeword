#!/bin/sh
# The release's package, as .github/workflows/release.yml makes it once the build and the tests passed: the ARM
# program built here (make, zig), packed and signed with the author's key (plugcheck.py pack), checked again
# (plugcheck.py show).  The workflow then attests it (actions/attest-build-provenance) and publishes it as the
# release's asset microwakeword-<version>.hmp, which is what the plugin index lists (README.md "Releases").  The
# template's tools/release.sh, for this repository:
#
#   tools/release.sh TAG OWNER/REPOSITORY KEY DIST [PROGRAM_SHA256]
#
# TAG the tag (v<the manifest's version>), OWNER/REPOSITORY the GitHub repository (the manifest's source has to be
# https://github.com/OWNER/REPOSITORY: the index takes a package from its source's releases only, and the attestation
# names that repository), KEY the author's key (its public half has to be keys/author.pub: an Echo takes updates only
# from the key it pinned), DIST a folder for the package and its description.  PROGRAM_SHA256 (64 hex digits; required
# in the release job, GITHUB_JOB=release): the program has to be that one (the build job's, which the tests ran).  The
# program is built here, in the job that attests it, from the tagged commit, never taken from the build job's artifact:
# another job of the same run runs other code (the test job, hassmic's), and it could put another file under the
# artifact's name before the release job downloads it (a review of e5ff47c).  Only the artifact's SHA-256 is taken, to
# compare with: a build that is not reproducible, or an artifact that is not the build job's, fails here and not on an
# Echo.  PLUGIN_CC as the Makefile takes it.  It prints the package's path last.
set -eu
[ $# -ge 4 ] || { echo "usage: tools/release.sh TAG OWNER/REPOSITORY KEY DIST [PROGRAM_SHA256]" >&2; exit 2; }
tag=$1 repo=$2 key=$3 dist=$4 want=${5:-}
fail() { echo "::error::$*" >&2; exit 1; }
# a SHA-256 given is one: 64 hex digits, lower case as sha256sum prints it.  An empty one is no "no check" (the
# release workflow's, read from a file that was not there, came as "" once; a review of 6bca341)
if [ $# -ge 5 ]; then
    case $want in *[!0-9a-f]*) fail "PROGRAM_SHA256 '$want' is no SHA-256 (64 hex digits)" ;; esac
    [ ${#want} = 64 ] || fail "PROGRAM_SHA256 '$want' is no SHA-256 (64 hex digits)"
fi
# the release job (GitHub's GITHUB_JOB) always holds its build to the build job's: there it is no option
[ "${GITHUB_JOB:-}" != release ] || [ $# -ge 5 ] || fail "the release job gives the build job's SHA-256 (PROGRAM_SHA256)"
field() { python3 -c 'import json, sys; print(json.load(open("manifest.json"))[sys.argv[1]])' "$1"; }
id=$(field id)
version=$(field version)
[ -f keys/author.pub ] || fail "no keys/author.pub"
[ "$(field source)" = "https://github.com/$repo" ] ||
    fail "manifest.json's source is $(field source), not this repository (https://github.com/$repo): the plugin index takes the package from its source's releases only"
[ "$tag" = "v$version" ] || fail "tag $tag, but manifest.json says version $version"
# the key that signs is the one Echos pinned: another would make this release a new install everywhere
python3 -c 'import sys; k = open(sys.argv[1], "rb").read(); sys.exit(len(k) != 64 or k[32:] != open("keys/author.pub", "rb").read())' "$key" ||
    fail "the signing key is not the key of keys/author.pub"
rm -f "build/$id" "build/$id-$version.hmp"
make "build/$id" >&2
if [ $# -ge 5 ]; then
    [ "$(sha256sum "build/$id" | cut -d' ' -f1)" = "$want" ] ||
        fail "the program built here is not the build job's (the one the tests ran): the build is not reproducible, or the artifact is not the build job's"
fi
make "build/$id-$version.hmp" KEY="$key" >&2
mkdir -p "$dist"
cp "build/$id-$version.hmp" "$dist/"
# no pipe: a package whose signature does not hold ends the release here
python3 sdk/plugcheck.py show "$dist/$id-$version.hmp" > "$dist/$id-$version.txt"
cat "$dist/$id-$version.txt" >&2
echo "$dist/$id-$version.hmp"
