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
            return r.json();
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
            .then((r) => r.json())
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
            .catch(() => {
                $("#login-error").textContent = "Connection error";
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
            $("#d-health").textContent = d.health || "--";
            $("#d-uptime").textContent = formatUptime(d.uptime_s);
            $("#d-fw").textContent = d.firmware_version || "--";
            $("#d-ip").textContent = d.ip_address || "--";
            $("#d-frames").textContent = d.frames_received ?? "--";
            $("#d-mqttpub").textContent = d.mqtt_publishes ?? "--";
            $("#d-rssi").textContent = d.wifi_rssi_dbm ? d.wifi_rssi_dbm + " dBm" : "--";
            $("#d-heap").textContent = d.free_heap_bytes ? Math.round(d.free_heap_bytes / 1024) + " KB" : "--";
            setHealthColor($("#d-health"), d.health);
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
        api("GET", "/api/telegrams").then((data) => {
            const arr = data.frames || [];
            const body = $("#tg-body");
            body.innerHTML = "";
            $("#tg-empty").hidden = arr.length > 0;
            arr.forEach((f) => {
                const tr = document.createElement("tr");
                tr.innerHTML =
                    "<td>" + (f.timestamp || "") + "</td>" +
                    '<td class="hex" title="' + f.raw_hex + '">' + (f.raw_hex || "").substring(0, 40) + "</td>" +
                    "<td>" + (f.rssi_dbm ?? "") + "</td>" +
                    "<td>" + (f.lqi ?? "") + "</td>" +
                    "<td>" + (f.crc_ok ? "OK" : "FAIL") + "</td>" +
                    "<td>" + (f.frame_length ?? "") + "</td>";
                body.appendChild(tr);
            });
        }).catch(() => {});
    }

    // RF Diagnostics
    function loadRfDiag() {
        api("GET", "/api/diagnostics/radio").then((d) => {
            const c = $("#rf-stats");
            c.innerHTML = "";
            const fields = [
                ["Radio State", d.radio_state],
                ["Frames Received", d.frames_received],
                ["CRC OK", d.frames_crc_ok],
                ["CRC Fail", d.frames_crc_fail],
                ["FIFO Overflows", d.fifo_overflows],
                ["Radio Resets", d.radio_resets],
                ["Recoveries", d.radio_recoveries],
                ["SPI Errors", d.spi_errors],
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
            msg.textContent = resp.valid ? "Saved" : "Validation failed: " + (resp.issues || []).map((i) => i.message).join(", ");
            msg.className = resp.valid ? "success" : "error";
            msg.hidden = false;
        }).catch(() => { msg.textContent = "Save failed"; msg.className = "error"; msg.hidden = false; });
    });

    function collectConfigValues(cfg) {
        for (const section of ["device", "wifi", "mqtt", "radio", "auth", "logging"]) {
            if (!cfg[section]) continue;
            for (const k of Object.keys(cfg[section])) {
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

    // OTA
    function loadOtaStatus() {
        api("GET", "/api/ota/status").then((d) => {
            $("#ota-status").textContent = "Status: " + (d.state || "idle") +
                (d.progress_pct ? " (" + d.progress_pct + "%)" : "") +
                (d.message ? " — " + d.message : "");
        }).catch(() => {});
    }

    $("#ota-upload-btn").addEventListener("click", () => {
        const file = $("#ota-file").files[0];
        if (!file) return;
        const progress = $(".progress");
        progress.hidden = false;
        apiRaw("POST", "/api/ota/upload", file).then((r) => r.json()).then((d) => {
            $("#ota-bar").style.width = "100%";
            $("#ota-status").textContent = d.message || "Upload complete";
        }).catch(() => { $("#ota-status").textContent = "Upload failed"; });
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
            const lines = data.lines || [];
            const filter = $("#log-filter").value;
            const filtered = filter ? lines.filter((l) => l.severity === filter) : lines;
            $("#log-output").textContent = filtered.map((l) =>
                "[" + (l.severity || "?").toUpperCase().padEnd(7) + "] " + (l.message || "")
            ).join("\n") || "No logs.";
        }).catch(() => {});
    }

    $("#log-filter").addEventListener("change", loadLogs);

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
