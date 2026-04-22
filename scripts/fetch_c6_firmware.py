"""Fetch the ESP-Hosted ESP32-C6 slave firmware and embed it into flash.

This script runs as a PlatformIO pre-build step. It only has any effect
for the Waveshare build (``BOARD_WAVESHARE_P4_101`` defined) - the Tab5
doesn't use esp-hosted / esp_wifi_remote, so baking a 1.2MB blob there
would just waste flash.

What it does:

1. Resolves the target slave firmware version from
   ``ESP_HOSTED_VERSION_*`` inside framework-arduinoespressif32-libs.
   The host side of ``esp-hosted`` on the P4 expects an exact slave
   version match; shipping a blob that disagrees with the linked
   host library would just keep triggering update prompts forever.
2. Idempotently downloads
   ``https://espressif.github.io/arduino-esp32/hosted/esp32c6-v<ver>.bin``
   to ``src/generated/esp32c6-v<ver>.bin``. Re-downloads only if the
   cached file is missing or the byte count looks wrong.
3. Emits ``src/generated/c6_firmware_blob.S`` with a ``.incbin`` of the
   .bin, exposing three globals consumed by c6_firmware_update.cpp:
       ``c6_firmware_bin``         - start of blob
       ``c6_firmware_bin_end``     - one byte past the end
       ``c6_firmware_version_str`` - NUL-terminated "2.12.3"-style string
   .incbin resolves paths relative to the assembler's CWD, which under
   SCons is unpredictable; to sidestep that we generate the .S alongside
   the .bin and reference the bare basename.
4. Adds ``src/generated`` to ASFLAGS ``-I`` so the assembler finds the
   .bin at assembly time.

This script is idempotent: running it repeatedly never re-downloads a
valid cached blob, and only rewrites the .S if its contents would
change (so SCons' up-to-date check doesn't needlessly rebuild).
"""

from __future__ import annotations

import re
import urllib.error
import urllib.request
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # noqa: F821 - SCons injects

_FW_URL_TEMPLATE = (
    "https://espressif.github.io/arduino-esp32/hosted/esp32c6-v{ver}.bin"
)
_MIN_PLAUSIBLE_SIZE = 512 * 1024  # anything smaller is a broken download


def _is_waveshare_build() -> bool:
    """Return True if the current PlatformIO env targets Waveshare.

    CPPDEFINES aren't fully populated when extra_scripts run (build_flags
    from the env get appended later in SCons' setup), so sniff the env
    name instead. The one env that links the blob is
    ``waveshare_p4_101``; anything else (notably ``esp32p4_pioarduino``
    for Tab5) skips.
    """
    return "waveshare" in env["PIOENV"].lower()  # type: ignore[name-defined]  # noqa: F821


def _detect_host_version() -> str:
    """Read ``ESP_HOSTED_VERSION_*`` from the Arduino libs.

    Falls back to a known-good hard-coded version if the header can't
    be found - the build can still succeed, just against a blob that
    may not match the installed host library.
    """
    fallback = "2.12.3"
    home = Path.home()
    header = (
        home
        / ".platformio"
        / "packages"
        / "framework-arduinoespressif32-libs"
        / "esp32p4"
        / "include"
        / "espressif__esp_hosted"
        / "host"
        / "esp_hosted_host_fw_ver.h"
    )
    if not header.is_file():
        print(f"[C6 FW] Warning: {header} not found, using fallback {fallback}")
        return fallback

    text = header.read_text()
    major = re.search(r"ESP_HOSTED_VERSION_MAJOR_1\s+(\d+)", text)
    minor = re.search(r"ESP_HOSTED_VERSION_MINOR_1\s+(\d+)", text)
    patch = re.search(r"ESP_HOSTED_VERSION_PATCH_1\s+(\d+)", text)
    if not (major and minor and patch):
        print(f"[C6 FW] Warning: could not parse {header}, using fallback {fallback}")
        return fallback
    return f"{major.group(1)}.{minor.group(1)}.{patch.group(1)}"


