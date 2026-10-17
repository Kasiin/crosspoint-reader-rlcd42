"""Apply the reviewed RLCD delta to the exact SDK revision used by this fork."""
from pathlib import Path
import subprocess

PIN = "7fcd3b34c108f28c086ade3b433924022dbc0998"


def prepare(root):
    root = Path(root).resolve()
    sdk = root / "freeink-sdk"
    patch = root / "patches" / "freeink-sdk-rlcd42.patch"
    revision = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise RuntimeError(f"RLCD SDK patch requires {PIN}; found {revision}")

    def check(*args):
        return subprocess.run(["git", "-C", str(sdk), "apply", *args, str(patch)],
                              capture_output=True, text=True)

    if check("--reverse", "--check").returncode == 0:
        return
    result = check("--check")
    if result.returncode:
        raise RuntimeError("SDK has conflicting changes; not overwriting them.\n" + result.stderr)
    subprocess.run(["git", "-C", str(sdk), "apply", str(patch)], check=True)


if __name__ == "__main__":
    prepare(Path(__file__).resolve().parents[1])
elif "Import" in globals():
    Import("env")
    prepare(env.subst("$PROJECT_DIR"))
