# Documentation Index

Two machine-readable views of this manual are published at the site root.

| File | Contents |
|---|---|
| [`/llms.txt`](../llms.txt) | A link index: every page, grouped by section, with an absolute URL and a one-line summary. Small enough to load before deciding what to fetch. |
| [`/llms-full.txt`](../llms-full.txt) | The manual concatenated as one Markdown document, each page preceded by its source path and URL. One fetch, no crawling. |

Both follow the [llms.txt](https://llmstxt.org/) convention.

## Generation

`tools/docs/build_llms_index.py` reads the navigation in `mkdocs.yml`, resolves each entry to its
published URL, and takes the summary from the page's own first paragraph. Nothing in either file is
maintained by hand, so neither can describe a page that does not exist or miss one that does.

The generator runs as part of the documentation build, after the showroom pages and the guarantee
matrices have been generated, so the compiler's own output is included rather than skipped.

## Coverage

`llms.txt` lists every page in the navigation **plus** the [showroom](../showroom/index.md) type
gallery. Those per-type pages are deliberately absent from the human navigation — two dozen entries
in a dropdown help nobody — but they are among the most useful pages here for a machine reader, so
each is listed with its own URL.

`llms-full.txt` carries the manual only. The type gallery holds the full generated code for every
type in eight language/profile variants, which is roughly four times the size of the manual; folding
it in would make one fetch expensive for every reader who wanted the prose. Fetch the type pages you
need from the URLs in `llms.txt`.

## Guarantees on the content

- **No timestamps.** Regenerating from an unchanged tree produces byte-identical output, which is the
  same property the compiler itself is held to.
- **Absolute URLs**, rooted at the `site_url` in `mkdocs.yml`.
- **Coverage is enforced.** A page under `docs/` that appears in neither the navigation nor the
  generator's exclusion list fails the build. The index cannot silently fall behind the manual.
