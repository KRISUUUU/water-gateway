(function () {
    "use strict";

    let token = localStorage.getItem("wg_token") || "";

    const $ = (sel) => document.querySelector(sel);
    const $$ = (sel) => document.querySelectorAll(sel);

    function api(method, path, body) {
        const opts = { method, headers: {} };
        if (token) opts.headers["Authorization"] = "Bearer " + token;
        if (body) {
            opts.headers["Content-Type"] = "application/json";
            opts.body = JSON.stringify(body);
        }
        return fetch(path, opts).then((r) => {
            if (r.status === 401) { showLogin(); throw new Error("unauthorized"); }
            return r.text().then((text) => {
                let data = {};
                try { data = text ? JSON.parse(text) : {}; } catch (_) { data = {}; }
                if (!r.ok) {
                    const err = new Error(data.error || ("http_" + r.status));
                    err.status = r.status;
                    err.data = data;
                    throw err;
                }
                return data;
            });
        });
    }

    function apiRaw(method, path, body) {
        const opts = { method, headers: {} };
        if (token) opts.headers["Authorization"] = "Bearer " + token;
        if (body) { opts.body = body; }
        return fetch(path, opts);
    }

    // Navigation
    function showPage(name) {
        $$(".page").forEach((p) => (p.hidden = true));
        const page = $("#" + name + "-page");
        if (page) page.hidden = false;
        $$(".nav-btn").forEach((b) => b.classList.toggle("active", b.dataset.page === name));

        if (name === "dashboard") loadDashboard();
        else if (name === "telegrams") loadTelegrams();
        else if (name === "meters") loadDetectedMeters();
        else if (name === "watchlist") loadWatchlist();
        else if (name === "rf") loadRfDiag();
        else if (name === "mqtt") loadMqttDiag();
        else if (name === "config") loadConfig();
        else if (name === "ota") loadOtaStatus();
        else if (name === "logs") loadLogs();
    }

    $$(".nav-btn[data-page]").forEach((btn) => {
        btn.addEventListener("click", () => showPage(btn.dataset.page));
    });

    // Auth
    function showLogin() {
        token = "";
        localStorage.removeItem("wg_token");
        $("nav").hidden = true;
        $$(".page").forEach((p) => (p.hidden = true));
        $("#login-page").hidden = false;
    }

    function showApp() {
        $("nav").hidden = false;
        showPage("dashboard");
    }

    $("#login-form").addEventListener("submit", (e) => {
        e.preventDefault();
        const pwd = $("#login-pwd").value;
        fetch("/api/auth/login", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ password: pwd }),
        })
            .then((r) => r.text().then((text) => {
                let data = {};
                try { data = text ? JSON.parse(text) : {}; } catch (_) { data = {}; }
                if (!r.ok) {
                    const msg = data.error || (r.status === 429 ? "Too many login attempts" : "Login failed");
                    throw new Error(msg);
                }
                return data;
            }))
            .then((data) => {
                if (data.token) {
                    token = data.token;
                    localStorage.setItem("wg_token", token);
                    $("#login-error").hidden = true;
                    showApp();
                } else {
                    $("#login-error").textContent = data.error || "Login failed";
                    $("#login-error").hidden = false;
                }
            })
            .catch((err) => {
                $("#login-error").textContent = (err && err.message) ? err.message : "Connection error";
                $("#login-error").hidden = false;
            });
    });

    $("#btn-logout").addEventListener("click", () => {
        api("POST", "/api/auth/logout").catch(() => {});
        showLogin();
    });

    // Dashboard
    function loadDashboard() {
        api("GET", "/api/status").then((d) => {
            const health = d.health || {};
            const metrics = d.metrics || {};
            const wifi = d.wifi || {};
            const mqtt = d.mqtt || {};
            const radio = d.radio || {};

            $("#d-health").textContent = health.state || "--";
            $("#d-uptime").textContent = formatUptime(metrics.uptime_s);
            $("#d-fw").textContent = d.firmware_version || "--";
            $("#d-ip").textContent = wifi.ip_address || "--";
            $("#d-frames").textContent = radio.frames_received ?? "--";
            $("#d-mqttpub").textContent = mqtt.publish_count ?? "--";
            $("#d-rssi").textContent = (wifi.rssi_dbm || wifi.rssi_dbm === 0) ? (wifi.rssi_dbm + " dBm") : "--";
            $("#d-heap").textContent = metrics.free_heap_bytes ? Math.round(metrics.free_heap_bytes / 1024) + " KB" : "--";
            setHealthColor($("#d-health"), (health.state || "").toLowerCase());
        }).catch(() => {});
    }

    function setHealthColor(el, state) {
        el.className = "";
        if (state === "healthy") el.classList.add("success");
        else if (state === "warning") el.classList.add("warning");
        else if (state === "error") el.classList.add("error");
    }

    function formatUptime(s) {
        if (!s && s !== 0) return "--";
        const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600),
              m = Math.floor((s % 3600) / 60);
        return d > 0 ? d + "d " + h + "h" : h > 0 ? h + "h " + m + "m" : m + "m " + (s % 60) + "s";
    }

    // Telegrams
    function loadTelegrams() {
        const filter = ($("#tg-filter") && $("#tg-filter").value) || "all";
        const suffix = filter === "all" ? "" : ("?filter=" + encodeURIComponent(filter));
        api("GET", "/api/telegrams" + suffix).then((data) => {
            const arr = data.telegrams || data.frames || [];
            const body = $("#tg-body");
            body.innerHTML = "";
            $("#tg-empty").hidden = arr.length > 0;
            arr.forEach((f) => {
                const tr = document.createElement("tr");
                const ts = f.timestamp_ms
                    ? new Date(Number(f.timestamp_ms)).toISOString()
                    : (f.timestamp || "");
                tr.innerHTML =
                    "<td>" + ts + "</td>" +
                    "<td>" + (f.meter_key || "") + "</td>" +
                    '<td class="hex" title="' + f.raw_hex + '">' + (f.raw_hex || "").substring(0, 40) + "</td>" +
                    "<td>" + (f.rssi_dbm ?? "") + "</td>" +
                    "<td>" + (f.lqi ?? "") + "</td>" +
                    "<td>" + (f.crc_ok ? "OK" : "FAIL") + "</td>" +
                    "<td>" + (f.duplicate ? "YES" : "NO") + "</td>" +
                    "<td>" + (f.watched ? "YES" : "NO") + "</td>" +
                    "<td>" + (f.frame_length ?? "") + "</td>";
                body.appendChild(tr);
            });
        }).catch(() => {});
    }

    function loadDetectedMeters() {
        const filter = ($("#meters-filter") && $("#meters-filter").value) || "all";
        const suffix = filter === "all" ? "" : ("?filter=" + encodeURIComponent(filter));
        api("GET", "/api/meters/detected" + suffix).then((data) => {
            const arr = data.meters || [];
            const body = $("#meters-body");
            body.innerHTML = "";
            $("#meters-empty").hidden = arr.length > 0;
            arr.forEach((m) => {
                const tr = document.createElement("tr");
                tr.innerHTML =
                    "<td>" + (m.alias || "") + "</td>" +
                    '<td class="hex">' + (m.key || "") + "</td>" +
                    "<td>" + (m.seen_count ?? 0) + "</td>" +
                    "<td>" + (m.last_rssi_dbm ?? "") + "</td>" +
                    "<td>" + (m.crc_fail_count ?? 0) + "</td>" +
                    "<td>" + (m.duplicate_count ?? 0) + "</td>" +
                    "<td>" + (m.watch_enabled ? "YES" : (m.watched ? "DISABLED" : "NO")) + "</td>";
                body.appendChild(tr);
            });
        }).catch(() => {});
    }

    function loadWatchlist() {
        api("GET", "/api/watchlist").then((data) => {
            const arr = data.watchlist || [];
            const body = $("#watchlist-body");
            body.innerHTML = "";
            arr.forEach((w) => {
                const tr = document.createElement("tr");
                tr.innerHTML =
                    "<td>" + (w.enabled ? "YES" : "NO") + "</td>" +
                    "<td>" + (w.alias || "") + "</td>" +
                    '<td class="hex">' + (w.key || "") + "</td>" +
                    "<td>" + (w.note || "") + "</td>";
                tr.addEventListener("click", () => {
                    $("#wl-key").value = w.key || "";
                    $("#wl-alias").value = w.alias || "";
                    $("#wl-note").value = w.note || "";
                    $("#wl-enabled").checked = !!w.enabled;
                });
                body.appendChild(tr);
            });
        }).catch(() => {});
    }

    // RF Diagnostics
    function loadRfDiag() {
        api("GET", "/api/diagnostics/radio").then((d) => {
            const c = $("#rf-stats");
            c.innerHTML = "";
            const rsm = d.rsm || {};
            const diag = d.diagnostics || {};
            const counters = diag.radio_counters || {};
            const fields = [
                ["RSM State", rsm.state],
                ["Consecutive Errors", rsm.consecutive_errors],
                ["Radio State", diag.radio_state],
                ["Frames Received", counters.frames_received],
                ["CRC OK", counters.frames_crc_ok],
                ["CRC Fail", counters.frames_crc_fail],
                ["FIFO Overflows", counters.fifo_overflows],
                ["Radio Resets", counters.radio_resets],
                ["Recoveries", counters.radio_recoveries],
                ["SPI Errors", counters.spi_errors],
            ];
            fields.forEach(([k, v]) => {
                const div = document.createElement("div");
                div.className = "card stat-card";
                div.innerHTML = "<h3>" + k + "</h3><span>" + (v ?? "--") + "</span>";
                c.appendChild(div);
            });
        }).catch(() => {});
    }

    // MQTT Diagnostics
    function loadMqttDiag() {
        api("GET", "/api/diagnostics/mqtt").then((d) => {
            const c = $("#mqtt-stats");
            c.innerHTML = "";
            const fields = [
                ["State", d.state],
                ["Broker", d.broker_uri],
                ["Publishes", d.publish_count],
                ["Failures", d.publish_failures],
                ["Reconnects", d.reconnect_count],
            ];
            fields.forEach(([k, v]) => {
                const div = document.createElement("div");
                div.className = "card stat-card";
                div.innerHTML = "<h3>" + k + "</h3><span>" + (v ?? "--") + "</span>";
                c.appendChild(div);
            });
        }).catch(() => {});
    }

    // Config
    function loadConfig() {
        api("GET", "/api/config").then((cfg) => {
            const container = $("#config-form-container");
            container.innerHTML = "";
            renderConfigSection(container, "Device", cfg.device || {});
            renderConfigSection(container, "WiFi", cfg.wifi || {});
            renderConfigSection(container, "MQTT", cfg.mqtt || {});
            renderConfigSection(container, "Radio", cfg.radio || {});
            renderConfigSection(container, "Auth", cfg.auth || {});
            renderConfigSection(container, "Logging", cfg.logging || {});
        }).catch(() => {});
    }

    function renderConfigSection(container, title, obj) {
        const card = document.createElement("div");
        card.className = "card";
        card.style.marginBottom = "12px";
        let html = "<h3>" + title + "</h3>";
        for (const [k, v] of Object.entries(obj)) {
            if (k === "password_set") {
                html += '<label>' + k + '<input type="text" value="' + (v ? "yes" : "no") + '" disabled></label>';
                continue;
            }
            const type = typeof v === "boolean" ? "checkbox" :
                         typeof v === "number" ? "number" : "text";
            const id = "cfg-" + title.toLowerCase() + "-" + k;
            if (type === "checkbox") {
                html += '<label><input type="checkbox" id="' + id + '"' + (v ? " checked" : "") + '> ' + k + '</label>';
            } else {
                html += '<label>' + k + '<input type="' + type + '" id="' + id + '" value="' + (v ?? "") + '"></label>';
            }
        }
        card.innerHTML = html;
        container.appendChild(card);
    }

    $("#cfg-save").addEventListener("click", () => {
        const msg = $("#cfg-msg");
        msg.hidden = true;
        api("GET", "/api/config").then((orig) => {
            collectConfigValues(orig);
            return api("POST", "/api/config", orig);
        }).then((resp) => {
            if (resp && resp.ok) {
                msg.textContent = resp.reboot_required
                    ? "Configuration saved. Reboot is required to apply runtime changes."
                    : "Configuration saved.";
                msg.className = "success";
            } else {
                const issues = (resp && resp.issues) ? resp.issues : [];
                const details = issues.map((i) => i.field + ": " + i.message).join(", ");
                msg.textContent = details ? ("Validation failed: " + details) : "Validation failed";
                msg.className = "error";
            }
            msg.hidden = false;
        }).catch((err) => {
            const issues = err && err.data && err.data.issues ? err.data.issues : [];
            const details = issues.map((i) => i.field + ": " + i.message).join(", ");
            msg.textContent = details ? ("Validation failed: " + details) : "Save failed";
            msg.className = "error";
            msg.hidden = false;
        });
    });

    function collectConfigValues(cfg) {
        for (const section of ["device", "wifi", "mqtt", "radio", "auth", "logging"]) {
            if (!cfg[section]) continue;
            for (const k of Object.keys(cfg[section])) {
                if (k === "password_set") continue;
                const el = $("#cfg-" + section + "-" + k);
                if (!el) continue;
                if (el.type === "checkbox") cfg[section][k] = el.checked;
                else if (el.type === "number") cfg[section][k] = Number(el.value);
                else {
                    if (el.value !== "***") cfg[section][k] = el.value;
                }
            }
        }
    }

    $("#cfg-export").addEventListener("click", () => {
        api("GET", "/api/config").then((cfg) => {
            const blob = new Blob([JSON.stringify(cfg, null, 2)], { type: "application/json" });
            const a = document.createElement("a");
            a.href = URL.createObjectURL(blob);
            a.download = "wmbus-gw-config.json";
            a.click();
        });
    });

    $("#wl-save").addEventListener("click", () => {
        const msg = $("#wl-msg");
        msg.hidden = true;
        const key = ($("#wl-key").value || "").trim();
        if (!key) {
            msg.textContent = "Meter key is required";
            msg.className = "error";
            msg.hidden = false;
            return;
        }
        api("POST", "/api/watchlist", {
            key,
            alias: $("#wl-alias").value || "",
            note: $("#wl-note").value || "",
            enabled: $("#wl-enabled").checked
        }).then(() => {
            msg.textContent = "Watchlist updated";
            msg.className = "success";
            msg.hidden = false;
            loadWatchlist();
            loadDetectedMeters();
        }).catch(() => {
            msg.textContent = "Watchlist update failed";
            msg.className = "error";
            msg.hidden = false;
        });
    });

    $("#wl-delete").addEventListener("click", () => {
        const msg = $("#wl-msg");
        msg.hidden = true;
        const key = ($("#wl-key").value || "").trim();
        if (!key) {
            msg.textContent = "Meter key is required";
            msg.className = "error";
            msg.hidden = false;
            return;
        }
        api("POST", "/api/watchlist/delete", { key }).then(() => {
            msg.textContent = "Watchlist entry removed";
            msg.className = "success";
            msg.hidden = false;
            loadWatchlist();
            loadDetectedMeters();
        }).catch(() => {
            msg.textContent = "Watchlist delete failed";
            msg.className = "error";
            msg.hidden = false;
        });
    });

    // OTA
    function loadOtaStatus() {
        api("GET", "/api/ota/status").then((d) => {
            $("#ota-status").textContent = "Status: " + (d.state || "idle") +
                (d.progress_pct ? " (" + d.progress_pct + "%)" : "") +
                (d.message ? " — " + d.message : "") +
                (d.current_version ? (" — version " + d.current_version) : "");
        }).catch(() => {});
    }

    $("#ota-upload-btn").addEventListener("click", () => {
        const file = $("#ota-file").files[0];
        if (!file) return;
        $("#ota-status").textContent =
            "Local OTA upload endpoint is not implemented yet. Use HTTPS URL OTA.";
    });

    $("#ota-url-btn").addEventListener("click", () => {
        const url = $("#ota-url").value;
        if (!url) return;
        api("POST", "/api/ota/url", { url }).then((d) => {
            $("#ota-status").textContent = d.message || "OTA started";
        }).catch(() => { $("#ota-status").textContent = "OTA failed"; });
    });

    // System
    $("#sys-reboot").addEventListener("click", () => {
        if (confirm("Reboot device?")) api("POST", "/api/system/reboot").catch(() => {});
    });

    $("#sys-reset").addEventListener("click", () => {
        if (confirm("Factory reset? All config will be lost!")) {
            api("POST", "/api/system/factory-reset").catch(() => {});
        }
    });

    $("#sys-bundle").addEventListener("click", () => {
        fetch("/api/support-bundle", { headers: { Authorization: "Bearer " + token } })
            .then((r) => r.blob())
            .then((blob) => {
                const a = document.createElement("a");
                a.href = URL.createObjectURL(blob);
                a.download = "support-bundle.json";
                a.click();
            });
    });

    // Logs
    function loadLogs() {
        api("GET", "/api/logs").then((data) => {
            const lines = data.entries || data.lines || [];
            const filter = $("#log-filter").value;
            const filtered = filter ? lines.filter((l) => l.severity === filter) : lines;
            $("#log-output").textContent = filtered.map((l) =>
                "[" + (l.severity || "?").toUpperCase().padEnd(7) + "] " +
                (l.timestamp_us ? (new Date(Math.floor(l.timestamp_us / 1000)).toISOString() + " ") : "") +
                (l.message || "")
            ).join("\n") || "No logs.";
        }).catch(() => {});
    }

    $("#log-filter").addEventListener("change", loadLogs);
    const tgFilter = $("#tg-filter");
    if (tgFilter) {
        tgFilter.addEventListener("change", loadTelegrams);
    }
    const metersFilter = $("#meters-filter");
    if (metersFilter) {
        metersFilter.addEventListener("change", loadDetectedMeters);
    }

    // Auto-refresh
    let refreshTimer = null;
    function startRefresh() {
        if (refreshTimer) clearInterval(refreshTimer);
        refreshTimer = setInterval(() => {
            const active = document.querySelector(".nav-btn.active");
            if (active) showPage(active.dataset.page);
        }, 10000);
    }

    // Init
    if (token) {
        api("GET", "/api/status").then(() => { showApp(); startRefresh(); }).catch(() => showLogin());
    } else {
        showLogin();
    }
})();
