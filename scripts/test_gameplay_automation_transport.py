"""Launch-level Windows pipe regression. Run with --exe and --assets; no third-party packages."""
import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid

from gameplay_automation import Client, AutomationError


def raw_response(client, data):
    while data:
        data = data[client._transfer(data):]
    while b"\n" not in client.pending:
        client.pending += client._transfer(None)
    line, client.pending = client.pending.split(b"\n", 1)
    return json.loads(line.decode("utf-8"))


def run(executable, assets, output, production=None):
    output.mkdir(parents=True, exist_ok=False)
    session = str(uuid.uuid4())
    root = output / "user-data"
    base = [str(executable), "--assets", str(assets), "--resource-id", "JXQY2", "--skip-startup-video"]
    cases = {
        "no_runtime_authorization": ["--automation-pipe", session, "--user-data-root", str(root)],
        "no_isolated_root": ["--enable-automation-hooks", "--automation-pipe", session],
        "normal_player_directory": ["--enable-automation-hooks", "--automation-pipe", session,
                                    "--user-data-root", str(assets.parent)],
        "resource_directory": ["--enable-automation-hooks", "--automation-pipe", session,
                               "--user-data-root", str(assets)],
        "invalid_name": ["--enable-automation-hooks", "--automation-pipe", "bad/name", "--user-data-root", str(root)],
        "scenario_injection": ["--enable-automation-hooks", "--automation-pipe", session,
                               "--user-data-root", str(root), "--startup-int", "Injected=1"],
    }
    results = {}
    for name, arguments in cases.items():
        process = subprocess.run(base + arguments, cwd=executable.parent, capture_output=True, timeout=15)
        assert process.returncode == 64, (name, process.returncode, process.stderr)
        results[name] = "passed"
    if production:
        process = subprocess.run([str(production), *base[1:], "--enable-automation-hooks", "--automation-pipe", session,
                                  "--user-data-root", str(root)], cwd=production.parent, capture_output=True, timeout=15)
        assert process.returncode == 64, process.returncode
        results["production_rejects_pipe"] = "passed"
    try:
        Client(session, timeout=0.2).close()
        raise AssertionError("Rejected launch exposed an endpoint")
    except AutomationError:
        results["rejected_launch_has_no_endpoint"] = "passed"

    root.mkdir(parents=True)
    (root / "automation").write_text("Block auxiliary output for this regression", encoding="utf-8")
    with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
        process = subprocess.Popen(base + ["--enable-automation-hooks", "--automation-pipe", session,
                                          "--user-data-root", str(root)], cwd=executable.parent, stdout=stdout, stderr=stderr)
    try:
        with Client(session, timeout=15, transcript=output / "commands.jsonl") as client:
            state = client.wait_until(lambda s: s["scene"] == "Title", description="title")
            assert not state["outputHealthy"]
            for name, request, reason in (
                ("invalid_utf8", b'\xff\n', "invalid_utf8"),
                ("duplicate_json_key", b'{"id":1,"id":2}\n', "invalid_json"),
                ("invalid_json", b'{]\n', "invalid_json"),
                ("protocol_version", b'{"version":2,"id":10,"command":"Observe","arguments":{}}\n', "unsupported_version"),
            ):
                response = raw_response(client, request)
                assert not response["ok"] and reason in response["error"], response
                results[name] = "passed"
            try:
                client.snapshot()
            except AutomationError as error:
                assert "capture_write_failed" in str(error), error
            else:
                raise AssertionError("Capture unexpectedly wrote into a regular file")
            assert client.observe()["scene"] == "Title", "Output failure changed the game"
            results["output_failure_preserves_game"] = "passed"
            # A message that never terminates is bounded; reconnect starts a new request sequence.
            data = b"x" * (65536 + 4096)
            try:
                while data:
                    data = data[client._transfer(data):]
                client._transfer(None)
            except AutomationError:
                pass
            else:
                raise AssertionError("Oversized message did not close the connection")
        time.sleep(0.2)
        with Client(session, timeout=5) as client:
            assert client.observe()["scene"] == "Title"
            results["oversized_request_and_reconnect"] = "passed"
            client.exit_game()
        assert process.wait(timeout=15) in (0, 2)  # ElementResult::erExit is the normal menu exit.
    finally:
        # This helper owns this fresh title-only process and its independent user data.
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        (output / "result.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--production-exe", type=Path)
    arguments = parser.parse_args()
    run(arguments.exe.resolve(), arguments.assets.resolve(), arguments.output.resolve(),
        arguments.production_exe.resolve() if arguments.production_exe else None)
