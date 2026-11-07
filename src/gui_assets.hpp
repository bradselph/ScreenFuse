#pragma once
namespace screenfuse {
namespace gui {

inline const char* IndexHtml() {
    static const char* text =
        R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ScreenFuse</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>

<header id="header">
  <div id="header-left">
    <span id="app-title">ScreenFuse</span>
    <span id="status-pill" class="pill pill-neutral">Idle</span>
    <span id="clock-pill" class="pill pill-neutral hidden"></span>
    <span id="record-pill" class="pill pill-error hidden">REC</span>
  </div>
  <div id="header-right">
    <button id="btn-connect" type="button" class="btn btn-primary">Connect</button>
    <button id="btn-screenshot" type="button" class="btn btn-secondary">Take Screenshot</button>
    <button id="btn-stream" type="button" class="btn btn-secondary">Start Stream</button>
    <button id="btn-quit" type="button" class="btn btn-danger" data-tip="shut the controller down (and the agent too, unless keep-agent is set)">Quit</button>
  </div>
</header>

<div id="firstrun-banner" class="banner hidden">
  <span>Both PCs need the same key file before ScreenFuse can connect to the agent. Generate one here, then copy it - and <code>screenfuse_agent.exe</code> - to the remote PC.</span>
  <button id="btn-genkey-banner" type="button" class="btn btn-primary btn-sm" data-flag="--genkey">Generate key</button>
</div>

<div id="error-banner" class="hidden">
  <span id="error-banner-text"></span>
  <button id="error-banner-close" type="button" class="btn-icon" aria-label="Close">&#10005;</button>
</div>

<main id="layout">
  <div id="left-col">

    <section class="section-card" data-section="connection">
      <button type="button" class="section-header"><span class="section-title">Connection</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="form-grid">
          <label for="opt-host">Host <span class="required">*</span></label>
          <input id="opt-host" type="text" class="form-input" placeholder="192.168.1.20" data-flag="--host" autocomplete="off" spellcheck="false">

          <label for="opt-port">Port</label>
          <input id="opt-port" type="number" class="form-input" min="1" max="65535" data-flag="--port">

          <label for="opt-keypath">Key file</label>
          <div class="input-with-button">
            <input id="opt-keypath" type="text" class="form-input" data-flag="--key" autocomplete="off" spellcheck="false">
            <button id="btn-browse-key" type="button" class="btn btn-secondary btn-sm">Browse</button>
          </div>

          <label for="opt-outdir">Output folder</label>
          <div class="input-with-button">
            <input id="opt-outdir" type="text" class="form-input" data-flag="--out" autocomplete="off" spellcheck="false">
            <button id="btn-browse-out" type="button" class="btn btn-secondary btn-sm">Browse</button>
            <button id="btn-open-out" type="button" class="btn btn-secondary btn-sm">Open folder</button>
          </div>

          <span></span>
          <button id="btn-genkey" type="button" class="btn btn-secondary btn-sm" data-flag="--genkey">Generate key</button>
        </div>
        <label class="switch-row">
          <span class="switch-label">Leave the agent running on exit</span>
          <span class="switch"><input type="checkbox" id="opt-keep-agent" data-flag="--keep-agent"><span class="switch-slider"></span></span>
        </label>
      </div>
    </section>

    <section class="section-card" data-section="displays">
      <button type="button" class="section-header"><span class="section-title">Displays</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="form-grid">
          <label for="opt-remote-monitor">Remote monitor</label>
          <select id="opt-remote-monitor" class="form-input" data-flag="--remote-monitor"></select>

          <label for="opt-overlay-monitor">Overlay monitor</label>
          <select id="opt-overlay-monitor" class="form-input" data-flag="--overlay-monitor"></select>

          <label for="opt-overlay-window">Overlay window</label>
          <input id="opt-overlay-window" type="text" class="form-input" placeholder="window class or title" data-flag="--overlay-window" autocomplete="off" spellcheck="false">

          <span></span>
          <button id="btn-refresh-state" type="button" class="btn btn-secondary btn-sm">Refresh</button>
        </div>
      </div>
    </section>

    <section class="section-card" data-section="timing">
      <button type="button" class="section-header"><span class="section-title">Timing</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="form-grid">
          <label for="opt-lead">Lead (ms)</label>
          <input id="opt-lead" type="number" class="form-input" min="50" data-flag="--lead">

          <label for="opt-overlay-delay">Overlay delay (ms)</label>
          <input id="opt-overlay-delay" type="number" class="form-input" min="0" data-flag="--overlay-delay">

          <label for="opt-max-skew">Max skew (ms)</label>
          <input id="opt-max-skew" type="number" class="form-input" min="0" data-flag="--max-skew">

          <label for="opt-max-age">Max age (ms)</label>
          <input id="opt-max-age" type="number" class="form-input" min="0" data-flag="--max-age">
        </div>
      </div>
    </section>
)HTML"
        R"HTML(
    <section class="section-card" data-section="compositing">
      <button type="button" class="section-header"><span class="section-title">Compositing</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="radio-btn-group" role="radiogroup" aria-label="Merge mode">
          <label class="radio-btn">
            <input type="radio" name="merge-mode" id="opt-merge-key" value="key" data-flag="--merge">
            <span>Luma key</span>
          </label>
          <label class="radio-btn">
            <input type="radio" name="merge-mode" id="opt-merge-screen" value="screen" data-flag="--merge">
            <span>Screen blend</span>
          </label>
        </div>
        <div class="form-grid">
          <label for="opt-key-low">Key low</label>
          <input id="opt-key-low" type="number" class="form-input" min="0" max="255" data-flag="--key-low">

          <label for="opt-key-high">Key high</label>
          <input id="opt-key-high" type="number" class="form-input" min="0" max="255" data-flag="--key-high">

          <label for="opt-min-black">Min black (0-1)</label>
          <input id="opt-min-black" type="number" class="form-input" min="0" max="1" step="0.05" data-flag="--min-black">

          <label for="opt-black-threshold">Black threshold</label>
          <input id="opt-black-threshold" type="number" class="form-input" min="0" max="255">
        </div>
        <div id="key-hint" class="hint-box"></div>
      </div>
    </section>

    <section class="section-card" data-section="screenshots">
      <button type="button" class="section-header"><span class="section-title">Screenshots</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <label class="switch-row">
          <span class="switch-label">PNG wire format</span>
          <span class="switch"><input type="checkbox" id="opt-png" data-flag="--png"><span class="switch-slider"></span></span>
        </label>
        <label class="switch-row">
          <span class="switch-label">Keep rejected captures</span>
          <span class="switch"><input type="checkbox" id="opt-keep-rejected" data-flag="--no-keep-rejected"><span class="switch-slider"></span></span>
        </label>
      </div>
    </section>

    <section class="section-card" data-section="streaming">
      <button type="button" class="section-header"><span class="section-title">Streaming</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="form-grid">
          <label for="opt-stream-fps">FPS</label>
          <input id="opt-stream-fps" type="number" class="form-input" min="1" max="240" data-flag="--stream-fps">

          <label for="opt-stream-quality">Quality</label>
          <input id="opt-stream-quality" type="number" class="form-input" min="1" max="100" data-flag="--stream-quality">

