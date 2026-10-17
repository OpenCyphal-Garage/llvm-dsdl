# Documentation Index

Two machine-readable views of this manual are published at the site root.

| File | Contents |
|---|---|
| [`/llms.txt`](../llms.txt) | A link index: every page, grouped by section, with an absolute URL and a one-line summary. Small enough to load before deciding what to fetch. |
| [`/llms-full.txt`](../llms-full.txt) | The manual concatenated as one Markdown document, each page preceded by its source path and URL. One fetch, no crawling. |

Both follow the [llms.txt](https://llmstxt.org/) convention.

## Generation

`tools/docs/build_llms_index.py` reads the navigation in `mkdocs.yml`, resolves each entry to its
published URL, and takes the summary from the page's own first paragraph, so neither file can
describe a page that does not exist or miss one that does. The `docs-generate` target runs it.

## Guarantees on the content

- **No timestamps.** Regenerating from an unchanged tree produces byte-identical output, which is the
  same property the compiler itself is held to.
- **Absolute URLs**, rooted at the `site_url` in `mkdocs.yml`.
- **Coverage is enforced.** A page under `docs/` that appears in neither the navigation nor the
  generator's exclusion list fails the build. The index cannot silently fall behind the manual.
