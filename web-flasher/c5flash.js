// c5flash.js — browser flashing for ESP32-C5 cards, driven by esptool-js 0.7.0.
//
// Why this exists: esp-web-tools 10.4.0 bundles esptool-js 0.6, which does not
// recognise newer XIAO C5 silicon (rev v1.2, chip magic 0x30e1706f) — chip
// detection throws and the <esp-web-install-button> never opens the port. The
// fix is esptool-js 0.7.0, which no esp-web-tools release ships yet, so we drive
// esptool-js 0.7.0 ourselves for the C5 cards only. Every other board keeps using
// esp-web-tools. Loaded via a dynamic import so a CDN hiccup shows a message
// instead of breaking the page.
//
// A card opts in with [data-c5flash] on a button inside a .board. The image URL
// is the card's active fork/official bin (board.dataset.binFork / binHam, chosen
// by board.dataset.fwActive) or, if the card has no fork toggle, the button's
// own data-c5bin attribute. The merged *.bin is written at offset 0 (its header
// already carries the right flash mode/size, so we pass "keep").

// Same build the batch flasher (batch.html) already uses successfully.
const ESPTOOL_JS = "https://unpkg.com/esptool-js@0.7.0/bundle.js";

function statusEl(card) {
  let el = card.querySelector("[data-c5status]");
  if (!el) {
    el = document.createElement("p");
    el.setAttribute("data-c5status", "");
    el.style.cssText = "font-size:12px;margin:8px 0 0;word-break:break-word";
    const btn = card.querySelector("[data-c5flash]");
    (btn ? btn.parentNode : card).insertBefore(el, btn ? btn.nextSibling : null);
  }
  return el;
}

function setStatus(card, msg, kind) {
  const el = statusEl(card);
  el.hidden = false;
  el.textContent = msg;
  el.style.color = kind === "err" ? "var(--danger)"
                 : kind === "ok"  ? "var(--ok, var(--accent))"
                 : "var(--muted)";
}

function binUrlFor(card, btn) {
  if (card && card.dataset.fwActive) {
    return card.dataset.fwActive === "ham" ? card.dataset.binHam : card.dataset.binFork;
  }
  return btn.getAttribute("data-c5bin") || (card && card.dataset.binFork);
}

async function flashC5(btn) {
  const card = btn.closest(".board") || btn.parentNode;
  if (!("serial" in navigator)) {
    setStatus(card, "This browser has no Web Serial — use desktop Chrome, Edge or Brave.", "err");
    return;
  }
  const url = binUrlFor(card, btn);
  if (!url) { setStatus(card, "No firmware image is set for this card.", "err"); return; }

  btn.disabled = true;
  let transport = null;
  try {
    setStatus(card, "Loading flasher…");
    const { ESPLoader, Transport } = await import(ESPTOOL_JS);

    setStatus(card, "Downloading firmware…");
    const resp = await fetch(url);
    if (!resp.ok) throw new Error("couldn't fetch image (" + resp.status + ")");
    const data = new Uint8Array(await resp.arrayBuffer());

    setStatus(card, "Select the C5's serial port in the prompt…");
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, true);

    const esploader = new ESPLoader({
      transport,
      baudrate: 115200,
      terminal: { clean() {}, writeLine(s) { console.log(s); }, write(s) { console.log(s); } },
      debugLogging: false,
    });

    setStatus(card, "Connecting to the chip…");
    const chip = await esploader.main();                       // esptool-js 0.7.0: detects C5 rev v1.2

    setStatus(card, "Connected: " + chip + ". Flashing…");
    await esploader.writeFlash({
      fileArray: [{ data, address: 0 }],                       // merged image at offset 0
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (_i, written, total) => {
        setStatus(card, "Flashing " + Math.round((written / total) * 100) + "%…");
      },
    });

    setStatus(card, "Flashed — resetting the board…", "ok");
    try { await esploader.after("hard_reset"); } catch (_) {}
    setStatus(card, "Done. The C5 is running the new firmware.", "ok");
  } catch (err) {
    const m = (err && err.message) ? err.message : String(err);
    setStatus(card, "Failed: " + m + ". Try the terminal steps below.", "err");
  } finally {
    try { if (transport) await transport.disconnect(); } catch (_) {}
    btn.disabled = false;
  }
}

export function mountC5Flash() {
  document.querySelectorAll("[data-c5flash]").forEach((btn) => {
    btn.addEventListener("click", () => flashC5(btn));
  });
}
