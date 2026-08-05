// Connector's client half.
//
// The whole job is: hold a bounded window of records, decide which of them the
// current filters admit, and keep the DOM in step without ever doing O(all
// records) work on a frame. A debug view that stutters under load is a debug
// view you stop opening precisely when the game is busy, which is when you
// needed it.
//
// Three decisions that follow from that:
//
//  * Records live in a ring, not the DOM. The DOM holds only what is visible-ish
//    (MAX_ROWS), and rows are recycled off the top.
//  * Incoming records are buffered and flushed on requestAnimationFrame, so a
//    burst of two thousand in one frame costs one layout, not two thousand.
//  * Re-filtering rebuilds from the ring rather than mutating rows in place --
//    it is rarer than arrival and much simpler to get right.

const MAX_RECORDS = 200000; // the ring; ~40 MB of strings at typical line length
const MAX_ROWS = 4000;      // DOM rows retained; well past a screenful

const state = {
  records: [],        // ring buffer of parsed records
  head: 0,            // next write index once the ring is full
  full: false,
  channels: new Map(),// name -> { on, count }
  levels: new Set(["info", "warn", "error"]),
  filter: null,       // {kind:'text'|'regex', value}
  follow: true,
  paused: false,
  frameFocus: null,
  pending: [],        // arrived since the last frame
  shown: 0,
  received: 0,
  rateWindow: [],
};

// Watches: name -> {value, history[], dirty}. Kept out of the log entirely.
const watches = new Map();
const WATCH_HISTORY = 90; // ~3 s at 30 Hz of arrivals; enough for a shape

const el = {
  log: document.getElementById("log"),
  channels: document.getElementById("channels"),
  filter: document.getElementById("filter"),
  levels: document.getElementById("levels"),
  follow: document.getElementById("follow"),
  pause: document.getElementById("pause"),
  clear: document.getElementById("clear"),
  wrap: document.getElementById("wrap"),
  conn: document.getElementById("conn"),
  count: document.getElementById("count"),
  shownCount: document.getElementById("shown"),
  rate: document.getElementById("rate"),
  pulse: document.getElementById("pulse"),
  empty: document.getElementById("empty"),
  watches: document.getElementById("watches"),
  frameBar: document.getElementById("frameBar"),
  frameNo: document.getElementById("frameNo"),
  frameClear: document.getElementById("frameClear"),
  export: document.getElementById("export"),
  memory: document.getElementById("memory"),
  memBody: document.getElementById("memBody"),
  memTabs: document.getElementById("memTabs"),
  memTotal: document.getElementById("memTotal"),
  views: document.getElementById("views"),
  memoryTab: document.getElementById("memoryTab"),
  main: document.querySelector(".main"),
  treemap: document.getElementById("treemap"),
  memChart: document.getElementById("memChart"),
};

// A debug tool that fails silently is worse than no debug tool: you read an
// empty pane and conclude the engine is quiet. Anything thrown gets said out
// loud, in the status bar, where the connection state already lives.
function fail(what, error) {
  const message = `${what}: ${error && error.message ? error.message : error}`;
  const bar = document.getElementById("conn");
  if (bar) {
    bar.textContent = message;
    bar.classList.remove("live");
    bar.style.color = "var(--red)";
  }
  console.error("[connector]", what, error);
}
window.addEventListener("error", (e) => fail("script error", e.error || e.message));
window.addEventListener("unhandledrejection", (e) => fail("unhandled", e.reason));

// --- ring ------------------------------------------------------------------
function push(record) {
  if (state.records.length < MAX_RECORDS) {
    state.records.push(record);
  } else {
    state.records[state.head] = record;
    state.head = (state.head + 1) % MAX_RECORDS;
    state.full = true;
  }
}

function* inOrder() {
  if (!state.full) {
    yield* state.records;
    return;
  }
  for (let i = 0; i < MAX_RECORDS; i++)
    yield state.records[(state.head + i) % MAX_RECORDS];
}

// --- filtering -------------------------------------------------------------
function parseFilter(text) {
  const trimmed = text.trim();
  if (!trimmed) return null;
  // /.../ is a regex; anything else is a case-insensitive substring. Typing a
  // regex by accident is far more annoying than having to add two slashes.
  const match = /^\/(.*)\/([a-z]*)$/.exec(trimmed);
  if (match) {
    try {
      return { kind: "regex", value: new RegExp(match[1], match[2] || "i") };
    } catch {
      return { kind: "text", value: trimmed.toLowerCase() };
    }
  }
  return { kind: "text", value: trimmed.toLowerCase() };
}

