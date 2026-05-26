#!/usr/bin/env python3
"""
extract_manim_animations_slower.py

Purpose
-------
Extract every Manim animation cell from a Jupyter notebook, render each Scene
as its own MP4, make every video presentation-friendly by slowing it down and
adding a pause at the end, and also create one combined video.

Designed for:
- Manim Community v0.20.1
- Notebooks that use cells like:
    %%manim -qm -v WARNING SceneName

Basic usage
-----------
Put this script in the same folder as your notebook, then run:

    python extract_manim_animations_slower.py Black_Box_Courier_Manim_Presentation_Notebook_FIXED_manim_0_20_1.ipynb --out courier_manim_videos --clean

Presentation-friendly defaults
------------------------------
By default this script:
- plays every animation at 75% speed, so it feels slower and easier to explain
- adds a 3 second freeze-frame pause after every scene
- creates slowed individual videos and one combined video

You can control that:

    python extract_manim_animations_slower.py your_notebook.ipynb --speed 0.65 --pause 5

Speed examples:
- --speed 1.0  normal speed
- --speed 0.85 slightly slower
- --speed 0.75 default slower presentation mode
- --speed 0.50 half speed

Outputs
-------
The output folder will contain:

    courier_manim_videos/
        01_GamePitchScene.mp4
        02_ArchitectureScene.mp4
        03_PlayerLoopScene.mp4
        ...
        combined_all_animations.mp4
        extracted_manim_scenes.py
        render_log.txt
        ffmpeg_concat_list.txt
        _raw_videos/                 optional raw Manim renders when --keep-raw is used
        _manim_media/                Manim's internal media folder

Requirements
------------
Install Manim and FFmpeg first.

    pip install manim

Manim normally needs FFmpeg installed on your system. To test:

    manim --version
    ffmpeg -version
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class SceneBlock:
    cell_index: int
    scene_names: list[str]
    code: str


def run_command(cmd: list[str], log_file: Path | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a command safely and save output to log."""
    printable = " ".join(shlex.quote(str(x)) for x in cmd)
    print(f"\n[RUN] {printable}")

    result = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    if log_file:
        with log_file.open("a", encoding="utf-8") as f:
            f.write("\n" + "=" * 100 + "\n")
            f.write(printable + "\n")
            f.write("=" * 100 + "\n")
            f.write(result.stdout or "")
            f.write("\n")

    if result.returncode != 0:
        print(result.stdout)
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {printable}")

    return result


def read_notebook_cells(notebook_path: Path) -> list[str]:
    """Return notebook cell sources as strings."""
    with notebook_path.open("r", encoding="utf-8") as f:
        nb = json.load(f)

    cells: list[str] = []
    for cell in nb.get("cells", []):
        source = cell.get("source", "")
        if isinstance(source, list):
            source = "".join(source)
        cells.append(source)
    return cells


def is_manim_cell(source: str) -> bool:
    """Detect cells that start with the Jupyter %%manim magic."""
    return source.lstrip().startswith("%%manim")


def remove_manim_magic(source: str) -> tuple[str, str]:
    """
    Remove the first %%manim line.

    Returns:
        magic_line, python_code
    """
    lines = source.splitlines()
    if not lines:
        return "", ""

    magic_line = lines[0].strip()
    code = "\n".join(lines[1:]).strip() + "\n"
    return magic_line, code


def scene_names_from_magic(magic_line: str) -> list[str]:
    """
    Try to read scene names from a line such as:
        %%manim -qm -v WARNING ArchitectureScene

    This is a helper only. The final source of truth is class detection.
    """
    try:
        tokens = shlex.split(magic_line)
    except ValueError:
        tokens = magic_line.split()

    if tokens and tokens[0] == "%%manim":
        tokens = tokens[1:]

    scene_names: list[str] = []
    skip_next = False
    options_that_take_value = {"-v", "--verbosity", "-o", "--output_file", "--format", "--save_sections"}

    for tok in tokens:
        if skip_next:
            skip_next = False
            continue

        if tok in options_that_take_value:
            skip_next = True
            continue

        if tok.startswith("-"):
            continue

        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", tok):
            scene_names.append(tok)

    return scene_names


def scene_names_from_code(code: str) -> list[str]:
    """
    Detect Manim Scene classes in Python code.

    Matches:
        class MyScene(Scene):
        class MyScene(MovingCameraScene):
        class MyScene(ThreeDScene):
    """
    pattern = r"^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*Scene[^)]*)\)\s*:"
    return [name for name, _base in re.findall(pattern, code, flags=re.MULTILINE)]


def extract_scene_blocks(notebook_path: Path) -> list[SceneBlock]:
    """Extract Manim scene blocks from all %%manim cells."""
    cells = read_notebook_cells(notebook_path)
    blocks: list[SceneBlock] = []

    for index, source in enumerate(cells):
        if not is_manim_cell(source):
            continue

        magic_line, code = remove_manim_magic(source)
        detected_from_code = scene_names_from_code(code)
        detected_from_magic = scene_names_from_magic(magic_line)
        scene_names = detected_from_code or detected_from_magic

        if not scene_names:
            print(f"[WARN] Manim cell {index} has no detectable Scene class; skipping.")
            continue

        blocks.append(SceneBlock(cell_index=index, scene_names=scene_names, code=code))

    return blocks


