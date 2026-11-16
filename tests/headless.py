"""ROM-free end-to-end checks for both NDJSON backends (stdlib only)."""
import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def check_backend(executable, directory, sc62015):
    image = bytearray(0x40000 if sc62015 else 0x100000)
    if sc62015:
        banner = b"EL 9300 Ver. 1.11  "
        image[0x1422:0x1422 + len(banner)] = banner
        image[0x3FFFD:0x40000] = bytes([0, 1, 0x0C])
        image[0x100] = 0xDE  # HALT
    else:
        image[0xFFF00:0xFFF03] = bytes([0, 0, 0xF0])
        image[0] = 5  # HALT
    rom = directory / ("sc.bin" if sc62015 else "tlcs.bin")
    rom.write_bytes(image)
    process = subprocess.Popen(
        [executable, str(rom), "--control"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    try:
        assert json.loads(process.stdout.readline())["event"] == "ready"

        def command(name, **fields):
            process.stdin.write(json.dumps(dict(command=name, id="test", **fields)) + "\n")
            process.stdin.flush()
            result = json.loads(process.stdout.readline())
            assert result["id"] == "test" and result["command"] == name
            return result

        status = command("status")
        assert status["ok"] and status["machine"] == ("el92xx" if sc62015 else "el99xx")
        assert status["address_bits"] == (20 if sc62015 else 24)
        assert ("BA" if sc62015 else "XWA0") in status["registers"]
        shot = command("screenshot")
        width = 96 if sc62015 else 132
        header = f"P5\n{width} 64\n255\n".encode()
        pixels = base64.b64decode(shot["data_base64"])
        assert pixels.startswith(header) and len(pixels) == len(header) + width * 64
        state = command("state.dump")
        assert state["format"] == ("el93state" if sc62015 else "el99state")
        assert command("run", cycles=10)["ok"]
        assert command("state.load", data_base64=state["data_base64"])["ok"]
        assert command("state.dump")["data_base64"] == state["data_base64"]
        assert not command("keys", press=["no-such-key"])["ok"]
        assert command("status")["ok"]  # recover after command errors
        if sc62015:
            assert not command("memory.read", address=0x100000, length=1)["ok"]
            assert command("internal.write", address=0xF0, value=0x40)["ok"]
            assert command("keys", press=["clear"])["ok"]
            assert command("internal.read", address=0xF2)["value"] == 4
            assert command("keys", release=["clear"])["ok"]
            assert command("matrix", output=6, input=2, pressed=True)["ok"]
            assert command("internal.read", address=0xF2)["value"] == 4
            assert not command("matrix", output=16, input=0, pressed=True)["ok"]
            assert command("keys", press=["calc"], release=["calc"])["ok"]
        else:
            assert not command("internal.read", address=0)["ok"]
            assert not command("matrix", output=0, input=0, pressed=True)["ok"]
        assert command("quit")["ok"]
        process.wait(timeout=10)
        assert process.returncode == 0, process.stderr.read()
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=10)

    trace = directory / "io.ndjson"
    result = subprocess.run(
        [executable, str(rom), "10", "--trace-io", str(trace), "--json"],
        capture_output=True, text=True, check=True,
    )
    assert json.loads(result.stdout)["machine"] == ("el92xx" if sc62015 else "el99xx")
    for line in trace.read_text().splitlines():
        entry = json.loads(line)
        assert isinstance(entry["pc"], int) and isinstance(entry["address"], int)


with tempfile.TemporaryDirectory(prefix="sharp-headless-") as temporary:
    for cpu in (False, True):
        check_backend(sys.argv[1], Path(temporary), cpu)
print("Both headless backends passed protocol checks")
