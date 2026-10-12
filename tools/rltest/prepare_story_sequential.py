"""Generate a source/HOST-only sequential checkpoint package; never starts a core."""

import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = [
    "host_tests/story_sequential_package.c", "tools/rltest/story_six_fixture.c",
    "src/story/sc_core.c", "src/story/sc_graph.c", "src/story/sc_progression.c",
    "src/story/sc_make.c", "src/story/sc_combat.c", "src/story/sc_bridge.c",
    "src/story/sc_save.c", "src/story/sc_storage.c", "src/story/sc_crc.c",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(output, catalog_c, catalog_json):
    output = output.resolve()
    if not output.is_relative_to(ROOT / "build" / "story") or output.exists():
        raise ValueError("Use a fresh output directory under build/story")
    for source in (catalog_c, catalog_json):
        if not source.resolve().is_relative_to(ROOT) or not source.is_file():
            raise ValueError("Missing workspace-local reviewed generated catalog")
    output.mkdir(parents=True)
    flags = ["gcc", "-m32", "-DSTORY_NATIVE_UI", "-DSTORY_NATIVE_PROBE",
             "-DSTORY_SERVICE_CANDIDATE", "-DROGUELIKE", "-std=gnu89",
             "-Wall", "-Wextra", "-Werror", "-Wno-endif-labels",
             "-I.", "-iquote", "include", "-iquote", "src",
             "-include", "host_tests/story_campaign_run_seams.h"]
    command = flags + SOURCES + [str(catalog_c), "-o", str(output / "host-export")]
    subprocess.run(command, cwd=ROOT, check=True)
    result = subprocess.run([str(output / "host-export"), str(output)], cwd=ROOT,
                            capture_output=True, text=True, check=True)
    (output / "layout.json").write_text(result.stdout)
    abi = json.loads(result.stdout)
    data = json.loads(catalog_json.read_text())
    target = data["encounters"]["red"][abi["target"]["index"]]
    if target["id"] != abi["target"]["id"] or abi["event_size"] != 3:
        raise ValueError("Canonical encounter/observer ABI mismatch")
    inputs = ROOT / "tools/rltest/story_sequential_inputs.json"
    tracked = SOURCES + [
        "tools/rltest/story_six_fixture.h", "tools/rltest/story_sequential.py",
        "tools/rltest/story_sequential_pak.py",
        "tools/rltest/story_sequential_buttons.py",
        "tools/rltest/story_sequential_supervisor.py",
        "tools/rltest/bind_story_sequential.py",
        "src/fragments/62/fragment62_2F74E0.c", "src/fragments/62/fragment62.h",
        "tools/rltest/prepare_story_sequential.py",
        "tools/rltest/story_sequential_inputs.json",
        "src/story/sc_campaign.c", "src/story/sc_campaign_run.c",
        "src/story/sc_observer_layout.inc",
        "src/story/sc_native_campaign.c", "src/story/sc_native_campaign.h",
        "src/story/sc_campaign_battle.h", "src/story/sc_ui.c", "src/story/sc_ui.h",
        "src/story/sc_combat.h", "src/story/sc_core.h",
        "host_tests/story_campaign_run_seams.h",
    ]
    tracked = sorted(set(tracked) | {
        str(path.relative_to(ROOT)) for directory, pattern in
        (("src/story", "*.[ch]"), ("src/story", "*.inc"), ("src/roguelike", "*.h"))
        for path in (ROOT / directory).glob(pattern)
    } | {"include/rl_battle.h", "src/22630.h", "src/29BA0.h",
         "tools/rltest/frontend.py", "tools/rltest/save_roundtrip.py",
         "tools/rltest/story_probe.py", "tools/rltest/seal_story_current_six.py"})
    manifest = {
        "schema": 1,
        "scope": "source-HOST-only-not-native-seal-or-execution-authorization",
        "layout": abi,
        "target": target,
        "catalog_c_sha256": digest(catalog_c),
        "catalog_json_sha256": digest(catalog_json),
        "input_sha256": digest(inputs),
        "files": {name: digest(output / name) for name in
                  ("normal-route.sc01", "normal-interrupted.sc01",
                   "transform-route.sc01", "normal-route.note",
                   "normal-interrupted.note", "transform-route.note", "layout.json")},
        "source_sha256": {name: digest(ROOT / name) for name in tracked},
        "carry_reload": "runtime-derived win journal; cannot honestly pin its bytes before execution",
        "native_prerequisites": json.loads(inputs.read_text())["pre_execution_blockers"],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (output / "manifest.sha256").write_text(digest(output / "manifest.json") + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--catalog-c", type=pathlib.Path, required=True)
    parser.add_argument("--catalog-json", type=pathlib.Path, required=True)
    args = parser.parse_args()
    manifest = prepare(args.out, args.catalog_c, args.catalog_json)
    print(json.dumps({key: manifest[key] for key in
                      ("files", "input_sha256", "carry_reload")}, indent=2))


if __name__ == "__main__":
    main()