def write_combined_scene_file(blocks: list[SceneBlock], scene_file: Path) -> list[str]:
    """
    Write all extracted Manim cells to one Python file.

    Returns scene names in notebook order, without duplicates.
    """
    ordered_scenes: list[str] = []
    seen: set[str] = set()

    chunks = [
        "# Auto-generated by extract_manim_animations_slower.py\n",
        "# This file contains all %%manim scene code extracted from the notebook.\n\n",
    ]

    for block in blocks:
        chunks.append(f"\n\n# ===== Notebook cell {block.cell_index} =====\n")
        chunks.append(block.code)
        chunks.append("\n")

        for name in block.scene_names:
            if name not in seen:
                ordered_scenes.append(name)
                seen.add(name)

    scene_file.write_text("".join(chunks), encoding="utf-8")
    return ordered_scenes


def find_latest_rendered_video(media_dir: Path, module_stem: str, scene_name: str) -> Path:
    """
    Find the MP4 Manim created for a scene.

    Manim usually writes to:
        media/videos/<module_stem>/<quality>/<scene_name>.mp4
    but this function searches recursively for safety.
    """
    candidates = list((media_dir / "videos").rglob(f"{scene_name}.mp4"))

    module_candidates = [p for p in candidates if module_stem in str(p)]
    if module_candidates:
        candidates = module_candidates

    if not candidates:
        raise FileNotFoundError(f"Could not find rendered MP4 for scene: {scene_name}")

    return max(candidates, key=lambda p: p.stat().st_mtime)


def ffmpeg_filter_for_speed_and_pause(speed: float, pause: float) -> str:
    """
    Build an FFmpeg video filter.

    speed < 1 means slower. Example:
        speed = 0.75 -> setpts=1.333333*PTS

    pause adds a final freeze-frame pause using tpad.
    """
    if speed <= 0:
        raise ValueError("--speed must be greater than 0")
    if pause < 0:
        raise ValueError("--pause cannot be negative")

    multiplier = 1.0 / speed
    filters = [f"setpts={multiplier:.8f}*PTS"]

    if pause > 0:
        filters.append(f"tpad=stop_mode=clone:stop_duration={pause:.3f}")

    filters.append("fps=30")
    filters.append("format=yuv420p")
    return ",".join(filters)


def make_presentation_video(raw_video: Path, final_video: Path, speed: float, pause: float, log_file: Path) -> None:
    """Slow down one video and add an end pause."""
    video_filter = ffmpeg_filter_for_speed_and_pause(speed, pause)

    cmd = [
        "ffmpeg",
        "-y",
        "-i",
        str(raw_video),
        "-vf",
        video_filter,
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-crf",
        "18",
        "-movflags",
        "+faststart",
        str(final_video),
    ]
    run_command(cmd, log_file=log_file)


def render_scenes(
    scene_file: Path,
    scene_names: list[str],
    out_dir: Path,
    quality: str,
    renderer: str | None,
    speed: float,
    pause: float,
    keep_raw: bool,
) -> list[Path]:
    """Render each scene, slow it down, add pause, and save the final MP4 into out_dir."""
    log_file = out_dir / "render_log.txt"
    log_file.write_text("", encoding="utf-8")

    media_dir = out_dir / "_manim_media"
    media_dir.mkdir(parents=True, exist_ok=True)

    raw_dir = out_dir / "_raw_videos"
    raw_dir.mkdir(parents=True, exist_ok=True)

    final_videos: list[Path] = []
    quality_flag = f"-q{quality}" if len(quality) == 1 else f"--quality={quality}"

    for idx, scene_name in enumerate(scene_names, start=1):
        cmd = [
            sys.executable,
            "-m",
            "manim",
            quality_flag,
            "-v",
            "WARNING",
            "--media_dir",
            str(media_dir),
        ]

        if renderer:
            cmd.extend(["--renderer", renderer])

        cmd.extend([str(scene_file), scene_name])

        print(f"\n[{idx}/{len(scene_names)}] Rendering {scene_name}...")
        run_command(cmd, log_file=log_file)

        rendered = find_latest_rendered_video(media_dir, scene_file.stem, scene_name)
        raw_target = raw_dir / f"{idx:02d}_{scene_name}_raw.mp4"
        final_target = out_dir / f"{idx:02d}_{scene_name}.mp4"

        shutil.copy2(rendered, raw_target)

        print(f"[POST] Slowing to {speed:.2f}x and adding {pause:.1f}s pause: {scene_name}")
        make_presentation_video(raw_target, final_target, speed=speed, pause=pause, log_file=log_file)

        if not keep_raw:
            raw_target.unlink(missing_ok=True)

        final_videos.append(final_target)
        print(f"[OK] Saved presentation video: {final_target}")

    if not keep_raw:
        try:
            raw_dir.rmdir()
        except OSError:
            pass

    return final_videos


