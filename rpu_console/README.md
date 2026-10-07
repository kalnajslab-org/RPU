# rpu_console

Live console for the RPU debug serial output. Shows value labels that update in
real time and a scrolling view of the raw incoming lines.

## Install

From GitHub (the app lives in the `rpu_console/` subdirectory of the repo):

    pip install "git+https://github.com/kalnajslab-org/RPU.git#subdirectory=rpu_console"

    # or, to keep it isolated from other Python environments:
    pipx install "git+https://github.com/kalnajslab-org/RPU.git#subdirectory=rpu_console"

To pick a branch or tag, add `@<ref>` after `.git`, e.g. `RPU.git@main#subdirectory=rpu_console`.
To upgrade later, add `--upgrade` (pip) or run `pipx upgrade rpu-console`.

From a local clone (editable, for development):

    pip install -e rpu_console/

## Run

    rpu-console                      # or: python -m rpu_console

Data sources (toolbar):

- **Serial**: any attached USB serial port, with a selectable baud rate.
- **File**: replay a captured log (e.g. `sample_console_01.txt`) at a set line rate.
- **Synthetic**: generated data in the same format, for testing.

Settings are saved to `~/.rpu_console.json`; the path is shown in the status bar.

## Test

    pip install -e "rpu_console/[test]" && pytest rpu_console/tests
