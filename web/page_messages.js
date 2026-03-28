import { $, clearChildren, kvRow } from "./ui_common.js";

function resolveElement(target) {
    if (!target) {
        return null;
    }
    return typeof target === "string" ? $(target) : target;
}

export function setMsg(target, kind, message) {
    const el = resolveElement(target);
    if (!el) {
        return;
    }
    el.className = "msg";
    if (kind === "error") {
        el.classList.add("msg-error");
    } else if (kind === "success") {
        el.classList.add("msg-success");
    } else if (kind === "warning") {
        el.classList.add("msg-warning");
    }
    el.textContent = message;
    el.hidden = false;
}

export function clearMsg(target) {
    const el = resolveElement(target);
    if (!el) {
        return;
    }
    el.className = "msg";
    el.textContent = "";
    el.hidden = true;
}

export function errorMessage(err, fallback) {
    if (err && err.data && err.data.detail) {
        return String(err.data.detail);
    }
    if (err && err.message && !String(err.message).startsWith("http_")) {
        return String(err.message);
    }
    return fallback;
}

export function setEmptyState(selector, message, hidden) {
    const el = $(selector);
    if (!el) {
        return;
    }
    if (message !== undefined) {
        el.textContent = message;
    }
    el.hidden = hidden;
}

export function renderKvLoadError(selector, message) {
    const el = $(selector);
    if (!el) {
        return;
    }
    clearChildren(el);
    el.appendChild(kvRow("Error", message));
}