def combine_with_ffmpeg(video_paths: list[Path], combined_path: Path, out_dir: Path) -> None:
    """
    Combine presentation videos into one MP4.

    The individual videos already contain the slower speed and pause, so the
    combined video automatically contains pauses between scenes.
    """
    if not video_paths:
        raise ValueError("No videos to combine.")

    concat_file = out_dir / "ffmpeg_concat_list.txt"
    with concat_file.open("w", encoding="utf-8") as f:
        for path in video_paths:
            safe_path = path.resolve().as_posix().replace("'", r"'\''")
            f.write(f"file '{safe_path}'\n")

    copy_cmd = [
        "ffmpeg",
        "-y",
        "-f",
        "concat",
        "-safe",
        "0",
        "-i",
        str(concat_file),
        "-c",
        "copy",
        str(combined_path),
    ]

    try:
        run_command(copy_cmd, log_file=out_dir / "render_log.txt")
        print(f"[OK] Combined video saved: {combined_path}")
        return
    except Exception as exc:
        print(f"[WARN] Fast concat failed. Falling back to re-encode. Reason: {exc}")

    reencode_cmd = [
        "ffmpeg",
        "-y",
        "-f",
        "concat",
        "-safe",
        "0",
        "-i",
        str(concat_file),
        "-vf",
        "fps=30,format=yuv420p",
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-crf",
        "18",
        "-movflags",
        "+faststart",
        str(combined_path),
    ]

    run_command(reencode_cmd, log_file=out_dir / "render_log.txt")
    print(f"[OK] Combined video saved: {combined_path}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Extract all %%manim animations from a Jupyter notebook, render them, "
            "slow them down, add a pause after each scene, and combine them."
        )
    )
    parser.add_argument(
        "notebook",
        type=Path,
        help="Path to the .ipynb file containing %%manim cells.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("exported_manim_animations"),
        help="Output folder for individual videos and combined video.",
    )
    parser.add_argument(
        "--quality",
        default="m",
        choices=["l", "m", "h", "p", "k"],
        help="Manim quality: l=low, m=medium, h=high, p=2K, k=4K. Default: m.",
    )
    parser.add_argument(
        "--renderer",
        default=None,
        choices=[None, "cairo", "opengl"],
        help="Optional Manim renderer. Default: let Manim choose.",
    )
    parser.add_argument(
        "--speed",
        type=float,
        default=0.75,
        help=(
            "Playback speed for every individual video. Values below 1 are slower. "
            "Default: 0.75, meaning 75%% speed."
        ),
    )
    parser.add_argument(
        "--pause",
        type=float,
        default=3.0,
        help="Freeze-frame pause, in seconds, added after every scene. Default: 3.0.",
    )
    parser.add_argument(
        "--keep-raw",
        action="store_true",
        help="Keep raw unslowed Manim renders in the _raw_videos folder.",
    )
    parser.add_argument(
        "--no-combine",
        action="store_true",
        help="Render individual videos only; do not create combined video.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete the output folder first if it already exists.",
    )

    args = parser.parse_args()

    if args.speed <= 0:
        raise ValueError("--speed must be greater than 0. Use 0.75 for a slower presentation speed.")
    if args.pause < 0:
        raise ValueError("--pause cannot be negative. Use 0 to disable the pause.")

    notebook_path = args.notebook.resolve()
    out_dir = args.out.resolve()

    if not notebook_path.exists():
        raise FileNotFoundError(f"Notebook not found: {notebook_path}")

    if notebook_path.suffix.lower() != ".ipynb":
        raise ValueError("Input must be a Jupyter notebook file ending in .ipynb")

    if args.clean and out_dir.exists():
        shutil.rmtree(out_dir)

    out_dir.mkdir(parents=True, exist_ok=True)

    blocks = extract_scene_blocks(notebook_path)
    if not blocks:
        raise RuntimeError("No %%manim animation cells were found in the notebook.")

    scene_file = out_dir / "extracted_manim_scenes.py"
    scene_names = write_combined_scene_file(blocks, scene_file)

    print("\nDetected scenes:")
    for i, name in enumerate(scene_names, start=1):
        print(f"  {i:02d}. {name}")

    print("\nPresentation timing:")
    print(f"  Speed: {args.speed:.2f}x")
    print(f"  Pause after every scene: {args.pause:.1f} seconds")

    videos = render_scenes(
        scene_file=scene_file,
        scene_names=scene_names,
        out_dir=out_dir,
        quality=args.quality,
        renderer=args.renderer,
        speed=args.speed,
        pause=args.pause,
        keep_raw=args.keep_raw,
    )

    if not args.no_combine:
        combined_path = out_dir / "combined_all_animations.mp4"
        combine_with_ffmpeg(videos, combined_path, out_dir)

    print("\nDone.")
    print(f"Output folder: {out_dir}")
    print("Individual slowed MP4s are saved in the same folder.")
    if args.keep_raw:
        print(f"Raw unslowed renders: {out_dir / '_raw_videos'}")
    if not args.no_combine:
        print(f"Combined video: {out_dir / 'combined_all_animations.mp4'}")


if __name__ == "__main__":
    main()
