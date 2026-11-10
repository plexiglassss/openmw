"""Reuse the verified engine only for a packaging-only descendant of its CI run."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ENGINE = "12e5b050cb144396f213bb5ce193b6a7e7e7fce1"
RUN = 42782245589
ARTIFACT = 13753455262
ARCHIVE_SHA = "9a7c5b462534456796f8ab438316d30535181a4bd663a016e05ac1a1044cf95d"
PACKAGING_INPUTS = {
    ".github/workflows/push.yml",
    "custom/town-performance/source-plan.json",
    "custom/combined-performance/reuse-windows-engine.py",
    "custom/combined-performance/complete-windows-distribution.py",
    "custom/combined-performance/stage-msvc-runtime.ps1",
}


def verify_and_extract(archive, install):
    if install.exists():
        raise RuntimeError("Expected a fresh staging directory")
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != ARCHIVE_SHA:
            raise ValueError("The downloaded archive differs from the original successful Windows run")
    with zipfile.ZipFile(archive) as package:
        inventory = json.loads(package.read("PACKAGE-SHA256.json"))["files"]
        if {i.filename for i in package.infolist() if not i.is_dir()} != set(inventory) | {"PACKAGE-SHA256.json"}:
            raise ValueError("The original archive has unexpected files")
        for path, row in inventory.items():
            with package.open(path) as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != row["sha256"]:
                    raise ValueError("Original package SHA-256 mismatch: " + path)
        build = json.loads(package.read("COMBINED-PACKAGE.json"))
        if build["candidate_commit"] != ENGINE or build["result"] != "PASS":
            raise ValueError("The original executable has a different source revision")
        package.extractall(install)


def may_reuse():
    if subprocess.run(["git", "merge-base", "--is-ancestor", ENGINE, "HEAD"]).returncode:
        return False
    changes = subprocess.check_output(["git", "diff", "--name-only", "-z", ENGINE, "HEAD"]).decode().split("\0")
    return bool(set(changes) - {""}) and (set(changes) - {""}) <= PACKAGING_INPUTS


def restore():
    if not may_reuse():
        return False
    endpoint = f"repos/{os.environ['GITHUB_REPOSITORY']}/actions/artifacts/{ARTIFACT}"
    response = subprocess.run(["gh", "api", endpoint], capture_output=True)
    if response.returncode:
        print("Previous artifact unavailable. Building the complete engine and test suites.")
        return False
    artifact = json.loads(response.stdout)
    if artifact["expired"]:
        return False
    if (artifact["workflow_run"]["id"] != RUN or artifact["workflow_run"]["head_sha"] != ENGINE
            or artifact["digest"] != "sha256:" + ARCHIVE_SHA):
        raise ValueError("Prior run, engine or archive identity mismatch")
    archive = Path(os.environ["RUNNER_TEMP"]) / "native-engine-12e5b050.zip"
    with archive.open("wb") as out:
        result = subprocess.run(["gh", "api", endpoint + "/zip"], stdout=out)
    if result.returncode:
        raise RuntimeError("Unable to retrieve the original engine ZIP")
    verify_and_extract(archive, Path("install"))
    print("Verified every original package file. Reusing the tested engine and its test results.")
    return True


if __name__ == "__main__":
    reused = restore()
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as out:
        print("reused=" + str(reused).lower(), file=out)