function admits(record) {
  // A watch is a value, not a line: it belongs in the panel and must never
  // scroll, whatever the filters say.
  if (record.kind === "watch" || record.kind === "sample") return false;
  if (state.frameFocus !== null && record.frame !== state.frameFocus) return false;
  // An event is a marker and always shows: it is what you navigate BY.
  if (record.kind === "event") return true;
  if (!state.levels.has(record.lvl)) return false;
  const channel = state.channels.get(record.ch);
  if (channel && !channel.on) return false;
  const f = state.filter;
  if (!f) return true;
  if (f.kind === "regex") return f.value.test(record.msg) || f.value.test(record.ch);
  return (
    record.msg.toLowerCase().includes(f.value) ||
    record.ch.toLowerCase().includes(f.value)
  );
}

// --- rendering -------------------------------------------------------------
function highlight(text) {
  const f = state.filter;
  if (!f) return document.createTextNode(text);
  const fragment = document.createDocumentFragment();
  let pattern;
  if (f.kind === "regex") {
    pattern = new RegExp(f.value.source, f.value.flags.includes("g")
      ? f.value.flags : f.value.flags + "g");
  } else {
    pattern = new RegExp(f.value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
  }
  let last = 0;
  for (const m of text.matchAll(pattern)) {
    if (m.index === undefined) break;
    if (m.index > last) fragment.append(text.slice(last, m.index));
    const mark = document.createElement("mark");
    mark.textContent = m[0];
    fragment.append(mark);
    last = m.index + m[0].length;
    if (m[0].length === 0) break; // a zero-width match would spin forever
  }
  fragment.append(text.slice(last));
  return fragment;
}

function rowFor(record) {
  const row = document.createElement("div");
  row.className = "row " + record.lvl + (record.kind === "event" ? " event" : "");
  const t = document.createElement("span");
  t.className = "t";
  t.textContent = (record.t / 1000).toFixed(2);
  const frame = document.createElement("span");
  frame.className = "frame";
  frame.textContent = record.frame;
  frame.title = "show only this frame";
  frame.onclick = () => focusFrame(record.frame);
  const ch = document.createElement("span");
  ch.className = "ch";
  ch.textContent = record.ch;
  const msg = document.createElement("span");
  msg.className = "msg";
  // A numeric sample reads as "name = value"; a text record is just the text.
  msg.append(highlight(
    record.v === undefined ? record.msg : `${record.msg} = ${record.v}`));
  row.append(t, frame, ch, msg);
  return row;
}

function appendRows(records) {
  const atBottom =
    el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 40;
  const fragment = document.createDocumentFragment();
  let added = 0;
  for (const record of records) {
    if (!admits(record)) continue;
    fragment.append(rowFor(record));
    added++;
  }
  if (!added) return;
  el.log.append(fragment);
  state.shown += added;

  // Recycle from the top. removeChild in a loop is faster here than any
  // clever alternative because the count is small and amortised.
  let excess = el.log.childElementCount - MAX_ROWS;
  while (excess-- > 0 && el.log.firstChild) el.log.removeChild(el.log.firstChild);

  if (state.follow && atBottom) el.log.scrollTop = el.log.scrollHeight;
  el.empty.style.display = "none";
}

function rebuild() {
  el.log.replaceChildren();
  state.shown = 0;
  const admitted = [];
  for (const record of inOrder()) if (admits(record)) admitted.push(record);
  // Only the tail can be on screen; building 200k rows to show 40 would hang
  // the tab for seconds.
  appendRows(admitted.slice(-MAX_ROWS));
  el.empty.style.display = el.log.childElementCount ? "none" : "";
  updateStatus();
}

// --- channels --------------------------------------------------------------
function noteWatch(record) {
  const key = `${record.ch}.${record.name ?? record.msg}`;
  let entry = watches.get(key);
  if (!entry) {
    entry = { history: [], value: "", numeric: false, channel: record.ch };
    watches.set(key, entry);
  }
  if (record.v !== undefined) {
    entry.numeric = true;
    entry.value = Math.abs(record.v) >= 1000
      ? record.v.toFixed(0)
      : record.v.toFixed(2);
    entry.history.push(record.v);
    if (entry.history.length > WATCH_HISTORY) entry.history.shift();
  } else {
    entry.value = record.msg;
  }
  entry.dirty = true;
}

// Redrawn on the animation frame like everything else, and only for entries
// that actually changed -- a watch panel that relayouts sixty times a second
// costs more than the game loop it is reporting on.
function flushWatches() {
  if (!watches.size) return;
  for (const [key, entry] of watches) {
    if (!entry.dirty) continue;
    entry.dirty = false;
    let node = entry.node;
    if (!node) {
      if (el.watches.querySelector(".empty")) el.watches.replaceChildren();
      node = document.createElement("div");
      node.className = "watch";
      const name = document.createElement("span");
      name.className = "wname";
      name.textContent = key;
      const value = document.createElement("span");
      value.className = "wval";
      const spark = document.createElement("canvas");
      spark.width = 136; spark.height = 32; // 2x for crispness on HiDPI
      node.append(name, spark, value);
      entry.node = node;
      entry.valueNode = value;
      entry.spark = spark;
      // Sorted insert, so the panel does not reshuffle as values arrive.
      const siblings = [...el.watches.children];
      const before = siblings.find((n) => n.firstChild.textContent > key);
      el.watches.insertBefore(node, before ?? null);
    }
    entry.valueNode.textContent = entry.value;
    // A channel switched off in the rail hides its watches as well as its
    // lines; otherwise the toggle does nothing visible for a watch-only
    // channel, which is exactly the case that made the rail confusing.
    const channel = state.channels.get(entry.channel);
    node.hidden = channel ? !channel.on : false;
    if (entry.numeric) drawSpark(entry);
  }
}

function drawSpark(entry) {
  const ctx = entry.spark.getContext("2d");
  const w = entry.spark.width, h = entry.spark.height;
  ctx.clearRect(0, 0, w, h);
  const points = entry.history;
  if (points.length < 2) return;
  let lo = Infinity, hi = -Infinity;
  for (const v of points) { if (v < lo) lo = v; if (v > hi) hi = v; }
  const span = hi - lo || 1;
  ctx.beginPath();
  points.forEach((v, i) => {
    const x = (i / (points.length - 1)) * w;
    const y = h - ((v - lo) / span) * (h - 3) - 1.5;
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  });
  ctx.strokeStyle = "#d03a3a";
  ctx.lineWidth = 2;
  ctx.stroke();
}

// --- memory ----------------------------------------------------------------
//
// The heap gets its own view rather than eleven more rows in the watch panel,
// because the question it answers is not "what is this number" but "what is
// using the memory" -- and that is a question about proportion. A treemap
// answers it in one glance: area is bytes, so the big square IS the problem,
// and you do not have to read and compare fourteen figures to find it.
//
// The engine sends three sets of names on the "mem" channel, and they are three
// different treemaps of the same heap:
//
//   phase.*  live bytes attributed to the frame phase that allocated them.
//            Exact -- every allocation is counted -- and the one to look at
//            when memory is growing.
//   churn.*  bytes allocated during the last frame, by phase. Exact. This is
//            the one to look at when the game hitches: a steady-state frame
//            should allocate approximately nothing.
//   site.*   live bytes by call stack, scaled up from a 1-in-N sample, so an
//            estimate. This is the "which 20% of the code" view.
const memory = {
  present: false,
  phase: new Map(),  // name -> {v, seen}
  churn: new Map(),
  site: new Map(),
  load: new Map(),
  size: new Map(),
  series: new Map(), // name -> [values]
  view: "phase",   // which treemap: phase | churn | site
  pane: "log",     // which of the two main views is on screen
  tiles: [],
  hover: null,
  dirty: false,
  // Why a pane is blank, which is not the same thing as being blank.
  empty: {
    phase: "no heap activity reported — is the build ENG_MEMPROF=ON?",
    churn: "nothing allocated last frame (which is the goal)",
    site: "no sampled call stacks — try  mem.sample 32  in the console",
    load: "no profiler scopes reported yet",
    size: "no symbol table: a stripped binary, or not ELF",
  },
};
// Five treemaps of three different budgets, and the table is what keeps them
// one view instead of three: each says what its numbers mean, how to format
// them, and whether they go stale. Adding a sixth is a row here and a
// telemetry::watchValue in the engine -- no new drawing code.
const VIEWS = {
  phase: { label: "live heap", unit: "bytes",
           note: "exact; every allocation counted" },
  churn: { label: "allocated last frame", unit: "bytes",
           note: "exact; a steady frame should be near nothing" },
  site:  { label: "live heap", unit: "bytes", stale: 5000,
           note: "estimated from a 1-in-N call-stack sample" },
  load:  { label: "cpu self time", unit: "ms",
           note: "self ms, so the parts sum to the frame" },
  size:  { label: "machine code", unit: "bytes", static: true,
           note: "read from this binary's own symbol table" },
};

const MEM_SERIES = 240;  // ~4 s of frames; the shape of a spike, not a history
const MEM_STALE_MS = 5000; // a site that stopped reporting stops being drawn
const PHI = (1 + Math.sqrt(5)) / 2;

// Returns true when the record belongs to the memory view and must not also
// land in the generic watch list.
function noteMemory(record) {
  if (record.ch !== "mem") return false;
  memory.present = true;
  const name = record.name ?? record.msg;

  if (record.kind === "sample") {
    let series = memory.series.get(name);
    if (!series) memory.series.set(name, (series = []));
    series.push(record.v ?? 0);
    if (series.length > MEM_SERIES) series.shift();
    memory.dirty = true;
    return true;
  }

  const dot = name.indexOf(".");
  const group = dot < 0 ? "" : name.slice(0, dot);
  const bucket = VIEWS[group] ? memory[group] : null;
  if (!bucket) return false; // live_blocks, peak_mb -- ordinary watches
  bucket.set(name.slice(dot + 1), { v: record.v ?? 0, seen: Date.now() });
  memory.dirty = true;
  return true;
}

function humanBytes(bytes) {
  const units = ["B", "KB", "MB", "GB"];
  let value = bytes, i = 0;
  while (value >= 1024 && i < units.length - 1) { value /= 1024; i++; }
  return `${value < 10 && i > 0 ? value.toFixed(1) : Math.round(value)} ${units[i]}`;
}

// A name always gets the same colour, so the tile you were watching is still
// the same colour after the layout reshuffles around it.
function humanMs(ms) {
  if (ms >= 10) return `${ms.toFixed(1)} ms`;
  if (ms >= 0.1) return `${ms.toFixed(2)} ms`;
  return `${(ms * 1000).toFixed(0)} us`;
}

function humanValue(value) {
  return VIEWS[memory.view].unit === "ms" ? humanMs(value) : humanBytes(value);
}

// One hue, and the value carries the shade: the bigger the tile, the darker the
// red. Area already encodes the quantity, so colouring by a hash of the NAME
// spent the strongest visual channel on an arbitrary label -- and it made two
// adjacent tiles look categorically different when the only thing that differed
// was their spelling.
//
// Doubling up this way, shade agrees with size. A big dark block reads as heavy
// at a glance even off in a corner, and the ordering survives being squinted
// at, printed, or seen by anyone colourblind: lightness is the one channel that
// always works.
//
// The ramp is the tile's SHARE of the largest tile, pulled through a power
// curve. Two shapes were tried before this one and both read as flat:
//
//   linear share    one tile usually dominates, so it is dark and every other
//                   tile collapses into the same pale wash.
//   ratio of logs   log(v)/log(max) compresses hard once the numbers are big:
//                   38 KB against 512 KB comes out at 0.80, so the whole map
//                   landed inside 8% of lightness.
//
// x^0.4 keeps the top end dark, lifts the small tiles clear of it, and still
// orders everything in between.
const TILE_DARK = 17;    // % lightness for the largest tile
const TILE_LIGHT = 62;   // ...and for the smallest
const TILE_CURVE = 0.4;

function tileLightness(value, max) {
  if (!(max > 0) || !(value > 0)) return TILE_LIGHT;
  const t = Math.pow(Math.min(value / max, 1), TILE_CURVE);
  return TILE_LIGHT - t * (TILE_LIGHT - TILE_DARK);
}

// How far a row of tiles is from the target aspect ratio, worst tile first.
// The classic squarified treemap (Bruls, Huizing & van Wijk) drives this toward
// 1.0 -- literal squares. Aiming at the golden ratio instead gives tiles that
// are recognisably rectangles with a consistent shape, which reads better at a
// glance than a field of near-squares where only area distinguishes them.
function rowBadness(areas, side, sum) {
  if (sum <= 0 || side <= 0) return Infinity;
  const thickness = sum / side;
  let worst = 1;
  for (const area of areas) {
    const length = area / thickness;
    if (length <= 0) return Infinity;
    const aspect = Math.max(length / thickness, thickness / length);
    const badness = aspect >= PHI ? aspect / PHI : PHI / aspect;
    if (badness > worst) worst = badness;
  }
  return worst;
}

// Squarify: fill the rectangle with rows laid along its shorter side, growing
// each row while doing so improves the worst tile in it and stopping the moment
// it does not. Items must arrive largest-first, which is what puts the big
// square in the top-left corner where the eye starts.
function squarify(items, rect) {
  const total = items.reduce((sum, item) => sum + item.value, 0);
  if (total <= 0 || rect.w <= 0 || rect.h <= 0) return [];
  const scale = (rect.w * rect.h) / total;
  const tiles = [];
  let box = { ...rect };
  let i = 0;

  while (i < items.length && box.w > 0.5 && box.h > 0.5) {
    const vertical = box.w >= box.h; // rows are vertical strips
    const side = vertical ? box.h : box.w;
    const areas = [];
    let sum = 0;
    let best = Infinity;
    let j = i;

    while (j < items.length) {
      const area = items[j].value * scale;
      if (area <= 0) { j++; continue; }
      const badness = rowBadness([...areas, area], side, sum + area);
      if (areas.length && badness > best) break;
      areas.push(area);
      sum += area;
      best = badness;
      j++;
    }
    if (!areas.length) break;

    const thickness = sum / side;
    let offset = 0;
    for (let k = 0; k < areas.length; k++) {
      const length = areas[k] / thickness;
      tiles.push({
        name: items[i + k].name,
        value: items[i + k].value,
        x: vertical ? box.x : box.x + offset,
        y: vertical ? box.y + offset : box.y,
        w: vertical ? thickness : length,
        h: vertical ? length : thickness,
      });
      offset += length;
    }
    if (vertical) { box.x += thickness; box.w -= thickness; }
    else { box.y += thickness; box.h -= thickness; }
    i += areas.length;
  }
  return tiles;
}

function memItems() {
  const bucket = memory[memory.view];
  const stale = VIEWS[memory.view].stale;
  const now = Date.now();
  const items = [];
  for (const [name, entry] of bucket) {
    if (entry.v <= 0) continue;
    // Sampled sites are republished once a second; anything that has gone quiet
    // for five is either freed or no longer running, and drawing it forever
    // would make the map a record of the past rather than a picture of now.
    // The exact views never go stale -- they are resent every frame.
    if (stale && now - entry.seen > stale) continue;
    items.push({ name, value: entry.v });
  }
  items.sort((a, b) => b.value - a.value);
  return items;
}

function fitCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const w = Math.max(1, Math.round(rect.width * dpr));
  const h = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w: rect.width, h: rect.height };
}

