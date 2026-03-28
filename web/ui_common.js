export const $ = (sel) => document.querySelector(sel);
export const $$ = (sel) => document.querySelectorAll(sel);

export function clearChildren(node) {
    while (node.firstChild) {
        node.removeChild(node.firstChild);
    }
}

export function text(value, fallback) {
    if (value === undefined || value === null || value === "") {
        return fallback || "--";
    }
    return String(value);
}

export function formatTimeMs(epochMs) {
    if (!epochMs) {
        return "--";
    }
    const d = new Date(Number(epochMs));
    return d.toISOString().replace("T", " ").replace("Z", "");
}

export function formatUptime(s) {
    if (s === undefined || s === null) {
        return "--";
    }
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = Math.floor(s % 60);
    if (d > 0) {
        return d + "d " + h + "h";
    }
    if (h > 0) {
        return h + "h " + m + "m";
    }
    return m + "m " + sec + "s";
}

export function toBool(value) {
    return !!value;
}

export function setHiddenIfPresent(sel, hidden) {
    const el = $(sel);
    if (el) {
        el.hidden = hidden;
    }
}

export function badgeClassByState(value) {
    const v = String(value || "").toLowerCase();
    if (v.includes("disconnect") || v.includes("error") || v.includes("fail") || v.includes("down")) {
        return "badge badge-error";
    }
    if (v.includes("warn") || v.includes("provisioning") || v.includes("idle")) {
        return "badge badge-warning";
    }
    if (v.includes("ok") || v === "connected" || v.includes("healthy") || v === "up") {
        return "badge badge-ok";
    }
    return "badge badge-muted";
}

export function kvRow(key, value) {
    const row = document.createElement("div");
    row.className = "kv-item";
    const k = document.createElement("span");
    k.textContent = key;
    const v = document.createElement("span");
    v.textContent = text(value);
    row.appendChild(k);
    row.appendChild(v);
    return row;
}

export function statCard(title, value, badgeKind) {
    const div = document.createElement("div");
    div.className = "card stat-card";
    const h3 = document.createElement("h3");
    h3.textContent = title;
    const val = document.createElement("div");
    val.className = "stat-value";
    val.textContent = text(value);
    if (badgeKind) {
        val.className = badgeKind;
    }
    div.appendChild(h3);
    div.appendChild(val);
    return div;
}

export function appendKvRows(container, rows) {
    rows.forEach((row) => container.appendChild(kvRow(row[0], row[1])));
}