          <label for="opt-stream-scale">Scale (%)</label>
          <input id="opt-stream-scale" type="number" class="form-input" min="10" max="100" data-flag="--stream-scale">

          <label for="opt-stream-out-monitor">Output</label>
          <select id="opt-stream-out-monitor" class="form-input" data-flag="--stream-monitor"></select>

          <label for="opt-stream-title">Window title</label>
          <input id="opt-stream-title" type="text" class="form-input" data-flag="--stream-title" autocomplete="off" spellcheck="false">
        </div>
        <label class="switch-row">
          <span class="switch-label">Raw (uncompressed) frames</span>
          <span class="switch"><input type="checkbox" id="opt-stream-raw" data-flag="--stream-raw"><span class="switch-slider"></span></span>
        </label>
        <label class="switch-row">
          <span class="switch-label">High quality</span>
          <span class="switch"><input type="checkbox" id="opt-high-quality" data-flag="--high-quality"><span class="switch-slider"></span></span>
        </label>
      </div>
    </section>

    <section class="section-card" data-section="audio">
      <button type="button" class="section-header"><span class="section-title">Audio and recording</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <label class="switch-row">
          <span class="switch-label">Audio</span>
          <span class="switch"><input type="checkbox" id="opt-audio" data-flag="--audio"><span class="switch-slider"></span></span>
        </label>
        <div class="form-grid">
          <label for="opt-audio-buffer">Audio buffer (ms)</label>
          <input id="opt-audio-buffer" type="number" class="form-input" min="20" max="2000" data-flag="--audio-buffer">
        </div>
        <label class="switch-row">
          <span class="switch-label">Record</span>
          <span class="switch"><input type="checkbox" id="opt-record" data-flag="--record"><span class="switch-slider"></span></span>
        </label>
        <div class="form-grid">
          <label for="opt-record-bitrate">Record bitrate (Mbit/s, 0 = auto)</label>
          <input id="opt-record-bitrate" type="number" class="form-input" min="0" max="50" data-flag="--record-bitrate">
        </div>
      </div>
    </section>

    <section class="section-card" data-section="remerge">
      <button type="button" class="section-header"><span class="section-title">Re-merge saved frames</span><span class="section-arrow">&#9662;</span></button>
      <div class="section-body">
        <div class="form-grid">
          <label for="remerge-remote">Remote frame</label>
          <div class="input-with-button">
            <input id="remerge-remote" type="text" class="form-input" autocomplete="off" spellcheck="false">
            <button id="btn-browse-remerge-remote" type="button" class="btn btn-secondary btn-sm">Browse</button>
          </div>
          <label for="remerge-overlay">Overlay frame</label>
          <div class="input-with-button">
            <input id="remerge-overlay" type="text" class="form-input" autocomplete="off" spellcheck="false">
            <button id="btn-browse-remerge-overlay" type="button" class="btn btn-secondary btn-sm">Browse</button>
          </div>
        </div>
        <button id="btn-remerge" type="button" class="btn btn-primary btn-sm">Re-merge</button>
        <div id="remerge-result" class="hint-box hidden"></div>
      </div>
    </section>

  </div>
)HTML"
        R"HTML(
  <div id="right-col">

    <section class="panel">
      <div class="panel-title">Live preview</div>
      <div id="preview-frame">
        <img id="preview-img" alt="live merged preview">
        <div id="preview-placeholder">no active stream</div>
      </div>
      <div class="preview-toolbar">
        <button id="btn-save-frame" type="button" class="btn btn-secondary btn-sm">Save this frame</button>
        <button id="btn-record-toggle" type="button" class="btn btn-secondary btn-sm">Start recording</button>
      </div>
      <div id="preview-stats" class="muted-text"></div>
    </section>

    <section class="panel">
      <div class="panel-title">Last capture</div>
      <div id="lastcap-empty" class="muted-text">No captures yet.</div>
      <div id="lastcap-body" class="hidden">
        <div class="lastcap-head">
          <span id="lastcap-badge" class="badge"></span>
          <span id="lastcap-name" class="lastcap-name"></span>
        </div>
        <div id="lastcap-stamp" class="muted-text"></div>
        <ul id="lastcap-checks" class="check-list"></ul>
      </div>
    </section>

    <section class="panel">
      <div class="panel-title-row">
        <span class="panel-title">Recent captures</span>
        <span id="captures-counts" class="muted-text"></span>
      </div>
      <div id="captures-grid" class="thumb-grid"></div>
    </section>

    <section class="panel">
      <div class="panel-title-row">
        <span class="panel-title">Log</span>
        <span class="log-toolbar">
          <button id="btn-log-pause" type="button" class="btn btn-secondary btn-sm">Pause</button>
          <button id="btn-log-clear" type="button" class="btn btn-secondary btn-sm">Clear</button>
        </span>
      </div>
      <div id="log-panel" class="output-pre log-panel"></div>
    </section>

  </div>
</main>

<div id="toast" class="hidden" role="alert" aria-live="polite">
  <span id="toast-message"></span>
  <button id="toast-close" type="button" class="btn-icon" aria-label="Dismiss">&#10005;</button>
</div>

<div id="image-modal" class="modal-overlay hidden" role="dialog" aria-modal="true">
  <div class="modal-box modal-box-wide">
    <div class="modal-header">
      <span id="image-modal-title" class="modal-title"></span>
      <button type="button" class="btn-icon modal-close" data-modal="image-modal" aria-label="Close">&#10005;</button>
    </div>
    <div class="modal-body">
      <img id="image-modal-img" class="modal-image" alt="capture">
    </div>
    <div class="modal-footer">
      <button id="image-modal-open" type="button" class="btn btn-secondary">Open file location</button>
      <button type="button" class="btn btn-secondary modal-close" data-modal="image-modal">Close</button>
    </div>
  </div>
</div>

<div id="confirm-modal" class="modal-overlay hidden" role="dialog" aria-modal="true">
  <div class="modal-box">
    <div class="modal-header">
      <span id="confirm-modal-title" class="modal-title"></span>
      <button type="button" class="btn-icon modal-close" data-modal="confirm-modal" aria-label="Close">&#10005;</button>
    </div>
    <div class="modal-body">
      <p id="confirm-modal-text"></p>
    </div>
    <div class="modal-footer">
      <button id="confirm-modal-yes" type="button" class="btn btn-danger">Confirm</button>
      <button type="button" class="btn btn-secondary modal-close" data-modal="confirm-modal">Cancel</button>
    </div>
  </div>
</div>

<script src="/app.js"></script>
</body>
</html>
)HTML";
    return text;
}

inline const char* StyleCss() {
    static const char* text =
        R"CSS(*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

html, body {
  height: 100%;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  font-size: 13px;
  background: #0e1117;
  color: #c9d1d9;
  line-height: 1.5;
}

#header {
  position: sticky;
  top: 0;
  z-index: 100;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 0 20px;
  height: 52px;
  background: #161b22;
  border-bottom: 1px solid #30363d;
  flex-wrap: wrap;
}

#header-left, #header-right { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }

#app-title {
  font-size: 15px;
  font-weight: 600;
  color: #e6edf3;
  white-space: nowrap;
  letter-spacing: 0.02em;
}

