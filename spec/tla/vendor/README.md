# Vendored TLA⁺ tools

`tla2tools.jar` is committed here so the formal-model check (`../check.sh`, and the
`formal-model` CI job) runs fully offline — no network fetch, no per-run download to
break or to trust.

| | |
|---|---|
| Version | TLA⁺ tools **1.8.0** |
| Source | https://github.com/tlaplus/tlaplus/releases/tag/v1.8.0 (`tla2tools.jar`) |
| SHA-256 | `8106d3f51f552e560c4b122a1fecf9ac2c9423bd1934f0bd56e2a2174d425771` |
| Size | ~3.9 MB |
| Licence | MIT (TLA⁺ tools) |

Verify:

```sh
shasum -a 256 tla2tools.jar   # must match the SHA-256 above
```

To update: download the new `tla2tools.jar` from the TLA⁺ releases page, replace this
file, and update the version + SHA-256 in this README. `check.sh` and CI pick it up
automatically (no path or version pin elsewhere).
