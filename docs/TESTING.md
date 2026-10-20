# Tests

`tests/test_smoke.py` checks what can break before the compile step: the
profile (`pipeline/ico_profile.json`), the pipeline's argument and input
checks, the pinned dependencies, `ico.ini` handling, the icon fallback and
the release script. It needs no game files, no Windows and no network, and
only the Python standard library.

```
python -I -m unittest discover -s tests -v
```

It runs the same way on Windows and Linux. `.github/workflows/ci.yml` runs it
on both, lints the Python sources with ruff, and builds the installer on
Windows.

`test_update_applies_changed_profile_defaults` is an expected failure: it
documents that an update keeps the old value of a `ico.ini` key the user never
edited. Remove the `expectedFailure` decorator when that is fixed.
