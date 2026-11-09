// Renders frames recorded from the real app (tools/demo-capture) as HTML.
// Shared by the build (first frame in the page) and the browser (playback).

// Frames are pictures of the app, so their text is drawn with CSS
// (`::before { content: attr(data-t) }`) rather than being page text: it keeps the
// app's own colors and is described once by the element's aria-label.

// Glyphs JetBrains Mono does not have. A terminal forces them into one cell;
// fixed-width boxes do the same here so columns stay aligned.
const NARROW = /([↻▦⤡⤢⇄])/;
const ESC = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" };
const attr = (s) => s.replace(/[&<>"]/g, (c) => ESC[c]);

const run = (text, s) =>
  text.split(NARROW).filter(Boolean)
    .map((t) => `<span class="s${s}${NARROW.test(t) && t.length === 1 ? " g" : ""}" data-t="${attr(t)}"></span>`)
    .join("");

/** CSS for the recorded styles: `.tf .s3 { color; background; font-weight }`. */
export function termCss(data) {
  return data.styles
    .map(([fg, bg, bold], i) => {
      const rules = [];
      if (fg) rules.push(`color:#${fg}`);
      if (bg) rules.push(`background:#${bg}`);
      if (bold) rules.push("font-weight:700");
      return rules.length ? `.tf .s${i}{${rules.join(";")}}` : "";
    })
    .join("");
}

/** One recorded row as HTML. */
export function rowHtml(data, i) {
  return data.lines[i].map(([text, s]) => run(text, s)).join("");
}

/** A whole frame: one `<div class="r">` per terminal row. */
export function frameHtml(data, index) {
  return data.frames[index].r.map((i) => `<div class="r">${rowHtml(data, i)}</div>`).join("");
}

/** Index of the last frame whose keys/caption match, for picking stills. */
export function findFrame(data, caption) {
  let found = -1;
  data.frames.forEach((f, i) => { if (f.c === caption) found = i; });
  return found;
}