function drawTreemap() {
  const { ctx, w, h } = fitCanvas(el.treemap);
  ctx.clearRect(0, 0, w, h);
  const items = memItems();
  const total = items.reduce((sum, item) => sum + item.value, 0);

  const view = VIEWS[memory.view];
  el.memTotal.textContent = items.length
    ? `${view.label}: ${humanValue(total)} across ${items.length} — ${view.note}`
    : `${view.label}: nothing reported yet`;
  if (!items.length) {
    // A blank rectangle is indistinguishable from a broken one. Say which of
    // the two reasons this is: the engine has not sent this group, or it sent
    // it and the values have gone stale.
    memory.tiles = [];
    ctx.font = "12px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.textAlign = "center";
    ctx.fillStyle = "#6a6a70";
    ctx.fillText(memory.empty[memory.view] ?? "waiting for the engine…",
                 w / 2, h / 2 - 8);
    ctx.textAlign = "left";
    return;
  }

  memory.tiles = squarify(items, { x: 0, y: 0, w, h });
  ctx.font = "600 11px ui-monospace, SFMono-Regular, Menlo, monospace";
  ctx.textBaseline = "top";

  const max = items[0].value;   // items are sorted largest-first
  for (const tile of memory.tiles) {
    const hot = memory.hover === tile.name;
    const light = tileLightness(tile.value, max);
    // Hover brightens rather than shifting hue, so the shade still reads as the
    // size while the tile is highlighted.
    // Saturation rides the ramp too: held constant, the pale end goes pink and
    // the dark end goes muddy brown. Deepening it as the tile darkens keeps the
    // whole range reading as one red.
    const sat = 34 + (TILE_LIGHT - light) * 0.7;
    ctx.fillStyle = hot
      ? `hsl(2 ${sat + 16}% ${Math.min(light + 13, 72)}%)`
      : `hsl(2 ${sat}% ${light}%)`;
    ctx.fillRect(tile.x, tile.y, tile.w, tile.h);
    ctx.strokeStyle = "rgba(0,0,0,.45)";
    ctx.lineWidth = 1;
    ctx.strokeRect(tile.x + .5, tile.y + .5, tile.w - 1, tile.h - 1);

    // A label that does not fit is worse than no label: it overflows into the
    // neighbouring tile and reads as belonging to it. The tooltip covers the
    // small ones.
    if (tile.w < 46 || tile.h < 24) continue;
    ctx.save();
    ctx.beginPath();
    ctx.rect(tile.x + 4, tile.y + 3, tile.w - 8, tile.h - 6);
    ctx.clip();
    // The shade varies across the map now, so the label has to pick a side:
    // white on the dark (large) tiles, near-black on the pale (small) ones.
    const dark = light < 42;
    ctx.fillStyle = dark ? "rgba(255,244,244,.96)" : "rgba(30,8,8,.92)";
    ctx.fillText(tile.name, tile.x + 5, tile.y + 4);
    if (tile.h >= 38) {
      ctx.fillStyle = dark ? "rgba(255,244,244,.68)" : "rgba(30,8,8,.68)";
      ctx.fillText(humanValue(tile.value), tile.x + 5, tile.y + 18);
    }
    ctx.restore();
  }

  if (memory.hover) {
    const tile = memory.tiles.find((t) => t.name === memory.hover);
    if (tile) {
      const share = total > 0 ? (tile.value / total) * 100 : 0;
      const label = `${tile.name}   ${humanValue(tile.value)}  (${share.toFixed(1)}%)`;
      const width = ctx.measureText(label).width + 14;
      const x = Math.min(Math.max(tile.x, 4), w - width - 4);
      const y = Math.max(tile.y - 22, 4);
      ctx.fillStyle = "rgba(12,12,14,.94)";
      ctx.fillRect(x, y, width, 19);
      ctx.strokeStyle = "rgba(255,255,255,.18)";
      ctx.strokeRect(x + .5, y + .5, width - 1, 18);
      ctx.fillStyle = "#e8e8ea";
      ctx.fillText(label, x + 7, y + 4);
    }
  }
}

