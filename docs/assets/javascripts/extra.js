// Mermaid, wired to follow the theme's colour mode.
//
// Driven by hand rather than through `startOnLoad`, because a diagram has to survive being
// re-themed: mermaid replaces the element's contents with the finished SVG, so once it has run the
// definition is gone and there is nothing left to render differently. Rendering explicitly gives a
// point in time before that happens, where the source is still on the page and can be stashed.
//
// This first call is at the top level, not inside the `load` handler below, and that placement is
// load-bearing. Mermaid registers its own `load` listener while its script is parsed -- which is
// before this file runs, and so before the listener below -- and that listener renders every
// diagram unless `startOnLoad` is already false by the time it fires. Left until `load`, the stash
// would capture mermaid's finished SVG rather than the source, and feeding that back in fails with
// "Syntax error in text". mkdocs.yml lists this file after the mermaid CDN script, so `mermaid` is
// defined here.
if (typeof mermaid !== "undefined") {
  mermaid.initialize({ startOnLoad: false });
}

window.addEventListener("load", () => {
  if (typeof mermaid === "undefined") {
    return;
  }

  const diagrams = Array.from(document.querySelectorAll(".mermaid"));
  if (diagrams.length === 0) {
    return;
  }

  // innerHTML rather than textContent, for the same reason `fence_div_format` is configured in
  // mkdocs.yml: innerHTML is what mermaid itself reads. The stashed text still carries entities
  // (`--&gt;`), which is exactly the form mermaid decoded the first time.
  diagrams.forEach((el) => {
    el.dataset.mermaidSource = el.innerHTML;
  });

  const render = () => {
    const dark = document.documentElement.getAttribute("data-bs-theme") === "dark";
    mermaid.initialize({
      startOnLoad: false,
      theme: dark ? "dark" : "neutral",
      securityLevel: "loose",
    });
    // Put the source back and clear mermaid's own done-marker, or `run()` skips these nodes.
    diagrams.forEach((el) => {
      el.innerHTML = el.dataset.mermaidSource;
      el.removeAttribute("data-processed");
    });
    mermaid.run({ nodes: diagrams });
  };

  render();

  // The colour-mode toggle raises no event of its own -- the theme's darkmode.js just sets
  // `data-bs-theme` on <html> -- so that attribute is the signal. The callback only touches the
  // diagram elements, so it cannot retrigger itself.
  new MutationObserver(render).observe(document.documentElement, {
    attributeFilter: ["data-bs-theme"],
  });
});
