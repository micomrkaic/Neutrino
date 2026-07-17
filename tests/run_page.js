// Headless page-wiring test: drives docs/index.html in jsdom with a
// functional module stub, verifying the editor->terminal->module pipeline
// crosses the script scope boundaries. Skips politely without jsdom.
let JSDOM;
try { JSDOM = require("jsdom").JSDOM; }
catch (e) { console.log("page: jsdom not installed, skipping"); process.exit(0); }
const fs = require("fs");
const html = fs.readFileSync("docs/index.html", "utf8");
const stub = `<script>
window.__fsWrites = []; window.__evals = [];
window.Neutrino = function () {
  return Promise.resolve({
    _nu_init: function(){}, _nu_version: function(){ return 0; },
    _malloc: function(){ return 1; }, _free: function(){},
    lengthBytesUTF8: function(s){ return s.length; },
    stringToUTF8: function(s){ window.__lastEval = s; },
    _nu_eval: function(){ window.__evals.push(window.__lastEval); return 0; },
    UTF8ToString: function(){ return "ok\\n"; },
    FS: { readdir: function(){ return []; },
          writeFile: function(n, d){ window.__fsWrites.push([n, String(d)]); },
          readFile: function(){ return new Uint8Array([35, 32, 72, 105]); },
          stat: function(){ return {mode: 0}; }, isFile: function(){ return false; } }
  });
};</script>`;
const dom = new JSDOM(html.replace(/<script src=[^>]*><\/script>/, stub),
                      { runScripts: "dangerously", pretendToBeVisual: true });
const w = dom.window, d = w.document;
setTimeout(() => {
  const fail = m => { console.error("page FAIL:", m); process.exit(1); };
  const ed = d.getElementById("edtext");
  ed.value = "let cube = fn x -> x^3\ncube(4)";
  d.getElementById("ed-run").dispatchEvent(new w.MouseEvent("click", { bubbles: true }));
  if (!w.__evals.length || !w.__evals[0].includes("cube(4)")) fail("editor buffer not evaluated directly");
  if (!d.getElementById("term").textContent.includes("% editor script")) fail("no editor marker in terminal");
  if (!d.getElementById("term").textContent.includes("ok")) fail("module output not shown in terminal");
  ed.focus();
  ed.dispatchEvent(new w.MouseEvent("click", { bubbles: true }));
  if (d.activeElement !== ed) fail("editor lost focus to the terminal");
  const input = d.querySelector(".inputline input");
  input.value = "manual packages";
  input.dispatchEvent(new w.KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
  if (d.getElementById("docs").style.display !== "flex") fail("manual did not open Docs tab");
  console.log("page: editor run, terminal echo, focus, and manual interception OK (stub)");
  // ---- phase 2: the real bundle through the real page ----
  try {
    const bundle = fs.readFileSync("docs/neutrino.js", "utf8");
    const html2 = fs.readFileSync("docs/index.html", "utf8")
      .replace(/<script src=[^>]*><\/script>/, "<script>" + bundle.replace(/<\/script>/g, "<\\/script>") + "</script>");
    const dom2 = new JSDOM(html2, { runScripts: "dangerously", pretendToBeVisual: true, url: "https://example.org/" });
    const w2 = dom2.window, d2 = w2.document;
    let tries = 0;
    const poll = setInterval(() => {
      if (!w2.NU) { if (++tries > 150) { console.error("page FAIL: module never initialized"); process.exit(1); } return; }
      clearInterval(poll);
      const ed2 = d2.getElementById("edtext");
      ed2.value = "let cube = fn x -> x^3\ncube(4)\nlet v = [1, 2, 3]\nv * 2";
      d2.getElementById("ed-run").dispatchEvent(new w2.MouseEvent("click", { bubbles: true }));
      // ans chains from the editor run into the terminal
      const input2 = d2.querySelector(".inputline input");
      input2.value = "ans + [1, 1, 1]";
      input2.dispatchEvent(new w2.KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
      setTimeout(() => {
        const t = d2.getElementById("term").textContent;
        const want = ["% editor script", "64", "[2, 4, 6]", "[3, 5, 7]"];
        const missing = want.filter(x => !t.includes(x));
        if (missing.length) { console.error("page FAIL (real wasm): missing " + missing.join(" | ")); process.exit(1); }
        console.log("page: real-bundle editor run echoes statement values in the terminal");
        process.exit(0);
      }, 300);
    }, 100);
  } catch (e) { console.log("page: real-bundle phase skipped (" + e.message + ")"); }
}, 80);