// Allocation over time. The treemap says what is using memory; this says
// whether it is settling or climbing, which the treemap cannot show at all.
function drawMemChart() {
  const { ctx, w, h } = fitCanvas(el.memChart);
  ctx.clearRect(0, 0, w, h);
  const churn = memory.series.get("alloc_kb") ?? [];
  const live = memory.series.get("live_mb") ?? [];
  if (churn.length < 2 && live.length < 2) return;

  const pad = 2;
  const plot = (points, color, fill, scaleMax) => {
    if (points.length < 2) return 0;
    const max = scaleMax ?? Math.max(...points, 1e-6);
    ctx.beginPath();
    points.forEach((v, i) => {
      const x = (i / (points.length - 1)) * w;
      const y = h - pad - (v / max) * (h - pad * 2);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    if (fill) {
      ctx.lineTo(w, h);
      ctx.lineTo(0, h);
      ctx.closePath();
      ctx.fillStyle = fill;
      ctx.fill();
    }
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5;
    ctx.stroke();
    return max;
  };

  // Each series is scaled to its own peak: they are kilobytes and megabytes of
  // two different things, and a shared axis would flatten one of them to a
  // straight line at zero. The legend carries the scale so the shapes are not
  // read as comparable heights.
  const churnMax = plot(churn, "#d8813a", "rgba(216,129,58,.16)");
  const liveMax = plot(live, "#4f9de0", null);

  ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
  ctx.textBaseline = "top";
  ctx.fillStyle = "#d8813a";
  ctx.fillText(`allocated/frame  peak ${churnMax.toFixed(0)} KB`, 6, 4);
  ctx.fillStyle = "#4f9de0";
  ctx.fillText(`live  peak ${liveMax.toFixed(1)} MB`, 6, 17);
}

// The tab appears the moment the engine says anything about memory, and not
// before: a build with the profiler compiled out should look exactly as it did.
function showPane(pane) {
  memory.pane = pane;
  el.main.classList.toggle("showing-memory", pane === "memory");
  for (const button of el.views.children)
    button.classList.toggle("on", button.dataset.pane === pane);
  // The canvases had no box while hidden, so they have to be laid out again
  // before they are worth drawing into.
  if (pane === "memory") { memory.dirty = true; flushMemory(); }
}

function flushMemory() {
  if (!memory.present) return;
  if (el.memoryTab.hidden) el.memoryTab.hidden = false;
  // Code size has no time axis -- it is the same number for the whole run --
  // so the chart gives the treemap its space back instead of drawing a
  // flat line and pretending otherwise.
  el.memBody.classList.toggle("nochart", !!VIEWS[memory.view].static);
  // Drawing a treemap nobody is looking at is pure cost, and the canvas has no
  // size while the pane is hidden, so it would be wrong as well as wasteful.
  if (!memory.dirty || memory.pane !== "memory") return;
  memory.dirty = false;
  drawTreemap();
  drawMemChart();
}

// Counted separately, because they are not the same thing to a reader. A
// channel that only ever emits watches ("frame", "render") has no lines to
// show, and a rail that advertised "540" next to an empty pane was the UI
// lying about what a click would do.
function noteChannel(name, record) {
  let entry = state.channels.get(name);
  if (!entry) {
    entry = { on: true, lines: 0, watches: 0 };
    state.channels.set(name, entry);
    renderChannels();
  }
  const isValue = record.kind === "watch" || record.kind === "sample";
  if (isValue) {
    // Distinct watches, not arrivals: "render has 2 values" is the useful
    // number, where "render has sent 540 updates" is just uptime.
    entry.watches = new Set([
      ...(entry.names ?? []),
      record.name ?? record.msg,
    ]).size;
    entry.names = [...(entry.names ?? []), record.name ?? record.msg];
    if (entry.names.length > 64) entry.names = [...new Set(entry.names)];
  } else {
    entry.lines++;
  }
  entry.dirty = true;
}

let channelsDirty = false;
function renderChannels() {
  channelsDirty = true;
}
function flushChannels() {
  if (!channelsDirty) return;
  channelsDirty = false;
  const names = [...state.channels.keys()].sort();
  if (!names.length) return;
  el.channels.replaceChildren();
  for (const name of names) {
    const entry = state.channels.get(name);
    const row = document.createElement("div");
    row.className = "channel " + (entry.on ? "on" : "off");
    const label = document.createElement("span");
    label.className = "name";
    label.textContent = name;
    const count = document.createElement("span");
    count.className = "count";
    if (entry.lines > 0) {
      count.textContent = entry.lines > 9999 ? "9k+" : entry.lines;
    } else if (entry.watches > 0) {
      // A tilde, and dimmer: these are values in the panel above, not lines
      // waiting in the log. Toggling still works -- it hides the watches.
      count.textContent = "~" + entry.watches;
      count.classList.add("values");
      row.title = `${entry.watches} watched value(s); no log lines`;
    } else {
      count.textContent = "0";
    }
    row.append(label, count);
    row.onclick = (event) => {
      // Alt-click solos, because "just this one" is the common intent and
      // unticking eleven others by hand is not a workflow.
      if (event.altKey) {
        const solo = state.channels.get(name).on &&
          [...state.channels.values()].filter((c) => c.on).length === 1;
        for (const [key, value] of state.channels) value.on = solo || key === name;
      } else {
        entry.on = !entry.on;
      }
      renderChannels();
      flushChannels();
      for (const w of watches.values()) w.dirty = true; // re-evaluate visibility
      flushWatches();
      rebuild();
    };
    el.channels.append(row);
  }
}

function focusFrame(number) {
  state.frameFocus = number;
  el.frameBar.hidden = false;
  el.frameNo.textContent = number;
  rebuild();
}

// --- status ----------------------------------------------------------------
function updateStatus() {
  el.count.textContent = state.received.toLocaleString();
  el.shownCount.textContent = el.log.childElementCount.toLocaleString();
}

setInterval(() => {
  const now = performance.now();
  state.rateWindow = state.rateWindow.filter((t) => now - t < 1000);
  el.rate.textContent = `${state.rateWindow.length}/s`;
  el.pulse.classList.toggle("idle", state.rateWindow.length === 0);
}, 250);

// --- the stream ------------------------------------------------------------
function connect() {
  const source = new EventSource("/api/stream");
  source.onopen = () => {
    el.conn.textContent = "connected";
    el.conn.classList.add("live");
  };
  source.onerror = () => {
    el.conn.textContent = "reconnecting…";
    el.conn.classList.remove("live");
    // EventSource retries on its own; saying so is all this has to do.
  };
  source.onmessage = (event) => {
    let record;
    try {
      record = JSON.parse(event.data);
    } catch {
      return;
    }
    state.received++;
    state.rateWindow.push(performance.now());
    noteChannel(record.ch, record);
    if (record.kind === "watch" || record.kind === "sample") {
      // The memory view claims its own names first; whatever it does not want
      // falls through to the generic watch panel.
      if (!noteMemory(record)) noteWatch(record);
      return; // never enters the ring: it would evict real log lines
    }
    push(record);
    if (!state.paused) state.pending.push(record);
  };
}

function frame() {
  flushChannels();
  flushWatches();
  flushMemory();
  if (state.pending.length) {
    appendRows(state.pending);
    state.pending.length = 0;
    updateStatus();
  }
  requestAnimationFrame(frame);
}

// --- wiring ----------------------------------------------------------------
el.filter.addEventListener("input", () => {
  state.filter = parseFilter(el.filter.value);
  rebuild();
});
el.levels.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button) return;
  const level = button.dataset.level;
  if (state.levels.has(level)) state.levels.delete(level);
  else state.levels.add(level);
  button.classList.toggle("on", state.levels.has(level));
  rebuild();
});
el.follow.onclick = () => {
  state.follow = !state.follow;
  el.follow.classList.toggle("on", state.follow);
  if (state.follow) el.log.scrollTop = el.log.scrollHeight;
};
el.pause.onclick = () => {
  state.paused = !state.paused;
  el.pause.classList.toggle("on", state.paused);
  el.pause.textContent = state.paused ? "resume" : "pause";
  // Records keep arriving into the ring while paused; resuming rebuilds so
  // nothing was actually missed, which is the difference between pause and
  // disconnect.
  if (!state.paused) rebuild();
};
el.wrap.onclick = () => {
  const on = el.wrap.classList.toggle("on");
  el.log.style.whiteSpace = on ? "pre-wrap" : "nowrap";
};
el.frameClear.onclick = () => {
  state.frameFocus = null;
  el.frameBar.hidden = true;
  rebuild();
};

