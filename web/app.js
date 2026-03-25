(function () {
    "use strict";

    let token = localStorage.getItem("wg_token") || "";
    let currentPage = "dashboard";
    let cacheStatus = null;
    let cacheWatchlist = [];
    let refreshTimer = null;
    let bootstrapInfo = null;

    const $ = (sel) => document.querySelector(sel);
    const $$ = (sel) => document.querySelectorAll(sel);

    function clearChildren(node) {
        while (node.firstChild) {
            node.removeChild(node.firstChild);
        }
    }

    function text(value, fallback) {
        if (value === undefined || value === null || value === "") {
            return fallback || "--";
        }
        return String(value);
    }

    function formatTimeMs(epochMs) {
        if (!epochMs) {
            return "--";
        }
        const d = new Date(Number(epochMs));
        return d.toISOString().replace("T", " ").replace("Z", "");
    }

    function formatUptime(s) {
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

    function setMsg(el, kind, message) {
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

    function toBool(value) {
        return !!value;
    }

    function badgeClassByState(value) {
        const v = String(value || "").toLowerCase();
        if (v.includes("ok") || v.includes("connected") || v.includes("healthy") || v === "up") {
            return "badge badge-ok";
        }
        if (v.includes("warn") || v.includes("provisioning") || v.includes("idle")) {
            return "badge badge-warning";
        }
        if (v.includes("error") || v.includes("fail") || v.includes("down") || v.includes("disconnected")) {
            return "badge badge-error";
        }
        return "badge badge-muted";
    }

    function kvRow(key, value) {
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

    function statCard(title, value, badgeKind) {
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

    function api(method, path, body) {
        const opts = { method: method, headers: {} };
        if (token) {
            opts.headers.Authorization = "Bearer " + token;
        }
        if (body) {
            opts.headers["Content-Type"] = "application/json";
            opts.body = JSON.stringify(body);
        }
        return fetch(path, opts).then((r) => {
            if (r.status === 401) {
                showLogin();
                throw new Error("unauthorized");
            }
            return r.text().then((txt) => {
                let data = {};
                try {
                    data = txt ? JSON.parse(txt) : {};
                } catch (_) {
                    data = {};
                }
                if (!r.ok) {
                    const e = new Error(data.error || ("http_" + r.status));
                    e.status = r.status;
                    e.data = data;
                    throw e;
                }
                return data;
            });
        });
    }

    function showLogin() {
        if (refreshTimer) {
            clearInterval(refreshTimer);
            refreshTimer = null;
        }
        token = "";
        localStorage.removeItem("wg_token");
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        if (bootstrapInfo && bootstrapInfo.provisioning && !bootstrapInfo.password_set) {
            showSetupScreen();
        } else {
            showSignInScreen();
        }
    }

    function showSignInScreen() {
        $("#login-form").hidden = false;
        $("#setup-form").hidden = true;
        if (bootstrapInfo && bootstrapInfo.provisioning) {
            $("#login-subtitle").textContent =
                "Provisioning mode detected. Sign in with the configured admin password.";
        } else {
            $("#login-subtitle").textContent = "Sign in to manage your device.";
        }
    }

    function showSetupScreen() {
        $("#login-form").hidden = true;
        $("#setup-form").hidden = false;
        $("#login-subtitle").textContent =
            "Initial setup required. Configure Wi-Fi and admin password.";
    }

    function showApp() {
        $("#login-page").hidden = true;
        $("#app-shell").hidden = false;
        showPage(currentPage);
        startRefresh();
    }

    function showPage(name) {
        currentPage = name;
        $$(".page").forEach((p) => {
            if (p.id !== "login-page") {
                p.hidden = true;
            }
        });
        const page = $("#" + name + "-page");
        if (page) {
            page.hidden = false;
        }
        $$(".nav-btn[data-page]").forEach((b) => {
            b.classList.toggle("active", b.dataset.page === name);
        });
        $("#sidebar").classList.remove("open");
        loadPage(name);
    }

    function loadPage(name) {
        if (name === "dashboard") {
            loadDashboard();
        } else if (name === "telegrams") {
            loadTelegrams();
        } else if (name === "meters") {
            loadDetectedMeters();
        } else if (name === "watchlist") {
            loadWatchlist();
        } else if (name === "diagnostics") {
            loadDiagnostics();
        } else if (name === "logs") {
            loadLogs();
        } else if (name === "ota") {
            loadOtaStatus();
        } else if (name === "settings") {
            loadConfig();
        } else if (name === "support") {
            loadSupport();
        } else if (name === "factory-reset") {
            $("#factory-msg").hidden = true;
        }
    }

    function applyModeUi(mode) {
        const badge = $("#topbar-mode");
        badge.textContent = "mode: " + text(mode, "--");
        badge.className = badgeClassByState(mode);
        const subtitle = $("#login-subtitle");
        if (String(mode).toLowerCase() === "provisioning") {
            subtitle.textContent =
                "Provisioning mode detected. Sign in and open Settings to complete first-time setup.";
        } else {
            subtitle.textContent = "Sign in to manage your device.";
        }
    }

    function applySetupMqttEnabled(enabled) {
        ["#setup-mqtt-host", "#setup-mqtt-port", "#setup-mqtt-user", "#setup-mqtt-password"].forEach(
            (sel) => {
                const el = $(sel);
                el.disabled = !enabled;
            }
        );
    }

    function bootstrap() {
        return fetch("/api/bootstrap")
            .then((r) => {
                if (!r.ok) {
                    throw new Error("bootstrap_failed");
                }
                return r.json();
            })
            .then((data) => {
                bootstrapInfo = {
                    mode: data.mode || "unknown",
                    provisioning: toBool(data.provisioning),
                    password_set: toBool(data.password_set),
                };
                return bootstrapInfo;
            })
            .catch(() => {
                bootstrapInfo = { mode: "unknown", provisioning: false, password_set: true };
                return bootstrapInfo;
            });
    }

    function runInitialSetup() {
        const msg = $("#setup-msg");
        msg.hidden = true;

        const ssid = $("#setup-ssid").value.trim();
        const wifiPassword = $("#setup-wifi-password").value;
        const adminPassword = $("#setup-admin-password").value;
        const mqttEnabled = $("#setup-mqtt-enabled").checked;
        const mqttHost = $("#setup-mqtt-host").value.trim();

        if (!ssid) {
            setMsg(msg, "error", "Wi-Fi SSID is required.");
            return Promise.resolve();
        }
        if (!adminPassword) {
            setMsg(msg, "error", "Admin password is required.");
            return Promise.resolve();
        }
        if (mqttEnabled && !mqttHost) {
            setMsg(msg, "error", "MQTT host is required when MQTT is enabled.");
            return Promise.resolve();
        }

        setMsg(msg, "warning", "Saving initial setup...");

        return fetch("/api/auth/login", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ password: adminPassword }),
        })
            .then((r) =>
                r.text().then((txt) => {
                    let data = {};
                    try {
                        data = txt ? JSON.parse(txt) : {};
                    } catch (_) {
                        data = {};
                    }
                    if (!r.ok || !data.token) {
                        throw new Error(data.error || "setup_login_failed");
                    }
                    return data;
                })
            )
            .then((loginData) => {
                token = loginData.token;
                localStorage.setItem("wg_token", token);
                return api("GET", "/api/config");
            })
            .then((cfg) => {
                cfg.device = cfg.device || {};
                cfg.wifi = cfg.wifi || {};
                cfg.auth = cfg.auth || {};
                cfg.mqtt = cfg.mqtt || {};

                cfg.device.name = $("#setup-device-name").value.trim() || cfg.device.name || "";
                cfg.device.hostname = $("#setup-hostname").value.trim() || cfg.device.hostname || "";
                cfg.wifi.ssid = ssid;
                cfg.wifi.password = wifiPassword;
                cfg.auth.admin_password = adminPassword;

                cfg.mqtt.enabled = mqttEnabled;
                if (mqttEnabled) {
                    cfg.mqtt.host = mqttHost;
                    const portValue = Number($("#setup-mqtt-port").value);
                    if (!Number.isNaN(portValue) && portValue > 0) {
                        cfg.mqtt.port = portValue;
                    }
                    cfg.mqtt.username = $("#setup-mqtt-user").value.trim();
                    cfg.mqtt.password = $("#setup-mqtt-password").value;
                }

                return api("POST", "/api/config", cfg);
            })
            .then(() => {
                setMsg(
                    msg,
                    "success",
                    "Initial setup saved. Reboot is required. After reboot, use normal admin login."
                );
            })
            .catch((err) => {
                setMsg(msg, "error", "Initial setup failed: " + (err.message || "unknown_error"));
            });
    }

    function loadDashboard() {
        Promise.all([
            api("GET", "/api/status"),
            api("GET", "/api/meters/detected"),
            api("GET", "/api/watchlist"),
        ])
            .then(([status, metersData, watchlistData]) => {
                cacheStatus = status;
                cacheWatchlist = watchlistData.watchlist || [];
                applyModeUi(status.mode);

                const health = status.health || {};
                const metrics = status.metrics || {};
                const wifi = status.wifi || {};
                const mqtt = status.mqtt || {};
                const radio = status.radio || {};
                const meters = metersData.meters || [];
                const detected = meters.length;
                const watchCount = (watchlistData.watchlist || []).length;
                const duplicateCount = meters.reduce(
                    (sum, m) => sum + Number(m.duplicate_count || 0),
                    0
                );

                const statusGrid = $("#dashboard-status-grid");
                clearChildren(statusGrid);
                statusGrid.appendChild(
                    statCard("Health", text(health.state), badgeClassByState(health.state))
                );
                statusGrid.appendChild(
                    statCard("Wi-Fi", text(wifi.state), badgeClassByState(wifi.state))
                );
                statusGrid.appendChild(
                    statCard("MQTT", text(mqtt.state), badgeClassByState(mqtt.state))
                );
                statusGrid.appendChild(
                    statCard("Radio", text(radio.state), badgeClassByState(radio.state))
                );
                statusGrid.appendChild(statCard("Mode", status.mode || "--"));
                statusGrid.appendChild(statCard("Firmware", status.firmware_version || "--"));

                const metricGrid = $("#dashboard-metrics-grid");
                clearChildren(metricGrid);
                metricGrid.appendChild(statCard("Uptime", formatUptime(metrics.uptime_s)));
                metricGrid.appendChild(statCard("Frames Received", radio.frames_received || 0));
                metricGrid.appendChild(statCard("CRC Fail", radio.frames_crc_fail || 0));
                metricGrid.appendChild(statCard("Duplicates", duplicateCount));
                metricGrid.appendChild(statCard("Incomplete Frames", radio.frames_incomplete || 0));
                metricGrid.appendChild(
                    statCard("Dropped Too Long", radio.frames_dropped_too_long || 0)
                );
                metricGrid.appendChild(statCard("MQTT Publish Failures", mqtt.publish_failures || 0));
                metricGrid.appendChild(statCard("Detected Meters", detected));
                metricGrid.appendChild(statCard("Watchlist", watchCount));
                metricGrid.appendChild(statCard("Wi-Fi RSSI", text(wifi.rssi_dbm, "--") + " dBm"));
                metricGrid.appendChild(
                    statCard("Free Heap", Math.round((metrics.free_heap_bytes || 0) / 1024) + " KB")
                );
                metricGrid.appendChild(statCard("IP Address", wifi.ip_address || "--"));
            })
            .catch(() => {});
    }

    function watchAliasMap() {
        const map = {};
        cacheWatchlist.forEach((e) => {
            map[e.key] = e.alias || "";
        });
        return map;
    }

    function createCell(row, value, className) {
        const td = document.createElement("td");
        if (className) {
            td.className = className;
        }
        td.textContent = text(value, "");
        row.appendChild(td);
        return td;
    }

    function addWatchlistQuickAction(key, suggestedAlias, suggestedNote) {
        $("#wl-key").value = key || "";
        $("#wl-alias").value = suggestedAlias || "";
        $("#wl-note").value = suggestedNote || "";
        $("#wl-enabled").checked = true;
        showPage("watchlist");
    }

    function loadTelegrams() {
        const filter = $("#tg-filter").value || "all";
        const apiFilter = filter === "problematic" ? "crc_fail" : filter;
        const suffix = apiFilter === "all" ? "" : ("?filter=" + encodeURIComponent(apiFilter));
        Promise.all([api("GET", "/api/telegrams" + suffix), api("GET", "/api/watchlist")])
            .then(([data, watchlist]) => {
                cacheWatchlist = watchlist.watchlist || [];
                const aliases = watchAliasMap();
                const arr = data.telegrams || [];
                const body = $("#tg-body");
                clearChildren(body);
                $("#tg-empty").hidden = arr.length > 0;

                arr.forEach((f) => {
                    const tr = document.createElement("tr");
                    createCell(tr, formatTimeMs(f.timestamp_ms));
                    createCell(tr, f.meter_key || "");
                    createCell(tr, aliases[f.meter_key] || "");
                    const raw = createCell(tr, f.raw_hex || "", "hex");
                    raw.title = f.raw_hex || "";
                    createCell(tr, f.frame_length);
                    createCell(tr, f.rssi_dbm);
                    createCell(tr, f.lqi);
                    createCell(tr, f.crc_ok ? "OK" : "FAIL");
                    createCell(tr, f.duplicate ? "YES" : "NO");
                    createCell(tr, f.watched ? "YES" : "NO");

                    const act = document.createElement("td");
                    const actions = document.createElement("div");
                    actions.className = "btn-row";

                    const copyBtn = document.createElement("button");
                    copyBtn.className = "btn btn-secondary";
                    copyBtn.textContent = "Copy";
                    copyBtn.addEventListener("click", () => {
                        navigator.clipboard.writeText(f.raw_hex || "").catch(() => {});
                    });
                    actions.appendChild(copyBtn);

                    const addBtn = document.createElement("button");
                    addBtn.className = "btn btn-primary";
                    addBtn.textContent = f.watched ? "Edit Watch" : "Add Watch";
                    addBtn.addEventListener("click", () => {
                        addWatchlistQuickAction(f.meter_key || "", aliases[f.meter_key] || "", "");
                    });
                    actions.appendChild(addBtn);
                    act.appendChild(actions);
                    tr.appendChild(act);
                    body.appendChild(tr);
                });
            })
            .catch(() => {});
    }

    function matchesMeterSearch(meter, search) {
        if (!search) {
            return true;
        }
        const s = search.toLowerCase();
        return (
            String(meter.key || "").toLowerCase().includes(s) ||
            String(meter.alias || "").toLowerCase().includes(s)
        );
    }

    function loadDetectedMeters() {
        const filter = $("#meters-filter").value || "all";
        const suffix = filter === "all" ? "" : ("?filter=" + encodeURIComponent(filter));
        const search = ($("#meters-search").value || "").trim();
        api("GET", "/api/meters/detected" + suffix)
            .then((data) => {
                const arr = (data.meters || []).filter((m) => matchesMeterSearch(m, search));
                const body = $("#meters-body");
                clearChildren(body);
                $("#meters-empty").hidden = arr.length > 0;
                arr.forEach((m) => {
                    const tr = document.createElement("tr");
                    createCell(tr, m.alias || "");
                    createCell(tr, m.key || "", "hex");
                    createCell(tr, m.manufacturer_id || "--");
                    createCell(tr, m.device_id || "--");
                    createCell(tr, formatTimeMs(m.first_seen_ms));
                    createCell(tr, formatTimeMs(m.last_seen_ms));
                    createCell(tr, m.seen_count || 0);
                    createCell(tr, text(m.last_rssi_dbm, "--") + " / " + text(m.last_lqi, "--"));
                    createCell(
                        tr,
                        m.watch_enabled ? "ENABLED" : m.watched ? "DISABLED" : "NO"
                    );
                    createCell(tr, m.note || "");

                    const act = document.createElement("td");
                    const row = document.createElement("div");
                    row.className = "btn-row";
                    const watchBtn = document.createElement("button");
                    watchBtn.className = "btn btn-primary";
                    watchBtn.textContent = m.watched ? "Edit Watch" : "Add";
                    watchBtn.addEventListener("click", () => {
                        addWatchlistQuickAction(m.key, m.alias || "", m.note || "");
                    });
                    row.appendChild(watchBtn);
                    act.appendChild(row);
                    tr.appendChild(act);
                    body.appendChild(tr);
                });
            })
            .catch(() => {});
    }

    function loadWatchlist() {
        api("GET", "/api/watchlist")
            .then((data) => {
                cacheWatchlist = data.watchlist || [];
                const body = $("#watchlist-body");
                clearChildren(body);
                $("#watchlist-empty").hidden = cacheWatchlist.length > 0;
                cacheWatchlist.forEach((w) => {
                    const tr = document.createElement("tr");
                    createCell(tr, w.enabled ? "YES" : "NO");
                    createCell(tr, w.alias || "");
                    createCell(tr, w.key || "", "hex");
                    createCell(tr, w.note || "");
                    tr.addEventListener("click", () => {
                        $("#wl-key").value = w.key || "";
                        $("#wl-alias").value = w.alias || "";
                        $("#wl-note").value = w.note || "";
                        $("#wl-enabled").checked = !!w.enabled;
                    });
                    body.appendChild(tr);
                });
            })
            .catch(() => {});
    }

    function loadDiagnostics() {
        Promise.all([api("GET", "/api/diagnostics/radio"), api("GET", "/api/diagnostics/mqtt"), api("GET", "/api/status"), api("GET", "/api/ota/status")])
            .then(([radioData, mqttData, statusData, otaData]) => {
                const radioEl = $("#rf-stats");
                const mqttEl = $("#mqtt-stats");
                const sysEl = $("#sys-stats");
                const otaEl = $("#diag-ota-stats");
                clearChildren(radioEl);
                clearChildren(mqttEl);
                clearChildren(sysEl);
                clearChildren(otaEl);

                const rsm = radioData.rsm || {};
                const diag = radioData.diagnostics || {};
                const rc = diag.radio_counters || {};
                [
                    ["RSM State", rsm.state],
                    ["Consecutive Errors", rsm.consecutive_errors],
                    ["Radio State", diag.radio_state],
                    ["Frames Received", rc.frames_received],
                    ["CRC OK", rc.frames_crc_ok],
                    ["CRC Fail", rc.frames_crc_fail],
                    ["Incomplete", rc.frames_incomplete],
                    ["Dropped Too Long", rc.frames_dropped_too_long],
                    ["FIFO Overflows", rc.fifo_overflows],
                    ["SPI Errors", rc.spi_errors],
                ].forEach((entry) => radioEl.appendChild(kvRow(entry[0], entry[1])));

                [
                    ["State", mqttData.state],
                    ["Broker", mqttData.broker_uri],
                    ["Publish Count", mqttData.publish_count],
                    ["Publish Failures", mqttData.publish_failures],
                    ["Reconnects", mqttData.reconnect_count],
                    ["Last Publish (ms)", mqttData.last_publish_epoch_ms],
                ].forEach((entry) => mqttEl.appendChild(kvRow(entry[0], entry[1])));

                const health = statusData.health || {};
                const metrics = statusData.metrics || {};
                const wifi = statusData.wifi || {};
                [
                    ["Health", health.state],
                    ["Uptime", formatUptime(metrics.uptime_s)],
                    ["Heap Free", metrics.free_heap_bytes],
                    ["Heap Min", metrics.min_free_heap_bytes],
                    ["Largest Block", metrics.largest_free_block],
                    ["Wi-Fi State", wifi.state],
                    ["Wi-Fi IP", wifi.ip_address],
                    ["Wi-Fi RSSI", wifi.rssi_dbm],
                ].forEach((entry) => sysEl.appendChild(kvRow(entry[0], entry[1])));

                [
                    ["State", otaData.state],
                    ["Progress", text(otaData.progress_pct, "0") + "%"],
                    ["Message", otaData.message],
                    ["Version", otaData.current_version],
                ].forEach((entry) => otaEl.appendChild(kvRow(entry[0], entry[1])));
            })
            .catch(() => {});
    }

    function renderConfigSection(container, sectionName, obj) {
        const card = document.createElement("div");
        card.className = "card";
        const title = document.createElement("h3");
        title.textContent = sectionName;
        card.appendChild(title);
        Object.keys(obj).forEach((key) => {
            const value = obj[key];
            const id = "cfg-" + sectionName.toLowerCase() + "-" + key;
            if (key === "password_set") {
                const row = document.createElement("div");
                row.className = "kv-item";
                const left = document.createElement("span");
                left.textContent = key;
                const right = document.createElement("span");
                right.textContent = value ? "yes" : "no";
                row.appendChild(left);
                row.appendChild(right);
                card.appendChild(row);
                return;
            }
            const label = document.createElement("label");
            label.setAttribute("for", id);
            label.textContent = key;
            const input = document.createElement("input");
            input.id = id;
            if (typeof value === "boolean") {
                input.type = "checkbox";
                input.checked = value;
            } else if (typeof value === "number") {
                input.type = "number";
                input.value = String(value);
            } else {
                input.type = "text";
                input.value = value === undefined || value === null ? "" : String(value);
            }
            label.appendChild(input);
            card.appendChild(label);
        });
        container.appendChild(card);
    }

    function loadConfig() {
        api("GET", "/api/config")
            .then((cfg) => {
                const c = $("#config-form-container");
                clearChildren(c);
                if (
                    cacheStatus &&
                    String(cacheStatus.mode || "").toLowerCase() === "provisioning"
                ) {
                    const onboarding = document.createElement("div");
                    onboarding.className = "card";
                    const h = document.createElement("h3");
                    h.textContent = "Provisioning Checklist";
                    const p = document.createElement("p");
                    p.className = "hint";
                    p.textContent =
                        "Complete Wi-Fi, admin password, and MQTT settings. Save settings and reboot to enter normal mode.";
                    onboarding.appendChild(h);
                    onboarding.appendChild(p);
                    c.appendChild(onboarding);
                }
                renderConfigSection(c, "Device", cfg.device || {});
                renderConfigSection(c, "WiFi", cfg.wifi || {});
                renderConfigSection(c, "MQTT", cfg.mqtt || {});
                renderConfigSection(c, "Radio", cfg.radio || {});
                renderConfigSection(c, "Auth", cfg.auth || {});
                renderConfigSection(c, "Logging", cfg.logging || {});
            })
            .catch(() => {});
    }

    function collectConfigValues(cfg) {
        ["device", "wifi", "mqtt", "radio", "auth", "logging"].forEach((section) => {
            if (!cfg[section]) {
                return;
            }
            Object.keys(cfg[section]).forEach((key) => {
                if (key === "password_set") {
                    return;
                }
                const el = $("#cfg-" + section + "-" + key);
                if (!el) {
                    return;
                }
                if (el.type === "checkbox") {
                    cfg[section][key] = el.checked;
                } else if (el.type === "number") {
                    cfg[section][key] = Number(el.value);
                } else if (el.value !== "***") {
                    cfg[section][key] = el.value;
                }
            });
        });
    }

    function loadOtaStatus() {
        api("GET", "/api/ota/status")
            .then((d) => {
                const otaGrid = $("#ota-state-grid");
                clearChildren(otaGrid);
                otaGrid.appendChild(kvRow("State", d.state));
                otaGrid.appendChild(kvRow("Progress", text(d.progress_pct, "0") + "%"));
                otaGrid.appendChild(kvRow("Current Version", d.current_version));
                otaGrid.appendChild(kvRow("Message", d.message));
                $("#ota-status").textContent = "Status: " + text(d.state);
            })
            .catch(() => {});
    }

    function loadSupport() {
        const summary = $("#support-summary");
        clearChildren(summary);
        Promise.all([api("GET", "/api/status"), api("GET", "/api/ota/status"), api("GET", "/api/watchlist")])
            .then(([status, ota, watch]) => {
                summary.appendChild(kvRow("Firmware", status.firmware_version));
                summary.appendChild(kvRow("Mode", status.mode));
                summary.appendChild(kvRow("Health", status.health ? status.health.state : "--"));
                summary.appendChild(kvRow("Watchlist Entries", (watch.watchlist || []).length));
                summary.appendChild(kvRow("OTA State", ota.state));
            })
            .catch(() => {});
    }

    function loadLogs() {
        api("GET", "/api/logs")
            .then((data) => {
                const lines = data.entries || [];
                const filter = $("#log-filter").value;
                const filtered = filter ? lines.filter((l) => l.severity === filter) : lines;
                const output = filtered
                    .map((l) => {
                        const ts = l.timestamp_us
                            ? new Date(Math.floor(l.timestamp_us / 1000)).toISOString()
                            : "";
                        return (
                            "[" + text(l.severity, "?").toUpperCase().padEnd(7) + "] " + ts + " " + text(l.message, "")
                        );
                    })
                    .join("\n");
                $("#log-output").textContent = output || "No logs.";
                $("#logs-empty").hidden = filtered.length > 0;
            })
            .catch(() => {});
    }

    function startRefresh() {
        if (refreshTimer) {
            clearInterval(refreshTimer);
        }
        refreshTimer = setInterval(() => {
            loadPage(currentPage);
        }, 10000);
    }

    function bindEvents() {
        $$(".nav-btn[data-page]").forEach((btn) => {
            btn.addEventListener("click", () => showPage(btn.dataset.page));
        });

        $("#menu-toggle").addEventListener("click", () => {
            $("#sidebar").classList.toggle("open");
        });

        $("#btn-logout").addEventListener("click", () => {
            api("POST", "/api/auth/logout").catch(() => {});
            showLogin();
        });

        $("#login-form").addEventListener("submit", (e) => {
            e.preventDefault();
            const pwd = $("#login-pwd").value;
            fetch("/api/auth/login", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ password: pwd }),
            })
                .then((r) =>
                    r.text().then((txt) => {
                        let data = {};
                        try {
                            data = txt ? JSON.parse(txt) : {};
                        } catch (_) {
                            data = {};
                        }
                        if (!r.ok) {
                            const retry = data.retry_after_s
                                ? " Try again in " + data.retry_after_s + "s."
                                : "";
                            throw new Error((data.error || "Login failed") + retry);
                        }
                        return data;
                    })
                )
                .then((data) => {
                    if (!data.token) {
                        throw new Error("No auth token");
                    }
                    token = data.token;
                    localStorage.setItem("wg_token", token);
                    $("#login-error").hidden = true;
                    showApp();
                })
                .catch((err) => {
                    setMsg($("#login-error"), "error", err.message || "Login failed");
                });
        });

        $("#setup-mqtt-enabled").addEventListener("change", (e) => {
            applySetupMqttEnabled(e.target.checked);
        });

        $("#setup-form").addEventListener("submit", (e) => {
            e.preventDefault();
            runInitialSetup();
        });

        $("#cfg-save").addEventListener("click", () => {
            const msg = $("#cfg-msg");
            msg.hidden = true;
            api("GET", "/api/config")
                .then((cfg) => {
                    collectConfigValues(cfg);
                    return api("POST", "/api/config", cfg);
                })
                .then((resp) => {
                    if (resp.relogin_required) {
                        setMsg(msg, "warning", "Saved. Authentication changed, please log in again.");
                        showLogin();
                        return;
                    }
                    if (resp.reboot_required) {
                        setMsg(msg, "warning", "Saved. Reboot required to apply runtime settings.");
                    } else {
                        setMsg(msg, "success", "Settings saved.");
                    }
                })
                .catch((err) => {
                    const issues = err && err.data && err.data.issues ? err.data.issues : [];
                    if (issues.length > 0) {
                        const detail = issues
                            .map((i) => i.field + ": " + i.message)
                            .join("; ");
                        setMsg(msg, "error", "Validation failed: " + detail);
                    } else {
                        setMsg(msg, "error", "Save failed.");
                    }
                });
        });

        $("#cfg-export").addEventListener("click", () => {
            api("GET", "/api/config").then((cfg) => {
                const blob = new Blob([JSON.stringify(cfg, null, 2)], {
                    type: "application/json",
                });
                const a = document.createElement("a");
                a.href = URL.createObjectURL(blob);
                a.download = "wmbus-gw-config.json";
                a.click();
            });
        });

        $("#wl-save").addEventListener("click", () => {
            const msg = $("#wl-msg");
            msg.hidden = true;
            const key = $("#wl-key").value.trim();
            if (!key) {
                setMsg(msg, "error", "Meter key is required.");
                return;
            }
            api("POST", "/api/watchlist", {
                key: key,
                alias: $("#wl-alias").value || "",
                note: $("#wl-note").value || "",
                enabled: $("#wl-enabled").checked,
            })
                .then(() => {
                    setMsg(msg, "success", "Watchlist updated.");
                    loadWatchlist();
                    if (currentPage === "meters") {
                        loadDetectedMeters();
                    }
                })
                .catch(() => setMsg(msg, "error", "Watchlist update failed."));
        });

        $("#wl-delete").addEventListener("click", () => {
            const msg = $("#wl-msg");
            msg.hidden = true;
            const key = $("#wl-key").value.trim();
            if (!key) {
                setMsg(msg, "error", "Meter key is required.");
                return;
            }
            api("POST", "/api/watchlist/delete", { key: key })
                .then(() => {
                    setMsg(msg, "success", "Watchlist entry removed.");
                    $("#wl-key").value = "";
                    $("#wl-alias").value = "";
                    $("#wl-note").value = "";
                    $("#wl-enabled").checked = true;
                    loadWatchlist();
                    loadDetectedMeters();
                })
                .catch(() => setMsg(msg, "error", "Watchlist delete failed."));
        });

        $("#ota-upload-btn").addEventListener("click", () => {
            const file = $("#ota-file").files[0];
            if (!file) {
                setMsg($("#ota-status"), "warning", "Select a firmware file first.");
                return;
            }
            setMsg($("#ota-status"), "warning", "Uploading firmware...");
            fetch("/api/ota/upload", {
                method: "POST",
                headers: {
                    Authorization: token ? "Bearer " + token : "",
                    "Content-Type": "application/octet-stream",
                },
                body: file,
            })
                .then((r) =>
                    r.text().then((txt) => {
                        let data = {};
                        try {
                            data = txt ? JSON.parse(txt) : {};
                        } catch (_) {
                            data = {};
                        }
                        if (!r.ok) {
                            throw new Error(data.error || "upload_failed");
                        }
                        return data;
                    })
                )
                .then((resp) => {
                    const detail = resp.detail || "Upload complete. Reboot required.";
                    setMsg($("#ota-status"), "success", detail);
                    loadOtaStatus();
                })
                .catch((err) => setMsg($("#ota-status"), "error", "Upload failed: " + err.message));
        });

        $("#ota-url-btn").addEventListener("click", () => {
            const url = $("#ota-url").value.trim();
            if (!url) {
                setMsg($("#ota-status"), "warning", "Enter URL first.");
                return;
            }
            api("POST", "/api/ota/url", { url: url })
                .then(() => setMsg($("#ota-status"), "success", "OTA URL update started."))
                .catch((err) => setMsg($("#ota-status"), "error", "OTA URL failed: " + err.message));
        });

        $("#sys-bundle").addEventListener("click", () => {
            fetch("/api/support-bundle", {
                headers: {
                    Authorization: token ? "Bearer " + token : "",
                },
            })
                .then((r) => {
                    if (!r.ok) {
                        throw new Error("download_failed");
                    }
                    return r.blob();
                })
                .then((blob) => {
                    const a = document.createElement("a");
                    a.href = URL.createObjectURL(blob);
                    a.download = "support-bundle.json";
                    a.click();
                })
                .catch(() => {});
        });

        $("#sys-reboot").addEventListener("click", () => {
            if (!confirm("Reboot device now?")) {
                return;
            }
            api("POST", "/api/system/reboot")
                .then(() => setMsg($("#factory-msg"), "warning", "Reboot command sent."))
                .catch(() => setMsg($("#factory-msg"), "error", "Reboot failed."));
        });

        $("#dash-reboot").addEventListener("click", () => {
            if (confirm("Reboot device now?")) {
                api("POST", "/api/system/reboot").catch(() => {});
            }
        });

        $("#sys-reset").addEventListener("click", () => {
            if (!confirm("Factory reset will erase configuration and reboot. Continue?")) {
                return;
            }
            api("POST", "/api/system/factory-reset")
                .then(() => setMsg($("#factory-msg"), "warning", "Factory reset command sent."))
                .catch(() => setMsg($("#factory-msg"), "error", "Factory reset failed."));
        });

        $("#tg-filter").addEventListener("change", loadTelegrams);
        $("#tg-refresh").addEventListener("click", loadTelegrams);
        $("#meters-filter").addEventListener("change", loadDetectedMeters);
        $("#meters-search").addEventListener("input", loadDetectedMeters);
        $("#meters-refresh").addEventListener("click", loadDetectedMeters);
        $("#diag-refresh").addEventListener("click", loadDiagnostics);
        $("#log-filter").addEventListener("change", loadLogs);
        $("#log-refresh").addEventListener("click", loadLogs);
        $$("[data-jump]").forEach((btn) =>
            btn.addEventListener("click", () => showPage(btn.dataset.jump))
        );
    }

    bindEvents();
    applySetupMqttEnabled(false);

    bootstrap().then((boot) => {
        if (token) {
            api("GET", "/api/status")
                .then((status) => {
                    cacheStatus = status;
                    applyModeUi(status.mode);
                    showApp();
                })
                .catch(() => showLogin());
            return;
        }
        if (boot.provisioning && !boot.password_set) {
            showLogin();
            return;
        }
        showLogin();
    });
})();