.pill {
  display: inline-flex;
  align-items: center;
  padding: 2px 10px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 600;
  white-space: nowrap;
}
.pill-neutral { background: #21262d; color: #8b949e; border: 1px solid #30363d; }
.pill-ok      { background: #0d2014; color: #3fb950; border: 1px solid #196127; }
.pill-warn    { background: #1f1600; color: #d29922; border: 1px solid #5c3d00; }
.pill-error   { background: #2d0f0f; color: #f85149; border: 1px solid #6e1212; }

.btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 12px;
  border-radius: 6px;
  border: 1px solid transparent;
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  white-space: nowrap;
  transition: background 0.12s, border-color 0.12s, opacity 0.12s;
}
.btn:disabled { opacity: 0.4; cursor: not-allowed; }

.btn-primary   { background: #238636; border-color: #2ea043; color: #fff; }
.btn-primary:hover:not(:disabled)   { background: #2ea043; border-color: #3fb950; }
.btn-secondary { background: #21262d; border-color: #30363d; color: #c9d1d9; }
.btn-secondary:hover:not(:disabled) { background: #30363d; border-color: #484f58; }
.btn-danger    { background: #3d0f0f; border-color: #6e1212; color: #f85149; }
.btn-danger:hover:not(:disabled)    { background: #5d1a1a; border-color: #f85149; }
.btn-sm { padding: 5px 9px; font-size: 12px; }

.btn-icon {
  background: transparent;
  border: none;
  color: #6e7681;
  cursor: pointer;
  font-size: 14px;
  padding: 2px 6px;
  border-radius: 4px;
  line-height: 1;
}
.btn-icon:hover { color: #c9d1d9; background: #30363d; }

.app-tooltip {
  position: fixed;
  z-index: 1000;
  max-width: 280px;
  padding: 6px 10px;
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 6px;
  color: #e6edf3;
  font-size: 12px;
  line-height: 1.45;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.55);
  pointer-events: none;
  transform: translate(-50%, -100%);
  white-space: pre-line;
  overflow-wrap: anywhere;
}
.app-tooltip.below { transform: translate(-50%, 0); }

#error-banner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 20px;
  background: #2d0f0f;
  border-bottom: 1px solid #6e1212;
  color: #f85149;
  font-size: 13px;
}
#error-banner.hidden { display: none; }

.banner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 10px 20px;
  background: #1c1600;
  border-bottom: 1px solid #5c4500;
  color: #e3b341;
  font-size: 13px;
}
.banner.hidden { display: none; }
.banner code {
  background: #0e1117;
  border: 1px solid #30363d;
  border-radius: 4px;
  padding: 1px 5px;
  font-family: "SF Mono", "Fira Code", Consolas, monospace;
}

.muted-text { color: #6e7681; font-size: 12px; }
)CSS"
        R"CSS(
#layout {
  display: grid;
  grid-template-columns: minmax(340px, 460px) 1fr;
  gap: 16px;
  padding: 16px 20px;
  align-items: start;
}

#left-col, #right-col { display: flex; flex-direction: column; gap: 12px; min-width: 0; }

/* The settings column is long and the working column is not, so the working
   column follows the scroll instead of leaving a screen of nothing beside it. */
#right-col {
  position: sticky;
  top: 68px;
  max-height: calc(100vh - 84px);
  overflow-y: auto;
  padding-right: 4px;
}

.section-card {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  overflow: hidden;
}

.section-header {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  background: #1c2128;
  border: none;
  color: #e6edf3;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  text-align: left;
}
.section-header:hover { background: #21262d; }
.section-title { text-transform: uppercase; letter-spacing: 0.04em; }
.section-arrow { color: #6e7681; transition: transform 0.12s; display: inline-block; }
.section-card.collapsed .section-arrow { transform: rotate(-90deg); }
.section-card.collapsed .section-body { display: none; }

.section-body { padding: 14px; display: flex; flex-direction: column; gap: 12px; }

.form-grid {
  display: grid;
  grid-template-columns: 112px 1fr;
  gap: 8px 12px;
  align-items: center;
}
label { color: #8b949e; font-size: 12px; }
.required { color: #f85149; }
.hint { color: #484f58; }

.form-input {
  background: #1c2128;
  border: 1px solid #30363d;
  border-radius: 6px;
  padding: 6px 10px;
  color: #c9d1d9;
  font-size: 12.5px;
  width: 100%;
  outline: none;
  transition: border-color 0.12s;
}
.form-input:focus { border-color: #58a6ff; }
.form-input:disabled { opacity: 0.45; cursor: not-allowed; }
select.form-input { cursor: pointer; }

.input-with-button { display: flex; gap: 6px; }
.input-with-button .form-input { flex: 1; min-width: 0; }

.radio-btn-group { display: flex; gap: 8px; }
.radio-btn {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 7px 10px;
  border-radius: 6px;
  border: 1px solid #30363d;
  background: #1c2128;
  color: #8b949e;
  font-size: 12.5px;
  cursor: pointer;
}
.radio-btn input { display: none; }
.radio-btn.active { border-color: #58a6ff; color: #e6edf3; background: #0d1f38; }

.switch-row { display: flex; align-items: center; justify-content: space-between; gap: 12px; cursor: pointer; }
.switch-label { color: #c9d1d9; font-size: 12.5px; }
.switch { position: relative; display: inline-block; width: 34px; height: 18px; flex-shrink: 0; }
.switch input { display: none; }
.switch-slider {
  position: absolute;
  inset: 0;
  background: #21262d;
  border: 1px solid #30363d;
  border-radius: 18px;
  transition: background 0.15s, border-color 0.15s;
}
.switch-slider::before {
  content: "";
  position: absolute;
  width: 12px;
  height: 12px;
  left: 2px;
  top: 2px;
  background: #8b949e;
  border-radius: 50%;
  transition: transform 0.15s, background 0.15s;
}
.switch input:checked + .switch-slider { background: #0d2014; border-color: #196127; }
.switch input:checked + .switch-slider::before { transform: translateX(16px); background: #3fb950; }
.switch input:disabled + .switch-slider { opacity: 0.4; cursor: not-allowed; }

.hint-box {
  background: #0e1117;
  border: 1px solid #21262d;
  border-radius: 6px;
  padding: 8px 10px;
  font-size: 11.5px;
  color: #8b949e;
  line-height: 1.5;
}
.hint-box.hint-warn { color: #d29922; border-color: #5c3d00; background: #1f1600; }
)CSS"
        R"CSS(
.panel {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 8px;
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.panel-title { font-size: 12px; font-weight: 600; color: #e6edf3; text-transform: uppercase; letter-spacing: 0.04em; }
.panel-title-row { display: flex; align-items: center; justify-content: space-between; }

#preview-frame {
  position: relative;
  width: 100%;
  aspect-ratio: 16 / 9;
  background: #0e1117;
  border: 1px solid #21262d;
  border-radius: 6px;
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
}
#preview-img { width: 100%; height: 100%; object-fit: contain; display: none; }
#preview-img.active { display: block; }
#preview-placeholder { color: #484f58; font-size: 12.5px; }
#preview-frame.streaming #preview-placeholder { display: none; }

.preview-toolbar { display: flex; gap: 8px; flex-wrap: wrap; }

.lastcap-head { display: flex; align-items: center; gap: 10px; }
.badge {
  display: inline-block;
  padding: 1px 8px;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.badge-accepted { background: #0d2014; color: #3fb950; border: 1px solid #196127; }
.badge-rejected { background: #2d0f0f; color: #f85149; border: 1px solid #6e1212; }
.lastcap-name {
  font-family: "SF Mono", "Fira Code", Consolas, monospace;
  font-size: 11.5px;
  color: #c9d1d9;
  overflow: hidden;
  text-overflow: ellipsis;
}

.check-list { list-style: none; display: flex; flex-direction: column; gap: 4px; margin-top: 4px; }
.check-list li {
  font-family: "SF Mono", "Fira Code", Consolas, monospace;
  font-size: 11px;
  padding: 4px 8px;
  border-radius: 4px;
  background: #0e1117;
  border: 1px solid #21262d;
}
.check-pass { color: #3fb950; }
.check-warn { color: #d29922; }
.check-fail { color: #f85149; }

.thumb-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(90px, 1fr)); gap: 8px; }
.thumb {
  position: relative;
  aspect-ratio: 1;
  border-radius: 6px;
  overflow: hidden;
  border: 1px solid #21262d;
  background: #0e1117;
  cursor: pointer;
}
.thumb img { width: 100%; height: 100%; object-fit: cover; display: block; }
.thumb-kind {
  position: absolute;
  bottom: 3px;
  left: 3px;
  padding: 0 5px;
  font-size: 9px;
  font-weight: 700;
  text-transform: uppercase;
  border-radius: 3px;
  background: rgba(14, 17, 23, 0.85);
  color: #8b949e;
}

.log-toolbar { display: flex; gap: 8px; }
.output-pre {
  background: #0e1117;
  border: 1px solid #21262d;
  border-radius: 6px;
  padding: 10px;
  color: #b3d0f0;
  font-family: "SF Mono", "Fira Code", Consolas, monospace;
  font-size: 11px;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-all;
}
.log-panel { height: 260px; overflow-y: auto; }
.log-line { display: block; }
.log-info  { color: #b3d0f0; }
.log-warn  { color: #d29922; }
.log-error { color: #f85149; }
)CSS"
        R"CSS(
#toast {
  position: fixed;
  bottom: 24px;
  right: 24px;
  z-index: 500;
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 12px 16px;
  border-radius: 8px;
  max-width: 420px;
  font-size: 13px;
  box-shadow: 0 4px 24px rgba(0,0,0,0.5);
  border: 1px solid transparent;
  word-break: break-word;
}
#toast.toast-ok    { background: #0d2014; border-color: #196127; color: #3fb950; }
#toast.toast-error { background: #2d0f0f; border-color: #6e1212; color: #f85149; }
#toast.hidden       { display: none; }
#toast-message { flex: 1; }

.modal-overlay {
  position: fixed;
  inset: 0;
  z-index: 300;
  background: rgba(0,0,0,0.72);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
}
.modal-overlay.hidden { display: none; }
.modal-box {
  background: #161b22;
  border: 1px solid #30363d;
  border-radius: 10px;
  width: 100%;
  max-width: 480px;
  max-height: calc(100vh - 80px);
  display: flex;
  flex-direction: column;
  box-shadow: 0 8px 48px rgba(0,0,0,0.7);
}
.modal-box-wide { max-width: 760px; }
.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px 12px;
  border-bottom: 1px solid #30363d;
}
.modal-title { font-weight: 600; color: #e6edf3; font-size: 14px; }
.modal-body { padding: 18px; overflow-y: auto; flex: 1; }
.modal-footer { padding: 12px 18px; border-top: 1px solid #30363d; display: flex; gap: 8px; justify-content: flex-end; }
.modal-image { width: 100%; border-radius: 6px; display: block; }

.hidden { display: none !important; }

::-webkit-scrollbar { width: 8px; height: 8px; }
::-webkit-scrollbar-track { background: #0e1117; }
::-webkit-scrollbar-thumb { background: #30363d; border-radius: 4px; }
::-webkit-scrollbar-thumb:hover { background: #484f58; }

@media (max-width: 900px) {
  #layout { grid-template-columns: 1fr; }
  #header { height: auto; padding: 10px 14px; }
  .form-grid { grid-template-columns: 1fr; }
  .form-grid label { padding-top: 4px; }
}
)CSS";
    return text;
}

inline const char* AppJs() {
    static const char* text =
        R"JS("use strict";

// The token is read once from the URL that launched the page and kept only
// in memory - it never goes back into the address bar or into storage.
let API_TOKEN = "";
(function initToken() {
    try {
        const params = new URLSearchParams(window.location.search);
        const t = params.get("token");
        if (t) API_TOKEN = t;
        const clean = window.location.pathname + window.location.hash;
        history.replaceState(null, "", clean);
    } catch (e) { /* URL API unavailable - token stays empty, requests will 401 */ }
})();

function $(id) { return document.getElementById(id); }
function on(el, ev, fn) { if (el) el.addEventListener(ev, fn); }

// One fetch helper: attaches the token, encodes a JSON body, and turns a
// non-2xx response into a rejected promise carrying the server's message.
async function api(path, opts) {
    opts = opts || {};
    const headers = Object.assign({}, opts.headers || {}, { "X-SF-Token": API_TOKEN });
    let body = opts.body;
    if (body && typeof body === "object") {
        headers["Content-Type"] = "application/json";
        body = JSON.stringify(body);
    }
    let resp;
    try {
        resp = await fetch(path, Object.assign({}, opts, { headers: headers, body: body }));
    } catch (e) {
        throw new Error("network error: " + e.message);
    }
    let data = null;
    try { data = await resp.json(); } catch (e) { data = null; }
    if (!resp.ok) {
        const msg = (data && data.error) ? data.error : ("HTTP " + resp.status);
        throw new Error(msg);
    }
    return data;
}

const state = {
    server: null,
    help: null,
    captures: [],
    logSeq: 0,
    logPaused: false,
    modalImagePath: "",
};

const OPTION_FIELDS = [
    { id: "opt-host", key: "host", type: "text" },
    { id: "opt-port", key: "port", type: "int" },
    { id: "opt-keypath", key: "keyPath", type: "text" },
    { id: "opt-outdir", key: "outDir", type: "text" },
    { id: "opt-overlay-window", key: "overlayWindow", type: "text" },
    { id: "opt-lead", key: "leadMs", type: "int" },
    { id: "opt-overlay-delay", key: "overlayDelayMs", type: "int" },
    { id: "opt-max-skew", key: "maxSkewMs", type: "int" },
    { id: "opt-max-age", key: "maxAgeMs", type: "int" },
    { id: "opt-key-low", key: "lowKey", type: "int" },
    { id: "opt-key-high", key: "highKey", type: "int" },
    { id: "opt-min-black", key: "minBlackRatio", type: "float" },
    { id: "opt-black-threshold", key: "blackThreshold", type: "int" },
    { id: "opt-png", key: "png", type: "bool" },
    { id: "opt-keep-rejected", key: "keepRejected", type: "bool" },
    { id: "opt-keep-agent", key: "keepAgent", type: "bool" },
    { id: "opt-stream-fps", key: "streamFps", type: "int" },
    { id: "opt-stream-quality", key: "streamQuality", type: "int" },
    { id: "opt-stream-scale", key: "streamScale", type: "int" },
    { id: "opt-stream-raw", key: "streamRaw", type: "bool" },
    { id: "opt-high-quality", key: "highQuality", type: "bool" },
    { id: "opt-stream-title", key: "streamTitle", type: "text" },
    { id: "opt-audio", key: "audio", type: "bool" },
    { id: "opt-audio-buffer", key: "audioBufferMs", type: "int" },
    { id: "opt-record", key: "record", type: "bool" },
    { id: "opt-record-bitrate", key: "recordBitrateMbit", type: "int" },
];

// Reconciling from server state must never clobber a field the user is
// mid-way through typing, or the panel becomes unusable under the 1 s poll.
function applyOptionsToForm(o) {
    for (const f of OPTION_FIELDS) {
        const el = $(f.id);
        if (!el || document.activeElement === el) continue;
        const v = o[f.key];
        if (v === undefined) continue;
        if (f.type === "bool") el.checked = !!v;
        else el.value = String(v);
    }
    const keyEl = $("opt-merge-key"), screenEl = $("opt-merge-screen");
    if (keyEl && screenEl && document.activeElement !== keyEl && document.activeElement !== screenEl) {
        screenEl.checked = !!o.screenMerge;
        keyEl.checked = !o.screenMerge;
    }
    // A Windows path rarely fits the field, so the whole thing lives in the
    // hover tooltip alongside what the setting means.
    const flags = (state.help && state.help.flags) || {};
    for (const id of ["opt-keypath", "opt-outdir"]) {
        const el = $(id);
        if (!el || !el.value) continue;
        const help = flags[el.dataset.flag] || "";
        el.dataset.tip = el.value + (help ? "\n\n" + help : "");
    }
}

function collectOptions() {
    const base = Object.assign({}, (state.server && state.server.options) || {});
    for (const f of OPTION_FIELDS) {
        const el = $(f.id);
        if (!el) continue;
        if (f.type === "bool") base[f.key] = !!el.checked;
        else if (f.type === "int") { const n = parseInt(el.value, 10); if (!isNaN(n)) base[f.key] = n; }
        else if (f.type === "float") { const n = parseFloat(el.value); if (!isNaN(n)) base[f.key] = n; }
        else base[f.key] = el.value;
    }
    const screenEl = $("opt-merge-screen");
    if (screenEl) base.screenMerge = !!screenEl.checked;
    const remoteMonEl = $("opt-remote-monitor");
    if (remoteMonEl && remoteMonEl.value !== "") base.remoteMonitor = parseInt(remoteMonEl.value, 10);
    const overlayMonEl = $("opt-overlay-monitor");
    if (overlayMonEl && overlayMonEl.value !== "") base.overlayMonitor = parseInt(overlayMonEl.value, 10);
    const streamOutEl = $("opt-stream-out-monitor");
    if (streamOutEl && streamOutEl.value !== "") base.streamOutMonitor = parseInt(streamOutEl.value, 10);
    return base;
}

let optionsDirty = false;
let optionsTimer = null;
function scheduleOptionsPost() {
    optionsDirty = true;
    if (optionsTimer) clearTimeout(optionsTimer);
    optionsTimer = setTimeout(postOptions, 350);
}
async function postOptions() {
    if (!optionsDirty) return;
    optionsDirty = false;
    try {
        const res = await api("/api/options", { method: "POST", body: collectOptions() });
        if (res && Array.isArray(res.warnings) && res.warnings.length) showToast(res.warnings.join(" "), true);
    } catch (e) {
        showToast("options not saved: " + e.message, false);
    }
}
)JS"
        R"JS(
function updateMergeRadioVisual() {
    const keyEl = $("opt-merge-key"), screenEl = $("opt-merge-screen");
    const keyLbl = keyEl ? keyEl.closest(".radio-btn") : null;
    const screenLbl = screenEl ? screenEl.closest(".radio-btn") : null;
    if (keyLbl) keyLbl.classList.toggle("active", !!(keyEl && keyEl.checked));
    if (screenLbl) screenLbl.classList.toggle("active", !!(screenEl && screenEl.checked));
}

function updateKeyHint() {
    const box = $("key-hint");
    if (!box) return;
    const lowEl = $("opt-key-low"), highEl = $("opt-key-high"), screenEl = $("opt-merge-screen");
    if (screenEl && screenEl.checked) {
        box.textContent = "screen blend keeps every overlay pixel and brightens what is underneath - key-low/key-high are not used in this mode.";
        box.classList.remove("hint-warn");
        return;
    }
    const low = lowEl ? parseInt(lowEl.value, 10) : NaN;
    const high = highEl ? parseInt(highEl.value, 10) : NaN;
    if (isNaN(low) || isNaN(high)) { box.textContent = ""; box.classList.remove("hint-warn"); return; }
    const gap = high - low;
    if (gap > 32) {
        box.textContent = "gap of " + gap + " between key-low and key-high: everything between the two fades in proportion, so a wide gap dims the overlay rather than softening its edges. Keep key-high close to key-low.";
        box.classList.add("hint-warn");
    } else {
        box.textContent = "gap of " + gap + " - the soft ramp only smooths anti-aliased edges. Raise key-low if the overlay's black is not quite black.";
        box.classList.remove("hint-warn");
    }
}

function onMergeChange() { updateMergeRadioVisual(); updateKeyHint(); scheduleOptionsPost(); }

function makeOption(value, label) {
    const o = document.createElement("option");
    o.value = String(value);
    o.textContent = label;
    return o;
}
function monitorLabel(m) {
    let s = "[" + m.index + "] " + m.device + " " + m.width + "x" + m.height;
    if (m.primary) s += " (primary)";
    if (m.hasOverlayWindow) s += " (overlay window here)";
    return s;
}

// The three monitor selects are rebuilt from scratch on change, which would
// steal an open dropdown out from under the user every second of polling -
// so this is skipped both while a select has focus and when nothing changed.
let lastMonitorsSig = "";
function rebuildMonitorSelects(server) {
    if (!server) return;
    const sig = JSON.stringify(server.monitors || []);
    if (sig === lastMonitorsSig) return;
    lastMonitorsSig = sig;

    const monitors = server.monitors || [];
    const opts = server.options || {};

    const remoteSel = $("opt-remote-monitor");
    if (remoteSel && document.activeElement !== remoteSel) {
        remoteSel.innerHTML = "";
        remoteSel.appendChild(makeOption(-1, "agent's default"));
        for (const m of monitors) remoteSel.appendChild(makeOption(m.index, monitorLabel(m)));
        remoteSel.value = String(opts.remoteMonitor != null ? opts.remoteMonitor : -1);
    }
    const overlaySel = $("opt-overlay-monitor");
    if (overlaySel && document.activeElement !== overlaySel) {
        overlaySel.innerHTML = "";
        overlaySel.appendChild(makeOption(-1, "primary / auto"));
        for (const m of monitors) overlaySel.appendChild(makeOption(m.index, monitorLabel(m)));
        overlaySel.value = String(opts.overlayMonitor != null ? opts.overlayMonitor : -1);
    }
    const streamSel = $("opt-stream-out-monitor");
    if (streamSel && document.activeElement !== streamSel) {
        streamSel.innerHTML = "";
        streamSel.appendChild(makeOption(-1, "window"));
        for (const m of monitors) streamSel.appendChild(makeOption(m.index, monitorLabel(m)));
        streamSel.value = String(opts.streamOutMonitor != null ? opts.streamOutMonitor : -1);
    }
}

function wireOptionControls() {
    for (const f of OPTION_FIELDS) {
        const el = $(f.id);
        if (!el) continue;
        on(el, f.type === "bool" ? "change" : "input", scheduleOptionsPost);
    }
    on($("opt-merge-key"), "change", onMergeChange);
    on($("opt-merge-screen"), "change", onMergeChange);
    on($("opt-remote-monitor"), "change", scheduleOptionsPost);
    on($("opt-overlay-monitor"), "change", scheduleOptionsPost);
    on($("opt-stream-out-monitor"), "change", scheduleOptionsPost);
    on($("opt-key-low"), "input", updateKeyHint);
    on($("opt-key-high"), "input", updateKeyHint);
}
)JS"
        R"JS(
function applyHelpTooltips() {
    const flags = (state.help && state.help.flags) || {};
    document.querySelectorAll("[data-flag]").forEach(el => {
        if (el.disabled) return;
        const f = el.dataset.flag;
        el.dataset.tip = flags[f] || f;
    });
    const bt = $("opt-black-threshold");
    if (bt) bt.dataset.tip = "luma at or below this counts as black for the --min-black check and the compositing statistics - not a separate command-line flag";
}
function restoreHelpTip(el) {
    const flags = (state.help && state.help.flags) || {};
    const f = el.dataset.flag;
    if (f && flags[f]) el.dataset.tip = flags[f];
}

const MODE_LABEL = { idle: "Idle", connecting: "Connecting", connected: "Connected", streaming: "Streaming" };

function renderStatusPills(s) {
    const pill = $("status-pill");
    if (pill) {
        let label = MODE_LABEL[s.mode] || s.mode || "Idle";
        let cls = "pill-neutral";
        if (s.linkDead) { label = "Link down"; cls = "pill-error"; }
        else if (s.mode === "connecting") cls = "pill-warn";
        else if (s.mode === "connected" || s.mode === "streaming") cls = "pill-ok";
        pill.textContent = label;
        pill.className = "pill " + cls;
    }
    const clockPill = $("clock-pill");
    if (clockPill) {
        if ((s.mode === "connected" || s.mode === "streaming") && s.agent) {
            clockPill.textContent = "offset " + s.agent.clockOffsetMs.toFixed(1) + " ms / rtt " + s.agent.rttMs.toFixed(1) + " ms";
            clockPill.classList.remove("hidden");
        } else {
            clockPill.classList.add("hidden");
        }
    }
    const recPill = $("record-pill");
    if (recPill) recPill.classList.toggle("hidden", !(s.recording && s.recording.armed));
}

function renderFirstRun(s) {
    const banner = $("firstrun-banner");
    if (banner) banner.classList.toggle("hidden", !!(s.key && s.key.present));
}

function renderButtons(s) {
    const busy = !!s.busy;
    const keyPresent = !!(s.key && s.key.present);
    const hostFilled = !!(s.options && s.options.host);

    const btnConnect = $("btn-connect");
    if (btnConnect) {
        if (s.mode === "idle") {
            btnConnect.textContent = "Connect";
            btnConnect.className = "btn btn-primary";
            btnConnect.disabled = busy || !keyPresent || !hostFilled;
            btnConnect.dataset.tip = !keyPresent ? "generate or point at a shared key first"
                : (!hostFilled ? "enter the agent's host/IP first" : "open a session with screenfuse_agent on Host");
        } else if (s.mode === "connecting") {
            btnConnect.textContent = "Connecting" + "\u2026";
            btnConnect.className = "btn btn-primary";
            btnConnect.disabled = true;
            btnConnect.dataset.tip = "waiting for the agent to answer";
        } else {
            btnConnect.textContent = "Disconnect";
            btnConnect.className = "btn btn-danger";
            btnConnect.disabled = busy;
            btnConnect.dataset.tip = "close the session (the agent keeps running if keep-agent is set)";
        }
    }
    const btnShot = $("btn-screenshot");
    if (btnShot) {
        btnShot.disabled = busy || s.mode !== "connected";
        btnShot.dataset.tip = s.mode === "streaming"
            ? "use \"Save this frame\" in the preview panel while streaming"
            : "capture and merge one verified screenshot now";
    }
    const btnStream = $("btn-stream");
    if (btnStream) {
        if (s.mode === "streaming") {
            btnStream.textContent = "Stop Stream";
            btnStream.className = "btn btn-danger";
            btnStream.disabled = busy;
            btnStream.dataset.tip = "stop the live stream";
        } else {
            btnStream.textContent = "Start Stream";
            btnStream.className = "btn btn-secondary";
            btnStream.disabled = busy || s.mode !== "connected";
            btnStream.dataset.tip = "stream continuously into the live preview window";
        }
    }
}

function renderBusyGates(s) {
    const busy = !!s.busy;
    ["btn-remerge", "btn-genkey", "btn-genkey-banner"].forEach(id => {
        const el = $(id);
        if (el) el.disabled = busy;
    });
}

// Mirrors the CLI's own refusal: --audio and --record only work with
// --stream, so the GUI disables them outside an active stream too.
function renderAudioRecordGate(s) {
    const streaming = s.mode === "streaming";
    for (const id of ["opt-audio", "opt-audio-buffer", "opt-record", "opt-record-bitrate"]) {
        const el = $(id);
        if (!el) continue;
        el.disabled = !streaming;
        if (!streaming) el.dataset.tip = "needs an active stream: audio and recording only take effect while streaming, just as --audio and --record refuse to run without --stream";
        else restoreHelpTip(el);
    }
}
)JS"
        R"JS(
let previewActive = false;
function renderPreview(s) {
    const img = $("preview-img");
    const frame = $("preview-frame");
    const shouldStream = s.mode === "streaming";
    if (shouldStream && !previewActive) {
        previewActive = true;
        if (img) { img.src = "/api/preview.mjpeg?token=" + encodeURIComponent(API_TOKEN); img.classList.add("active"); }
        if (frame) frame.classList.add("streaming");
    } else if (!shouldStream && previewActive) {
        previewActive = false;
        if (img) { img.removeAttribute("src"); img.classList.remove("active"); }
        if (frame) frame.classList.remove("streaming");
    }
    const btnSave = $("btn-save-frame");
    if (btnSave) btnSave.disabled = !shouldStream || !!s.busy;

    const stats = $("preview-stats");
    if (stats) {
        if (shouldStream && s.stream) {
            const st = s.stream;
            const dot = " \u00b7 ";
            let text = st.width + "x" + st.height + dot + st.inFps.toFixed(1) + " fps in / " + st.outFps.toFixed(1) + " fps out" + dot + st.megabits.toFixed(1) + " Mbit/s" + dot + st.latencyMs.toFixed(0) + " ms latency";
            if (st.pairingMs) text += dot + "overlay paired " + st.pairingMs.toFixed(1) + " ms";
            stats.textContent = text;
        } else {
            stats.textContent = "";
        }
    }
}

function renderRecordButton(s) {
    const btn = $("btn-record-toggle");
    if (!btn) return;
    const streaming = s.mode === "streaming";
    const armed = !!(s.recording && s.recording.armed);
    btn.textContent = armed ? "Stop recording" : "Start recording";
    btn.className = "btn btn-sm " + (armed ? "btn-danger" : "btn-secondary");
    btn.disabled = !streaming || !!s.busy;
}

function fileNameOf(p) {
    if (!p) return "";
    const parts = p.split(/[\\/]/);
    return parts[parts.length - 1];
}

function renderLastCapture(cap) {
    const empty = $("lastcap-empty");
    const body = $("lastcap-body");
    if (!cap || !cap.have) {
        if (empty) empty.classList.remove("hidden");
        if (body) body.classList.add("hidden");
        return;
    }
    if (empty) empty.classList.add("hidden");
    if (body) body.classList.remove("hidden");
    const badge = $("lastcap-badge");
    if (badge) {
        badge.textContent = cap.accepted ? "ACCEPTED" : "REJECTED";
        badge.className = "badge " + (cap.accepted ? "badge-accepted" : "badge-rejected");
    }
    const name = $("lastcap-name");
    if (name) name.textContent = fileNameOf(cap.path);
    const stamp = $("lastcap-stamp");
    if (stamp) stamp.textContent = cap.stamp || "";
    const list = $("lastcap-checks");
    if (list) {
        list.innerHTML = "";
        for (const line of (cap.checks || [])) {
            const li = document.createElement("li");
            li.textContent = line;
            if (line.indexOf("PASS") === 0) li.className = "check-pass";
            else if (line.indexOf("WARN") === 0) li.className = "check-warn";
            else if (line.indexOf("FAIL") === 0) li.className = "check-fail";
            list.appendChild(li);
        }
    }
}

function renderCounters(s) {
    const el = $("captures-counts");
    if (el && s.counters) el.textContent = s.counters.saved + " saved \u00b7 " + s.counters.rejected + " rejected";
}

function render() {
    const s = state.server;
    if (!s) return;
    renderStatusPills(s);
    renderFirstRun(s);
    renderButtons(s);
    renderBusyGates(s);
    applyOptionsToForm(s.options || {});
    rebuildMonitorSelects(s);
    updateMergeRadioVisual();
    updateKeyHint();
    renderAudioRecordGate(s);
    renderPreview(s);
    renderRecordButton(s);
    renderLastCapture(s.lastCapture);
    renderCounters(s);
}
)JS"
        R"JS(
function openModal(el) { if (el) el.classList.remove("hidden"); }
function closeModal(el) { if (el) el.classList.add("hidden"); }

document.querySelectorAll(".modal-close").forEach(btn => {
    on(btn, "click", () => { const id = btn.dataset.modal; if (id) closeModal($(id)); });
});
document.querySelectorAll(".modal-overlay").forEach(overlay => {
    on(overlay, "click", (e) => { if (e.target === overlay) closeModal(overlay); });
});
document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    const openOnes = document.querySelectorAll(".modal-overlay:not(.hidden)");
    if (openOnes.length) closeModal(openOnes[openOnes.length - 1]);
});

let toastTimer = null;
function showToast(msg, ok) {
    const el = $("toast"), msgEl = $("toast-message");
    if (!el || !msgEl) return;
    msgEl.textContent = msg;
    el.classList.remove("hidden", "toast-ok", "toast-error");
    el.classList.add(ok ? "toast-ok" : "toast-error");
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.add("hidden"), 6000);
}
on($("toast-close"), "click", () => { const el = $("toast"); if (el) el.classList.add("hidden"); if (toastTimer) clearTimeout(toastTimer); });

function showErrorBanner(msg) {
    const el = $("error-banner"), txt = $("error-banner-text");
    if (txt) txt.textContent = msg;
    if (el) el.classList.remove("hidden");
}
function hideErrorBanner() { const el = $("error-banner"); if (el) el.classList.add("hidden"); }
on($("error-banner-close"), "click", hideErrorBanner);

// A lightweight modal replaces the blocking confirm() dialog, which would
// stall the mjpeg preview and the state/log polling while it is open.
let confirmCallback = null;
function confirmAction(title, text, onYes) {
    const t = $("confirm-modal-title"), b = $("confirm-modal-text");
    if (t) t.textContent = title;
    if (b) b.textContent = text;
    confirmCallback = onYes;
    openModal($("confirm-modal"));
}
on($("confirm-modal-yes"), "click", () => {
    const cb = confirmCallback;
    confirmCallback = null;
    closeModal($("confirm-modal"));
    if (cb) cb();
});

function openImageModal(f) {
    const img = $("image-modal-img"), title = $("image-modal-title");
    if (img) img.src = "/api/file?path=" + encodeURIComponent(f.path) + "&token=" + encodeURIComponent(API_TOKEN);
    if (title) title.textContent = f.name;
    state.modalImagePath = f.path;
    openModal($("image-modal"));
}
on($("image-modal-open"), "click", async () => {
    if (!state.modalImagePath) return;
    try { await api("/api/open", { method: "POST", body: { target: "path", path: state.modalImagePath } }); }
    catch (e) { showToast("could not open: " + e.message, false); }
});
)JS"
        R"JS(
document.querySelectorAll(".section-header").forEach(h => {
    const card = h.closest(".section-card");
    const key = card ? card.dataset.section : null;
    if (key) {
        try { if (localStorage.getItem("sf-collapsed-" + key) === "1") card.classList.add("collapsed"); }
        catch (e) { /* storage unavailable - sections just stay expanded */ }
    }
    on(h, "click", () => {
        if (!card) return;
        card.classList.toggle("collapsed");
        if (key) {
            try { localStorage.setItem("sf-collapsed-" + key, card.classList.contains("collapsed") ? "1" : "0"); }
            catch (e) { /* nothing to persist to */ }
        }
    });
});

on($("btn-connect"), "click", async () => {
    const s = state.server;
    if (!s) return;
    try {
        if (s.mode === "idle") await api("/api/connect", { method: "POST", body: {} });
        else await api("/api/disconnect", { method: "POST", body: {} });
        pollStateOnce();
    } catch (e) { showToast(e.message, false); }
});
on($("btn-screenshot"), "click", async () => {
    try { await api("/api/snapshot", { method: "POST", body: {} }); pollStateOnce(); }
    catch (e) { showToast(e.message, false); }
});
on($("btn-stream"), "click", async () => {
    const s = state.server;
    if (!s) return;
    try {
        if (s.mode === "streaming") await api("/api/stream/stop", { method: "POST", body: {} });
        else await api("/api/stream/start", { method: "POST", body: {} });
        pollStateOnce();
    } catch (e) { showToast(e.message, false); }
});
on($("btn-quit"), "click", () => {
    const keepsAgent = !!(state.server && state.server.options && state.server.options.keepAgent);
    confirmAction("Quit ScreenFuse", "This shuts the controller down" + (keepsAgent ? "." : " and disconnects the agent."), async () => {
        try { await api("/api/quit", { method: "POST", body: {} }); }
        catch (e) { showToast(e.message, false); }
    });
});

on($("btn-save-frame"), "click", async () => {
    try { await api("/api/stream/still", { method: "POST", body: {} }); showToast("frame saved", true); fetchCaptures(); }
    catch (e) { showToast(e.message, false); }
});
on($("btn-record-toggle"), "click", async () => {
    try { await api("/api/record/toggle", { method: "POST", body: {} }); pollStateOnce(); }
    catch (e) { showToast(e.message, false); }
});

async function doGenKey() {
    try {
        const res = await api("/api/genkey", { method: "POST", body: {} });
        showToast("key written to " + res.path, true);
        pollStateOnce();
    } catch (e) { showToast(e.message, false); }
}
on($("btn-genkey"), "click", doGenKey);
on($("btn-genkey-banner"), "click", doGenKey);

async function pickInto(kind, purpose, inputId) {
    try {
        const res = await api("/api/pick", { method: "POST", body: { kind: kind, purpose: purpose } });
        if (res && res.path) {
            const el = $(inputId);
            if (el) el.value = res.path;
            if (inputId === "opt-keypath" || inputId === "opt-outdir") scheduleOptionsPost();
        }
    } catch (e) { showToast(e.message, false); }
}
on($("btn-browse-key"), "click", () => pickInto("file", "key", "opt-keypath"));
on($("btn-browse-out"), "click", () => pickInto("folder", "out", "opt-outdir"));
on($("btn-browse-remerge-remote"), "click", () => pickInto("file", "remoteFrame", "remerge-remote"));
on($("btn-browse-remerge-overlay"), "click", () => pickInto("file", "overlayFrame", "remerge-overlay"));

on($("btn-open-out"), "click", async () => {
    const el = $("opt-outdir");
    try { await api("/api/open", { method: "POST", body: { target: "outdir", path: el ? el.value : "" } }); }
    catch (e) { showToast(e.message, false); }
});
on($("btn-refresh-state"), "click", () => pollStateOnce());

on($("btn-remerge"), "click", async () => {
    const remote = ($("remerge-remote") || {}).value || "";
    const overlay = ($("remerge-overlay") || {}).value || "";
    if (!remote || !overlay) { showToast("pick both a remote and an overlay frame first", false); return; }
    const box = $("remerge-result");
    try {
        const res = await api("/api/remerge", { method: "POST", body: { remote: remote, overlay: overlay } });
        if (box) { box.textContent = "merged: " + res.path; box.classList.remove("hidden"); }
        showToast("re-merged", true);
        fetchCaptures();
    } catch (e) { showToast(e.message, false); }
});

on($("btn-log-pause"), "click", () => {
    state.logPaused = !state.logPaused;
    const btn = $("btn-log-pause");
    if (btn) btn.textContent = state.logPaused ? "Resume" : "Pause";
});
on($("btn-log-clear"), "click", () => { const panel = $("log-panel"); if (panel) panel.innerHTML = ""; });
)JS"
        R"JS(
function setupTooltips() {
    const tip = document.createElement("div");
    tip.className = "app-tooltip hidden";
    document.body.appendChild(tip);

    function show(target) {
        const text = target.dataset.tip;
        if (!text) return;
        tip.textContent = text;
        tip.classList.remove("hidden");
        const r = target.getBoundingClientRect();
        let left = r.left + r.width / 2;
        left = Math.max(70, Math.min(left, window.innerWidth - 70));
        tip.style.left = left + "px";
        if (r.top > 64) { tip.style.top = (r.top - 8) + "px"; tip.classList.remove("below"); }
        else { tip.style.top = (r.bottom + 8) + "px"; tip.classList.add("below"); }
    }
    function hide() { tip.classList.add("hidden"); }

    document.addEventListener("mouseover", e => { const t = e.target.closest && e.target.closest("[data-tip]"); if (t) show(t); });
    document.addEventListener("mouseout", e => { const t = e.target.closest && e.target.closest("[data-tip]"); if (t) hide(); });
    document.addEventListener("focusin", e => { const t = e.target.closest && e.target.closest("[data-tip]"); if (t) show(t); });
    document.addEventListener("focusout", hide);
    window.addEventListener("scroll", hide, true);
    window.addEventListener("resize", hide);
}

function appendLogLines(lines) {
    const panel = $("log-panel");
    if (!panel) return;
    const atBottom = Math.abs(panel.scrollHeight - panel.clientHeight - panel.scrollTop) < 12;
    for (const l of lines) {
        const div = document.createElement("div");
        div.className = "log-line log-" + String(l.level || "info").toLowerCase();
        div.textContent = "[" + l.time + "] " + l.level + " " + l.text;
        panel.appendChild(div);
    }
    while (panel.childNodes.length > 2000) panel.removeChild(panel.firstChild);
    if (atBottom) panel.scrollTop = panel.scrollHeight;
}

let logPollTimer = null;
let logPollDelay = 1000;
async function pollLogOnce() {
    if (!state.logPaused) {
        try {
            const data = await api("/api/log?since=" + state.logSeq);
            if (data && Array.isArray(data.lines) && data.lines.length) {
                appendLogLines(data.lines);
                state.logSeq = data.seq;
            }
            logPollDelay = 1000;
        } catch (e) {
            logPollDelay = 5000;
        }
    }
    if (logPollTimer) clearTimeout(logPollTimer);
    logPollTimer = setTimeout(pollLogOnce, logPollDelay);
}

function renderCaptures() {
    const grid = $("captures-grid");
    if (!grid) return;
    grid.innerHTML = "";
    const files = state.captures.slice().sort((a, b) => b.modifiedMs - a.modifiedMs);
    if (files.length === 0) {
        const empty = document.createElement("span");
        empty.className = "muted-text";
        empty.textContent = "no captures yet";
        grid.appendChild(empty);
        return;
    }
    for (const f of files) {
        const div = document.createElement("div");
        div.className = "thumb";
        div.title = f.name;
        const img = document.createElement("img");
        img.src = "/api/file?path=" + encodeURIComponent(f.path) + "&token=" + encodeURIComponent(API_TOKEN);
        img.alt = f.name;
        img.loading = "lazy";
        div.appendChild(img);
        const kind = document.createElement("span");
        kind.className = "thumb-kind";
        kind.textContent = f.kind || "";
        div.appendChild(kind);
        div.addEventListener("click", () => openImageModal(f));
        grid.appendChild(div);
    }
}

async function fetchCaptures() {
    try {
        const data = await api("/api/captures");
        state.captures = (data && data.files) || [];
        renderCaptures();
    } catch (e) { /* supplemental - the rest of the panel still works */ }
}

let lastCapturesFetch = 0;
function maybeFetchCaptures() {
    const now = Date.now();
    if (now - lastCapturesFetch > 5000) {
        lastCapturesFetch = now;
        fetchCaptures();
    }
}
)JS"
        R"JS(
let statePollTimer = null;
let statePollDelay = 1000;
async function pollStateOnce() {
    try {
        state.server = await api("/api/state");
        hideErrorBanner();
        statePollDelay = 1000;
        render();
        maybeFetchCaptures();
    } catch (e) {
        showErrorBanner("state: " + e.message);
        statePollDelay = 5000;
    } finally {
        if (statePollTimer) clearTimeout(statePollTimer);
        statePollTimer = setTimeout(pollStateOnce, statePollDelay);
    }
}

async function loadHelp() {
    try {
        state.help = await api("/api/help");
        applyHelpTooltips();
    } catch (e) { /* tooltips just fall back to the flag name */ }
}

async function init() {
    setupTooltips();
    wireOptionControls();
    await loadHelp();
    await pollStateOnce();
    fetchCaptures();
    pollLogOnce();
}

init();
)JS";
    return text;
}

} // namespace gui
} // namespace screenfuse