// --- memory view wiring ----------------------------------------------------
function selectView(view) {
  memory.view = view;
  for (const button of el.memTabs.querySelectorAll("button"))
    button.classList.toggle("on", button.dataset.view === view);
  memory.hover = null;   // the old tile name means nothing in the new view
  memory.dirty = true;
}

el.memTabs.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (button && button.dataset.view) selectView(button.dataset.view);
});
el.views.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button || button.hidden) return;
  showPane(button.dataset.pane);
});
el.treemap.addEventListener("mousemove", (event) => {
  const rect = el.treemap.getBoundingClientRect();
  const x = event.clientX - rect.left, y = event.clientY - rect.top;
  const tile = memory.tiles.find(
    (t) => x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h);
  const name = tile ? tile.name : null;
  if (name !== memory.hover) { memory.hover = name; memory.dirty = true; }
});
el.treemap.addEventListener("mouseleave", () => {
  if (memory.hover) { memory.hover = null; memory.dirty = true; }
});
// A canvas does not reflow with its box; without this the treemap keeps the
// layout it was first drawn at and gets stretched by the browser.
new ResizeObserver(() => { memory.dirty = true; }).observe(el.memBody);

// Export what is on screen, filters and all. A bug report is "here is the log
// around it", and making someone screenshot a terminal for that is why bug
// reports arrive without one.
el.export.onclick = () => {
  const lines = [...el.log.children].map((row) =>
    [...row.children].map((c) => c.textContent).join("  ")).join("\n");
  const blob = new Blob([lines], { type: "text/plain" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  const when = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
  link.download = `connector-${when}.log`;
  link.click();
  URL.revokeObjectURL(link.href);
};

el.clear.onclick = async () => {
  state.records = [];
  state.head = 0;
  state.full = false;
  state.received = 0;
  for (const entry of state.channels.values()) {
    entry.lines = 0;
    entry.watches = 0;
    entry.names = [];
  }
  watches.clear();
  el.watches.replaceChildren();
  el.log.replaceChildren();
  el.empty.style.display = "";
  renderChannels();
  updateStatus();
  try {
    await fetch("/api/clear", { method: "POST" });
  } catch { /* the server may be down; the local clear still stands */ }
};

// Scrolling up turns follow off, which is what every log viewer does and what
// everyone expects without being told.
el.log.addEventListener("scroll", () => {
  const atBottom =
    el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 40;
  if (!atBottom && state.follow) {
    state.follow = false;
    el.follow.classList.remove("on");
  }
});

document.addEventListener("keydown", (event) => {
  if (event.target.tagName === "INPUT") {
    if (event.key === "Escape") { el.filter.value = ""; state.filter = null; rebuild(); }
    return;
  }
  if (event.key === "/") { event.preventDefault(); el.filter.focus(); }
  if (event.key === " ") { event.preventDefault(); el.pause.click(); }
  if (event.key === "f") el.follow.click();
  // m toggles the memory pane; 1-5 pick a view while it is up. Same shape as
  // the log's own shortcuts, and it beats aiming at a small button when you are
  // watching a number move.
  if (event.key === "m" && !el.memoryTab.hidden)
    showPane(memory.pane === "memory" ? "log" : "memory");
  if (memory.pane === "memory" && event.key >= "1" && event.key <= "5") {
    const order = Object.keys(VIEWS);
    const pick = order[Number(event.key) - 1];
    if (pick) selectView(pick);
  }
});

// Scrollback first, so opening Connector mid-session shows how the run got
// here rather than an empty pane.
// ?nostream=1 loads the scrollback and stops there, with no live connection.
// Useful for reading a captured session without the tail moving under you --
// and it is what makes the UI screenshot-testable, because a page holding an
// open EventSource never reaches network-idle.
const params = new URLSearchParams(location.search);
const LIVE = params.get("nostream") !== "1";

(async function boot() {
  try {
    const response = await fetch("/api/history?limit=4000");
    const { rows } = await response.json();
    for (const row of rows) {
      try {
        const record = JSON.parse(row);
        state.received++;
        noteChannel(record.ch, record);
        if (record.kind === "watch" || record.kind === "sample") {
          if (!noteMemory(record)) noteWatch(record);
        } else push(record);
      } catch { /* a truncated row is not worth failing the boot over */ }
    }
    flushChannels();
    rebuild();
    // ?pane=memory opens straight into the treemap. Same reasoning as
    // ?nostream=1 next to it: it is what makes the view screenshot-testable,
    // and it is genuinely useful when memory is what you came to look at.
    const wanted = params.get("view");
    if (wanted && VIEWS[wanted]) selectView(wanted);
    if (params.get("pane") === "memory" && memory.present) showPane("memory");
    else flushMemory(); // reveals the tab if the engine mentioned memory
  } catch (error) {
    fail("history", error);
  }
  if (LIVE) {
    connect();
  } else {
    el.conn.textContent = "snapshot (no live stream)";
  }
  requestAnimationFrame(frame);
})();