def _download_if_missing(url: str, dest: Path) -> None:
    """Fetch ``url`` to ``dest`` unless a plausibly-complete copy exists."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_file() and dest.stat().st_size >= _MIN_PLAUSIBLE_SIZE:
        return
    # Espressif only keeps the latest slave firmware on their GitHub Pages
    # mirror, so older header-reported versions 404. If the directly
    # requested URL is gone but another esp32c6-v*.bin is sitting in the
    # same destination directory (cached from a previous build against a
    # newer arduino-esp32 libs pack), reuse that - the host side of
    # esp-hosted 2.x is backward compatible across minor/patch versions.
    print(f"[C6 FW] Downloading {url} -> {dest}")
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            data = resp.read()
    except urllib.error.URLError as exc:
        cached = sorted(dest.parent.glob("esp32c6-v*.bin"))
        cached = [p for p in cached if p.is_file() and p.stat().st_size >= _MIN_PLAUSIBLE_SIZE]
        if cached:
            src = cached[-1]
            print(
                f"[C6 FW] Upstream URL {url} unavailable ({exc}); "
                f"falling back to cached {src.name}"
            )
            dest.write_bytes(src.read_bytes())
            return
        raise RuntimeError(
            f"Failed to download ESP32-C6 firmware from {url}: {exc}. "
            "Check internet connectivity, or pre-populate the file manually."
        ) from exc
    if len(data) < _MIN_PLAUSIBLE_SIZE:
        raise RuntimeError(
            f"Downloaded ESP32-C6 firmware is suspiciously small ({len(data)} bytes); "
            f"refusing to cache. URL: {url}"
        )
    dest.write_bytes(data)
    print(f"[C6 FW] Downloaded {len(data)} bytes")


def _write_asm(asm_path: Path, bin_name: str, version: str) -> None:
    """Generate ``c6_firmware_blob.S`` if its contents would change."""
    asm = (
        "/*\n"
        " * c6_firmware_blob.S - AUTO-GENERATED by scripts/fetch_c6_firmware.py.\n"
        " * DO NOT EDIT BY HAND.\n"
        " *\n"
        f" * Embeds esp-hosted slave firmware v{version} for ESP32-C6 into\n"
        " * .rodata so BoardWifi_ApplyCoprocessorFirmware() can stream it\n"
        " * into the co-processor via esp_hosted_slave_ota_write() with no\n"
        " * network. Symbols consumed by src/board/waveshare/\n"
        " * board_wifi_firmware_waveshare.cpp.\n"
        " */\n"
        "\n"
        "    .section .rodata\n"
        "    .align 4\n"
        "    .global c6_firmware_bin\n"
        "    .global c6_firmware_bin_end\n"
        "    .global c6_firmware_version_str\n"
        "c6_firmware_bin:\n"
        f'    .incbin "{bin_name}"\n'
        "c6_firmware_bin_end:\n"
        "    .align 4\n"
        "c6_firmware_version_str:\n"
        f'    .asciz "{version}"\n'
    )
    if asm_path.is_file() and asm_path.read_text() == asm:
        return
    asm_path.parent.mkdir(parents=True, exist_ok=True)
    asm_path.write_text(asm)
    print(f"[C6 FW] Wrote {asm_path}")


def _run() -> None:
    if not _is_waveshare_build():
        return

    project_dir = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]  # noqa: F821
    version = _detect_host_version()
    bin_name = f"esp32c6-v{version}.bin"
    gen_dir = project_dir / "src" / "generated"
    bin_path = gen_dir / bin_name
    asm_path = gen_dir / "c6_firmware_blob.S"

    _download_if_missing(_FW_URL_TEMPLATE.format(ver=version), bin_path)
    _write_asm(asm_path, bin_name, version)

    # .incbin resolves paths relative to the assembler's include search
    # path. Add src/generated so the bare basename in the .S file works.
    env.Append(ASFLAGS=["-I", str(gen_dir)])  # type: ignore[name-defined]  # noqa: F821


_run()
