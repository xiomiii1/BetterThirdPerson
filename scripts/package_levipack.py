import argparse
import json
import re
import zipfile
from pathlib import Path


def read_metadata(version_header: Path) -> dict[str, str]:
    text = version_header.read_text(
        encoding="utf-8"
    )

    values: dict[str, str] = {}

    for key in (
        "Name",
        "Author",
        "Description",
        "Version",
    ):
        pattern = re.compile(
            rf'inline\s+constexpr\s+std::string_view\s+'
            rf'{re.escape(key)}\s*=\s*"([^"]*)"\s*;',
            re.MULTILINE,
        )

        match = pattern.search(text)

        if match is None:
            raise RuntimeError(
                f"Missing {key} in {version_header}"
            )

        values[key] = match.group(1)

    return values


def write_package(
    library: Path,
    icon: Path,
    output: Path,
    version_header: Path,
) -> None:

    for required in (
        library,
        icon,
        version_header,
    ):
        if not required.is_file():
            raise FileNotFoundError(
                f"Required file does not exist: "
                f"{required}"
            )

    values = read_metadata(
        version_header
    )

    manifest = {
        "type": "preload-native",
        "name": values["Name"],
        "author": values["Author"],
        "description": values["Description"],
        "version": values["Version"],
        "entry": "libBetterThirdPerson.so",
        "icon": "icon.png",
        "overwrite_files": [
            "icon.png"
        ],
        "overwrite_folders": [],
    }

    output.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    if output.exists():
        output.unlink()

    with zipfile.ZipFile(
        output,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:

        archive.writestr(
            "manifest.json",
            json.dumps(
                manifest,
                indent=2
            ) + "\n",
        )

        archive.write(
            library,
            "libBetterThirdPerson.so",
        )

        archive.write(
            icon,
            "icon.png",
        )

    with zipfile.ZipFile(
        output,
        "r"
    ) as archive:

        expected = {
            "manifest.json",
            "libBetterThirdPerson.so",
            "icon.png",
        }

        actual = set(
            archive.namelist()
        )

        if actual != expected:
            raise RuntimeError(
                f"Invalid .levipack contents: "
                f"{actual}"
            )

        packaged_manifest = json.loads(
            archive.read(
                "manifest.json"
            )
        )

        if packaged_manifest != manifest:
            raise RuntimeError(
                "Packaged manifest verification failed"
            )


def main() -> None:

    parser = argparse.ArgumentParser(
        description=
            "Package BetterThirdPerson "
            "as a .levipack"
    )

    parser.add_argument(
        "--library",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--icon",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--version-header",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--output",
        type=Path,
        required=True,
    )

    args = parser.parse_args()

    write_package(
        args.library,
        args.icon,
        args.output,
        args.version_header,
    )

    print(
        args.output.resolve()
    )


if __name__ == "__main__":
    main()
