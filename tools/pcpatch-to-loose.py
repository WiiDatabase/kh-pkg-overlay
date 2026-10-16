#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "click",
# ]
# ///
import fnmatch
import os
import shutil
import threading
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path, PurePosixPath

import click

KINDS = ("original", "remastered")
ARCHIVE_ORDER = ("first", "second", "third", "fourth", "fifth", "sixth")


def archive_rank(name: str) -> int:
    suffix = name.rsplit("_", 1)[-1].lower()
    return ARCHIVE_ORDER.index(suffix) if suffix in ARCHIVE_ORDER else len(ARCHIVE_ORDER)


def collect(patches: tuple[Path, ...], excludes: tuple[str, ...]) -> tuple[dict[str, tuple[Path, str]], int]:
    """Map each kh-mods relative path to (patch, member); later patches win, earlier archives win inside a patch."""
    tree: dict[str, tuple[Path, str]] = {}
    skipped = 0
    for patch in patches:
        chosen: dict[str, tuple[int, str]] = {}
        with zipfile.ZipFile(patch) as z:
            for member in z.namelist():
                parts = PurePosixPath(member).parts
                if member.endswith("/") or len(parts) < 3 or parts[1] not in KINDS:
                    continue
                if any(p in ("..", "") for p in parts) or member.startswith("/"):
                    raise click.ClickException(f"{patch.name}: unsafe path {member!r}")
                if any(fnmatch.fnmatch(member, pat) for pat in excludes):
                    skipped += 1
                    continue
                rel = "/".join(parts[1:])
                rank = archive_rank(parts[0])
                if rel not in chosen or rank < chosen[rel][0]:
                    chosen[rel] = (rank, member)
        for rel, (_, member) in chosen.items():
            tree[rel] = (patch, member)
    return tree, skipped


@click.command()
@click.argument("patches", nargs=-1, required=True, type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.option(
    "-o",
    "--output",
    required=True,
    type=click.Path(file_okay=False, path_type=Path),
    help="The program's romfs directory; files go to <output>/kh-mods/.",
)
@click.option("-x", "--exclude", multiple=True, help="Glob on patch paths to skip, e.g. '*/title.2ld/*'. Repeatable.")
@click.option("-j", "--jobs", default=os.cpu_count() or 4, show_default=True, help="Parallel writers.")
def main(patches: tuple[Path, ...], output: Path, exclude: tuple[str, ...], jobs: int) -> None:
    """Unpack KHPCPatchManager patches (.kh1pcpatch, .kh2pcpatch) into a kh-mods loose-file tree.

    Later PATCHES win over earlier ones. Within one patch, a path that appears in several
    archive folders is taken from the earliest archive (first, second, … sixth), the one the game reads.
    """
    tree, skipped = collect(patches, exclude)
    root = output / "kh-mods"
    local = threading.local()

    def write(item: tuple[str, tuple[Path, str]]) -> None:
        rel, (patch, member) = item
        zips = local.__dict__.setdefault("zips", {})
        if patch not in zips:
            zips[patch] = zipfile.ZipFile(patch)
        dest = root / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        with zips[patch].open(member) as src, open(dest, "wb") as dst:
            shutil.copyfileobj(src, dst, 1 << 20)

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(write, tree.items()))

    counts = {kind: sum(1 for rel in tree if rel.startswith(kind + "/")) for kind in KINDS}
    click.echo(f"{counts['original']} original, {counts['remastered']} remastered files in {root}, {skipped} excluded")


if __name__ == "__main__":
    main()
