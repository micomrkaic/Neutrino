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
  if (!w.__fsWrites.length || w.__fsWrites[0][0] !== "/_editor.nu") fail("editor did not write /_editor.nu");
  if (!w.__evals.length || !w.__evals[0].includes("_editor.nu")) fail("editor run did not eval");
  if (!d.getElementById("term").textContent.includes('load("/_editor.nu")')) fail("terminal did not echo");
  if (!d.getElementById("term").textContent.includes("script ran")) fail("no completion feedback");
  ed.focus();
  ed.dispatchEvent(new w.MouseEvent("click", { bubbles: true }));
  if (d.activeElement !== ed) fail("editor lost focus to the terminal");
  const input = d.querySelector(".inputline input");
  input.value = "manual packages";
  input.dispatchEvent(new w.KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
  if (d.getElementById("docs").style.display !== "flex") fail("manual did not open Docs tab");
  console.log("page: editor run, terminal echo, focus, and manual interception OK");
}, 80);
