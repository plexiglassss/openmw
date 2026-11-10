"""Reuse matching Windows binaries for a packaging-only descendant, or build them from source."""
import argparse
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
TOOLS_HEAD = "1956cbbd39652d56843b9474c058ad08c4e179df"
TOOLS_RUN = 42798538724
TOOLS_ARTIFACT = 13757305351
TOOLS_SHA = "7cf254f3a931da32d720da8bba648cf4225ad0c1891037909aa220f26760873f"
TOOL_FILES = {"openmw-cs.exe", "openmw-essimporter.exe", "bsatool.exe", "esmtool.exe", "niftest.exe",
              "openmw-bulletobjecttool.exe", "defaults-cs.bin"}
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


def download(run, commit, artifact_id, digest):
    endpoint = f"repos/{os.environ['GITHUB_REPOSITORY']}/actions/artifacts/{artifact_id}"
    response = subprocess.run(["gh", "api", endpoint], capture_output=True)
    if response.returncode:
        return None
    artifact = json.loads(response.stdout)
    if artifact["expired"]:
        return None
    if (artifact["workflow_run"]["id"] != run or artifact["workflow_run"]["head_sha"] != commit
            or artifact["digest"] != "sha256:" + digest):
        raise ValueError("Prior run, commit or archive identity mismatch")
    archive = Path(os.environ["RUNNER_TEMP"]) / f"openmw-artifact-{artifact_id}.zip"
    for _ in range(2):
        with archive.open("wb") as out:
            result = subprocess.run(["gh", "api", endpoint + "/zip"], stdout=out)
        if not result.returncode:
            with archive.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
                    raise ValueError("Downloaded archive hash mismatch")
            return archive
    return None


def restore(companion_tools):
    if not may_reuse():
        return False
    if companion_tools:
        if subprocess.run(["git", "merge-base", "--is-ancestor", TOOLS_HEAD, "HEAD"]).returncode:
            return False
        archive = download(TOOLS_RUN, TOOLS_HEAD, TOOLS_ARTIFACT, TOOLS_SHA)
        if not archive:
            return False
        with zipfile.ZipFile(archive) as package:
            entries = package.namelist()
            if len(entries) != len(TOOL_FILES) or set(entries) != TOOL_FILES:
                raise ValueError("Unexpected contents in the companion tools archive")
            package.extractall("build")
        print("Reusing companion tools compiled from the same source. Launch checks will run on all six.")
    else:
        archive = download(RUN, ENGINE, ARTIFACT, ARCHIVE_SHA)
        if not archive:
            return False
        verify_and_extract(archive, Path("install"))
        print("Verified every original package file. Reusing the tested engine and its test results.")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--companion-tools", action="store_true")
    reused = restore(parser.parse_args().companion_tools)
    if not reused:
        print("The matching archive is unavailable or inapplicable. Building from source.")
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as out:
        print("reused=" + str(reused).lower(), file=out)
