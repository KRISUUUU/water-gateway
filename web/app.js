(function () {
    "use strict";

    let token = localStorage.getItem("wg_token") || "";
    let currentPage = "dashboard";
    let cacheStatus = null;
    let cacheWatchlist = [];
    let refreshTimer = null;
    let heavyRefreshTimer = null;
    let otaStatusPollTimer = null;
    let otaAutoRebootTimer = null;
    let otaAutoRebootArmed = false;
    const dashboardCache = { duplicateCount: 0, detected: 0, watchlistCount: 0 };
    let bootstrapInfo = null;
    const radioSchedulerOptions = [
        { value: 0, label: "Locked", help: "Stay on one enabled radio profile." },
        { value: 1, label: "Priority", help: "Preferred profile with bounded fallback scanning." },
        { value: 2, label: "Scan", help: "Round-robin across enabled profiles." },
    ];
    const radioProfileOptions = [
        {
            bit: 0x02,
            id: "WMbusT868",
            label: "Wireless M-Bus T-mode",
            help: "Current production receive path and safe fallback profile.",
        },
        {
            bit: 0x04,
            id: "WMbusPriosR3",
            label: "PRIOS R3",
            help: "Capture-only experimental profile available to the primary radio runtime.",
        },
        {
            bit: 0x08,
            id: "WMbusPriosR4",
            label: "PRIOS R4",
            help: "Capture-only experimental profile using the same PRIOS pipeline at 868.30 MHz.",
        },
    ];

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

    function toNumberOr(value, fallback) {
        const parsed = Number(value);
        return Number.isFinite(parsed) ? parsed : fallback;
    }

    function isFirstBootProvisioning() {
        return !!(bootstrapInfo && bootstrapInfo.provisioning && !bootstrapInfo.password_set);
    }

    function setHiddenIfPresent(sel, hidden) {
        const el = $(sel);
        if (el) {
            el.hidden = hidden;
        }
    }

    function stopRefreshTimer() {
        if (refreshTimer) {
            clearInterval(refreshTimer);
            refreshTimer = null;
        }
        if (heavyRefreshTimer) {
            clearInterval(heavyRefreshTimer);
            heavyRefreshTimer = null;
        }
    }

    function startOtaStatusPolling() {
        stopOtaStatusPolling();
        loadOtaStatus();
        otaStatusPollTimer = setInterval(() => {
            loadOtaStatus();
        }, 1000);
    }

    function stopOtaStatusPolling() {
        if (otaStatusPollTimer) {
            clearInterval(otaStatusPollTimer);
            otaStatusPollTimer = null;
        }
    }

    function clearOtaAutoRebootTimer() {
        if (otaAutoRebootTimer) {
            clearTimeout(otaAutoRebootTimer);
            otaAutoRebootTimer = null;
        }
    }

    function setSettingsRebootButtonProminent(prominent) {
        const btn = $("#cfg-reboot");
        if (!btn) {
            return;
        }
        btn.classList.toggle("btn-primary", !!prominent);
        btn.classList.toggle("btn-secondary", !prominent);
    }

    function sendRebootCommand(messageEl, successMessage, failureMessage) {
        return api("POST", "/api/system/reboot")
            .then(() => {
                setSettingsRebootButtonProminent(false);
                if (messageEl) {
                    setMsg(messageEl, "warning", successMessage || "Reboot command sent.");
                }
            })
            .catch((err) => {
                if (messageEl) {
                    setMsg(messageEl, "error", failureMessage || "Reboot failed.");
                }
                throw err;
            });
    }

    function maybeScheduleOtaAutoReboot(status) {
        const state = String((status && status.state) || "").toLowerCase();
        const progress = Number(status && status.progress_pct);
        if (state === "failed") {
            otaAutoRebootArmed = false;
            clearOtaAutoRebootTimer();
            stopOtaStatusPolling();
            return;
        }
        if (!otaAutoRebootArmed) {
            return;
        }
        if (state !== "rebooting" && progress < 100) {
            return;
        }
        otaAutoRebootArmed = false;
        stopOtaStatusPolling();
        if (otaAutoRebootTimer) {
            return;
        }
        otaAutoRebootTimer = setTimeout(() => {
            otaAutoRebootTimer = null;
            sendRebootCommand(
                $("#ota-status"),
                "Restarting device to activate new firmware...",
                "Automatic reboot after OTA failed."
            ).catch(() => {});
        }, 2000);
    }

    /** Sign-in only: session invalid, logout, or API 401. Never shows Initial Setup. */
    function showSessionExpiredSignIn() {
        stopRefreshTimer();
        token = "";
        localStorage.removeItem("wg_token");
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        $("#login-subtitle").textContent = "Sign in to manage your device.";
        const err = $("#login-error");
        if (err) {
            err.hidden = true;
            err.textContent = "";
        }
    }

    /** Cold start with no token after bootstrap (not first-boot provisioning). */
    function showStartupUnauthenticated(boot) {
        stopRefreshTimer();
        token = "";
        localStorage.removeItem("wg_token");
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        if (boot && boot.bootstrap_failed) {
            const startupMsg = $("#auth-startup-msg");
            if (startupMsg) {
                startupMsg.hidden = false;
                startupMsg.textContent =
                    "Unable to read bootstrap state. Showing sign in fallback.";
            }
        } else {
            setHiddenIfPresent("#auth-startup-msg", true);
        }
        $("#login-subtitle").textContent = "Sign in to manage your device.";
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
                showSessionExpiredSignIn();
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

    function apiRaw(method, path, options) {
        const opts = Object.assign({ method: method, headers: {} }, options || {});
        opts.headers = Object.assign({}, opts.headers || {});
        if (token) {
            opts.headers.Authorization = "Bearer " + token;
        }
        return fetch(path, opts).then((r) => {
            if (r.status === 401) {
                showSessionExpiredSignIn();
                throw new Error("unauthorized");
            }
            if (!r.ok) {
                throw new Error("http_" + r.status);
            }
            return r;
        });
    }

    function showSignInScreen() {
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        if (bootstrapInfo && bootstrapInfo.provisioning) {
            $("#login-subtitle").textContent =
                "Provisioning mode detected. Sign in with the configured admin password.";
        } else {
            $("#login-subtitle").textContent = "Sign in to manage your device.";
        }
    }

    function showSetupScreen() {
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", true);
        setHiddenIfPresent("#setup-form", false);
        $("#login-subtitle").textContent =
            "Initial setup required. Configure Wi-Fi and admin password.";
    }

    function forceFirstBootSetup() {
        token = "";
        localStorage.removeItem("wg_token");
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        showSetupScreen();
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
                "Provisioning mode detected. Use Initial Setup on first boot, or sign in if already configured.";
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
        const timeoutMs = 5000;
        const controller = typeof AbortController !== "undefined" ? new AbortController() : null;
        let timeoutId = null;
        if (controller) {
            timeoutId = setTimeout(() => controller.abort(), timeoutMs);
        }
        const fetchOpts = {
            cache: "no-store",
        };
        if (controller) {
            fetchOpts.signal = controller.signal;
        }

        return fetch("/api/bootstrap", fetchOpts)
            .then((r) => {
                if (!r.ok) {
                    throw new Error("bootstrap_failed");
                }
                return r.json();
            })
            .then((data) => {
                if (timeoutId) {
                    clearTimeout(timeoutId);
                }
                bootstrapInfo = {
                    mode: data.mode || "unknown",
                    provisioning: toBool(data.provisioning),
                    password_set: toBool(data.password_set),
                    bootstrap_failed: false,
                };
                return bootstrapInfo;
            })
            .catch(() => {
                if (timeoutId) {
                    clearTimeout(timeoutId);
                }
                bootstrapInfo = {
                    mode: "unknown",
                    provisioning: false,
                    password_set: true,
                    bootstrap_failed: true,
                };
                return bootstrapInfo;
            });
    }

    function checkFirstBootByLiveConfig(status) {
        if (!status || String(status.mode || "").toLowerCase() !== "provisioning") {
            return Promise.resolve(false);
        }
        return api("GET", "/api/config")
            .then((cfg) => {
                const auth = cfg && cfg.auth ? cfg.auth : {};
                const passwordSet = !!auth.password_set;
                if (!passwordSet) {
                    bootstrapInfo = {
                        mode: "provisioning",
                        provisioning: true,
                        password_set: false,
                        bootstrap_failed: false,
                    };
                    return true;
                }
                return false;
            })
            .catch(() => false);
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

        const payload = {
            device: {
                name: $("#setup-device-name").value.trim(),
                hostname: $("#setup-hostname").value.trim(),
            },
            wifi: {
                ssid: ssid,
                password: wifiPassword,
            },
            auth: {
                admin_password: adminPassword,
            },
            mqtt: {
                enabled: mqttEnabled,
                host: mqttHost,
                username: $("#setup-mqtt-user").value.trim(),
                password: $("#setup-mqtt-password").value,
            },
        };
        const portValue = Number($("#setup-mqtt-port").value);
        if (!Number.isNaN(portValue) && portValue > 0) {
            payload.mqtt.port = portValue;
        }

        return fetch("/api/bootstrap/setup", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
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
                        const issues = Array.isArray(data.issues)
                            ? data.issues.map((i) => i.field + ": " + i.message).join("; ")
                            : "";
                        const suffix = issues ? " (" + issues + ")" : "";
                        throw new Error((data.error || "setup_save_failed") + suffix);
                    }
                    return data;
                })
            )
            .then(() => {
                token = "";
                localStorage.removeItem("wg_token");
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

                dashboardCache.duplicateCount = duplicateCount;
                dashboardCache.detected = detected;
                dashboardCache.watchlistCount = watchCount;

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

    /** Light dashboard refresh: /api/status only; keeps last meter/watchlist/duplicate totals from full load. */
    function loadDashboardLight() {
        api("GET", "/api/status")
            .then((status) => {
                cacheStatus = status;
                applyModeUi(status.mode);

                const health = status.health || {};
                const metrics = status.metrics || {};
                const wifi = status.wifi || {};
                const mqtt = status.mqtt || {};
                const radio = status.radio || {};
                const duplicateCount = dashboardCache.duplicateCount;
                const detected = dashboardCache.detected;
                const watchCount = dashboardCache.watchlistCount;

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

    function refreshStatusOnly() {
        if (currentPage === "dashboard") {
            loadDashboardLight();
            return;
        }
        api("GET", "/api/status")
            .then((status) => {
                cacheStatus = status;
                applyModeUi(status.mode);
            })
            .catch(() => {});
    }

    function refreshHeavyIfNeeded() {
        if (currentPage === "telegrams") {
            loadTelegrams();
        } else if (currentPage === "meters") {
            loadDetectedMeters();
        } else if (currentPage === "watchlist") {
            loadWatchlist();
        } else if (currentPage === "dashboard") {
            loadDashboard();
        }
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
                    const proto = f.protocol_name || "WMBUS_T";
                    const protoCell = createCell(tr, proto);
                    if (proto === "PRIOS_R3" || proto === "PRIOS_R4" || proto === "PRIOS") {
                        protoCell.style.cssText = "color:#7ec8e3;font-weight:600";
                    }
                    createCell(tr, f.vendor || "");
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
        Promise.all([api("GET", "/api/diagnostics/radio"), api("GET", "/api/diagnostics/mqtt"), api("GET", "/api/status"), api("GET", "/api/ota/status"), api("GET", "/api/diagnostics/prios")])
            .then(([radioData, mqttData, statusData, otaData, priosData]) => {
                const radioEl = $("#rf-stats");
                const schedEl = $("#scheduler-stats");
                const priosStatusEl = $("#prios-status");
                const priosCapturesEl = $("#prios-captures");
                const mqttEl = $("#mqtt-stats");
                const sysEl = $("#sys-stats");
                const otaEl = $("#diag-ota-stats");
                clearChildren(radioEl);
                clearChildren(schedEl);
                clearChildren(priosStatusEl);
                clearChildren(priosCapturesEl);
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

                // PRIOS capture campaign section
                const prios = priosData || {};
                const priosCampaign = prios.campaign_active === true;
                const priosDiscovery = prios.discovery_active === true;
                const priosVariant  = prios.variant || "manchester_off";
                const priosMode = prios.mode || "inactive";
                const operatorView = radioData.operator_view || {};
                const operatorTmode = operatorView.tmode || {};
                const operatorPrios = operatorView.prios || {};
                [
                    ["PRIOS Mode", priosMode === "discovery_sniffer"
                        ? "\u25CF Discovery / Sniffer ACTIVE \u2014 T-mode suspended"
                        : (priosMode === "campaign_1E9B" || priosMode === "campaign_fake_sync"
                            ? "\u25CF Campaign 0x1E9B ACTIVE \u2014 T-mode suspended"
                            : "\u25CB inactive")],
                    ["Variant",       priosVariant === "manchester_on"
                        ? "Variant B (Manchester ON)"
                        : "Variant A (Manchester OFF)"],
                    ["Profile",       prios.profile],
                    ["Support Level", prios.support_level || "identity_only_capture"],
                    ["Reading Decode", prios.reading_decode_available ? "available" : "not available"],
                    ["Burst Starts Seen", prios.burst_starts_seen],
                    ["Total Captures",prios.total_captures],
                    ["Noise Rejections", prios.noise_rejections],
                    ["Quality Rejections", prios.quality_rejections],
                    ["Variant B Short Rejects", prios.variant_b_short_rejections],
                    ["Similarity Rejects", prios.similarity_rejections],
                    ["Last Reject Reason", prios.last_reject_reason],
                    ["Total Evicted", prios.total_evicted],
                    ["Retained Captures", prios.retained_captures],
                    ["Retained A/B Totals", text(prios.retained_variant_a_total, "0") + " / " + text(prios.retained_variant_b_total, "0")],
                    ["Retained Length Avg", prios.retained_length_avg],
                    ["Retained Length Min/Max", text(prios.retained_length_min, "0") + " / " + text(prios.retained_length_max, "0")],
                    ["Variant B Min Timeout Bytes", prios.variant_b_min_timeout_capture_bytes],
                    ["Sync Campaign Starts",  prios.sync_campaign_starts],
                    ["Dedup Rejected",         prios.total_dedup_rejected],
                    ["Device Quota Rejected",  prios.total_device_quota_rejected],
                ].forEach((entry) => priosStatusEl.appendChild(kvRow(entry[0], entry[1])));

                // Show export button only when there are captures
                const exportRow = $("#prios-export-row");
                if (exportRow) {
                    exportRow.style.display = (prios.total_captures > 0) ? "" : "none";
                }

                const recentCaptures = Array.isArray(prios.recent_captures) ? prios.recent_captures : [];
                if (recentCaptures.length === 0) {
                    const note = document.createElement("p");
                    note.className = "empty-state";
                    note.textContent = (priosCampaign || priosDiscovery)
                        ? "PRIOS mode active. No captures yet \u2014 move near the meter."
                        : "No PRIOS captures. Enable PRIOS discovery or campaign mode in Settings to start.";
                    priosCapturesEl.appendChild(note);
                } else {
                    const previewNote = document.createElement("p");
                    previewNote.className = "muted";
                    previewNote.textContent = "Live diagnostics show only the most recent preview rows. Use export for the full retained capture set.";
                    priosCapturesEl.appendChild(previewNote);
                    recentCaptures.slice().reverse().forEach((c) => {
                        const row = document.createElement("div");
                        row.className = "kv-item";
                        row.style.alignItems = "flex-start";
                        row.style.paddingBottom = "8px";

                        // Left: variant + sequence + signal quality
                        const left = document.createElement("span");
                        const variantTag = c.variant === "manchester_on" ? "[B]" : "[A]";
                        left.textContent = variantTag + " #" + c.seq +
                            "  " + c.bytes_captured + "B" +
                            "  " + c.rssi_dbm + "dBm" +
                            "  lqi=" + c.lqi;

                        // Right: device fingerprint badge (if present) + hex preview
                        const right = document.createElement("span");
                        right.style.cssText = "display:flex;flex-direction:column;align-items:flex-end;gap:3px";

                        const fp = c.device_fingerprint || null;
                        if (fp) {
                            // Derive a stable hue from first fingerprint byte so each
                            // device gets a consistent color across page refreshes.
                            const hue = Math.round((parseInt(fp.substring(0, 2), 16) / 256) * 360);
                            const badge = document.createElement("span");
                            badge.className = "badge";
                            badge.style.cssText =
                                "background:hsl(" + hue + ",45%,22%);" +
                                "color:hsl(" + hue + ",80%,72%);" +
                                "border:1px solid hsl(" + hue + ",45%,35%);" +
                                "font-family:Consolas,Menlo,monospace;" +
                                "cursor:pointer;user-select:none";
                            badge.title = "Click to add to watchlist";
                            badge.textContent = c.device_alias
                                ? fp + " \u2014 " + c.device_alias
                                : fp;
                            badge.addEventListener("click", () => {
                                addWatchlistQuickAction(fp, c.device_alias || "", "PRIOS device");
                            });
                            right.appendChild(badge);
                        }

                        const hexSpan = document.createElement("span");
                        hexSpan.className = "hex";
                        hexSpan.textContent = c.display_prefix_hex || c.prefix_hex || "--";
                        right.appendChild(hexSpan);

                        row.appendChild(left);
                        row.appendChild(right);
                        priosCapturesEl.appendChild(row);
                    });
                }

                const sched = radioData.scheduler || {};
                const topology = radioData.topology || {};
                const enabledProfiles = Array.isArray(sched.enabled_profiles)
                    ? sched.enabled_profiles.join(", ")
                    : "--";
                [
                    ["Radio Topology", topology.single_radio_mode ? "Single-radio (primary active)" : "Multi-radio"],
                    ["Active Radio Count", topology.active_radio_count],
                    ["Supported Radio Slots", topology.supported_radio_slots],
                    ["Control Mode", operatorView.control_mode || "--"],
                    ["PRIOS Override Active", operatorView.prios_override_active ? "yes" : "no"],
                    ["Configured PRIOS Profile", operatorView.configured_prios_profile || "--"],
                    ["Effective Frequency", operatorView.effective_frequency_khz
                        ? (Number(operatorView.effective_frequency_khz) / 1000).toFixed(2) + " MHz"
                        : "--"],
                    ["Scheduler Mode", sched.mode],
                    ["Preferred Profile", sched.preferred_profile],
                    ["Selected Profile", sched.selected_profile],
                    ["Active Profile", sched.active_profile],
                    ["Active Protocol", sched.active_protocol],
                    ["Last Apply Status", sched.last_apply_status],
                    ["Enabled Profiles", enabledProfiles],
                    ["Last Switch Reason", sched.last_switch_reason],
                    ["Last Wake Source", sched.last_wake_source],
                    ["Profile Switches", sched.profile_switch_count],
                    ["Profile Applies", sched.profile_apply_count],
                    ["Profile Apply Failures", sched.profile_apply_failures],
                    ["IRQ Wakes", sched.irq_wake_count],
                    ["Fallback Wakes", sched.fallback_wake_count],
                    ["T-mode Recent Accepts", operatorTmode.recent_accepts],
                    ["T-mode Last Reject", operatorTmode.last_reject_reason],
                    ["T-mode Last Success", operatorTmode.last_success_meter_key
                        ? operatorTmode.last_success_meter_key + " @ " + text(operatorTmode.last_success_rssi_dbm, "--") + " dBm / LQI " + text(operatorTmode.last_success_lqi, "--")
                        : "--"],
                    ["PRIOS Recent Accepts", operatorPrios.recent_accepts],
                    ["PRIOS Support", operatorPrios.support_level],
                    ["PRIOS Last Reject", operatorPrios.last_reject_reason],
                    ["PRIOS Last Success", operatorPrios.last_success_meter_key
                        ? operatorPrios.last_success_meter_key + " @ " + text(operatorPrios.last_success_rssi_dbm, "--") + " dBm / LQI " + text(operatorPrios.last_success_lqi, "--")
                        : "--"],
                ].forEach((entry) => schedEl.appendChild(kvRow(entry[0], entry[1])));

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

    function appendFieldHint(parent, message) {
        const hint = document.createElement("p");
        hint.className = "hint";
        hint.textContent = message;
        parent.appendChild(hint);
    }

    function createConfigSubsection(titleText, toneClass) {
        const section = document.createElement("section");
        section.className = "config-subsection";
        if (toneClass) {
            section.classList.add(toneClass);
        }

        const title = document.createElement("div");
        title.className = "subsection-title";
        title.textContent = titleText;
        section.appendChild(title);
        return section;
    }

    function setRadioSectionState(section, active) {
        if (!section) {
            return;
        }
        section.classList.toggle("config-subsection-active", !!active);
        section.classList.toggle("config-subsection-inactive", !active);
        section.querySelectorAll("input, select, button, textarea").forEach((el) => {
            if (el.dataset.allowWhileInactive === "true") {
                return;
            }
            el.disabled = !active;
        });
    }

    function renderRadioConfigSection(container, radioCfg) {
        const radio = radioCfg || {};
        const card = document.createElement("div");
        card.className = "card";

        const title = document.createElement("h3");
        title.textContent = "Radio";
        card.appendChild(title);

        appendFieldHint(
            card,
            "This firmware currently runs one active CC1101 runtime. The primary radio can schedule multiple profiles; the secondary radio slot is reserved in the runtime model for future hardware."
        );

        const rateLabel = document.createElement("label");
        rateLabel.setAttribute("for", "cfg-radio-data_rate");
        rateLabel.textContent = "Data Rate Selector";
        const rateInput = document.createElement("input");
        rateInput.id = "cfg-radio-data_rate";
        rateInput.type = "number";
        rateInput.value = String(toNumberOr(radio.data_rate, 0));
        rateLabel.appendChild(rateInput);
        card.appendChild(rateLabel);
        appendFieldHint(card, "Reserved field. Leave at 0 unless a specific radio profile requires otherwise.");

        const recoveryRow = document.createElement("label");
        recoveryRow.className = "checkbox-row";
        const recoveryInput = document.createElement("input");
        recoveryInput.id = "cfg-radio-auto_recovery";
        recoveryInput.type = "checkbox";
        recoveryInput.checked = !!radio.auto_recovery;
        const recoveryText = document.createElement("span");
        recoveryText.textContent = "Enable automatic radio recovery";
        recoveryRow.appendChild(recoveryInput);
        recoveryRow.appendChild(recoveryText);
        card.appendChild(recoveryRow);

        const activeModeBanner = document.createElement("div");
        activeModeBanner.id = "cfg-radio-active-mode-banner";
        activeModeBanner.className = "config-mode-banner";
        card.appendChild(activeModeBanner);

        const normalSection = createConfigSubsection("Normal Radio Scheduling");
        card.appendChild(normalSection);
        appendFieldHint(
            normalSection,
            "These settings control the active radio plan only when PRIOS Experimental Mode is Off."
        );
        const normalWarning = document.createElement("p");
        normalWarning.className = "msg msg-warning";
        normalWarning.hidden = true;
        normalWarning.textContent = "Standard scheduler is inactive while PRIOS Experimental mode is running.";
        normalSection.appendChild(normalWarning);

        const schedulerLabel = document.createElement("label");
        schedulerLabel.setAttribute("for", "cfg-radio-scheduler_mode");
        schedulerLabel.textContent = "Scheduler Mode";
        const schedulerSelect = document.createElement("select");
        schedulerSelect.id = "cfg-radio-scheduler_mode";
        radioSchedulerOptions.forEach((option) => {
            const opt = document.createElement("option");
            opt.value = String(option.value);
            opt.textContent = option.label;
            schedulerSelect.appendChild(opt);
        });
        schedulerSelect.value = String(toNumberOr(radio.scheduler_mode, 0));
        schedulerLabel.appendChild(schedulerSelect);
        normalSection.appendChild(schedulerLabel);
        appendFieldHint(
            normalSection,
            "Scheduler mode and enabled profiles are saved for the primary radio instance. They are intentionally overridden while PRIOS experimental mode is active."
        );

        const profilesBlock = document.createElement("div");
        profilesBlock.className = "config-subsection";
        const profilesTitle = document.createElement("div");
        profilesTitle.className = "subsection-title";
        profilesTitle.textContent = "Enabled Profiles";
        profilesBlock.appendChild(profilesTitle);
        const enabledProfilesMask = toNumberOr(radio.enabled_profiles, 0x02);
        radioProfileOptions.forEach((profile) => {
            const row = document.createElement("label");
            row.className = "checkbox-row";
            const input = document.createElement("input");
            input.type = "checkbox";
            input.id = "cfg-radio-enabled-profile-" + profile.id;
            input.checked = (enabledProfilesMask & profile.bit) !== 0;
            const textWrap = document.createElement("span");
            textWrap.textContent = profile.label;
            row.appendChild(input);
            row.appendChild(textWrap);
            profilesBlock.appendChild(row);
            appendFieldHint(profilesBlock, profile.help);
        });
        normalSection.appendChild(profilesBlock);

        const priosSection = createConfigSubsection("PRIOS Experimental");
        card.appendChild(priosSection);
        appendFieldHint(
            priosSection,
            "When PRIOS Experimental Mode is On, the runtime locks the radio to the selected PRIOS profile and suspends normal scheduler-driven T-mode reception."
        );

        const priosModeLabel = document.createElement("label");
        priosModeLabel.setAttribute("for", "cfg-radio-prios_mode");
        priosModeLabel.textContent = "PRIOS Experimental Mode";
        const priosModeSelect = document.createElement("select");
        priosModeSelect.id = "cfg-radio-prios_mode";
        [
            { value: "off", label: "Off" },
            { value: "campaign", label: "Experimental campaign (sync-based)" },
            { value: "discovery", label: "Discovery / sniffer (no sync assumption)" },
        ].forEach((option) => {
            const opt = document.createElement("option");
            opt.value = option.value;
            opt.textContent = option.label;
            priosModeSelect.appendChild(opt);
        });
        priosModeSelect.value = radio.prios_discovery_mode
            ? "discovery"
            : (radio.prios_capture_campaign ? "campaign" : "off");
        priosModeLabel.appendChild(priosModeSelect);
        priosSection.appendChild(priosModeLabel);

        const priosProfileLabel = document.createElement("label");
        priosProfileLabel.setAttribute("for", "cfg-radio-prios_profile");
        priosProfileLabel.textContent = "PRIOS Profile";
        const priosProfileSelect = document.createElement("select");
        priosProfileSelect.id = "cfg-radio-prios_profile";
        [
            { value: "WMbusPriosR3", label: "PRIOS R3 (868.95 MHz)" },
            { value: "WMbusPriosR4", label: "PRIOS R4 (868.30 MHz)" },
        ].forEach((option) => {
            const opt = document.createElement("option");
            opt.value = option.value;
            opt.textContent = option.label;
            priosProfileSelect.appendChild(opt);
        });
        priosProfileSelect.value =
            radio.prios_profile === "WMbusPriosR4" ? "WMbusPriosR4" : "WMbusPriosR3";
        priosProfileLabel.appendChild(priosProfileSelect);
        priosSection.appendChild(priosProfileLabel);
        appendFieldHint(
            priosSection,
            "PRIOS Profile is the operator-facing source of truth for experimental PRIOS carrier selection: R3 uses 868.95 MHz and R4 uses 868.30 MHz."
        );

        const manchesterRow = document.createElement("label");
        manchesterRow.className = "checkbox-row";
        const manchesterInput = document.createElement("input");
        manchesterInput.id = "cfg-radio-prios_manchester_enabled";
        manchesterInput.type = "checkbox";
        manchesterInput.checked = !!radio.prios_manchester_enabled;
        manchesterInput.dataset.allowWhileInactive = "true";
        const manchesterText = document.createElement("span");
        manchesterText.textContent = "Variant B (Manchester ON)";
        manchesterRow.appendChild(manchesterInput);
        manchesterRow.appendChild(manchesterText);
        priosSection.appendChild(manchesterRow);
        appendFieldHint(
            priosSection,
            "Variant A = Manchester OFF. Variant B = Manchester ON. The selected variant applies to both PRIOS campaign and discovery/sniffer modes."
        );

        const variantOffNote = document.createElement("label");
        variantOffNote.className = "checkbox-row checkbox-row-muted";
        const variantOffIndicator = document.createElement("input");
        variantOffIndicator.type = "checkbox";
        variantOffIndicator.disabled = true;
        const variantOffText = document.createElement("span");
        variantOffText.textContent = "Variant A (Manchester OFF)";
        variantOffNote.appendChild(variantOffIndicator);
        variantOffNote.appendChild(variantOffText);
        priosSection.appendChild(variantOffNote);

        const priosInfoBlock = document.createElement("div");
        priosInfoBlock.className = "config-mode-banner mode-info";
        priosInfoBlock.id = "cfg-radio-prios-info";
        priosSection.appendChild(priosInfoBlock);

        appendFieldHint(
            priosSection,
            "PRIOS R3 and R4 share the same frame format, sync word 0x1E9B, diagnostics path, export path, and live telegram handling. Only the PRIOS carrier/profile changes."
        );

        const advancedSection =
            createConfigSubsection("Advanced Radio Overrides", "config-subsection-advanced");
        card.appendChild(advancedSection);
        appendFieldHint(
            advancedSection,
            "Expert-only compatibility fields. PRIOS experimental modes do not use Frequency (kHz) as their primary source of truth."
        );

        const freqLabel = document.createElement("label");
        freqLabel.setAttribute("for", "cfg-radio-frequency_khz");
        freqLabel.textContent = "Frequency (kHz)";
        const freqInput = document.createElement("input");
        freqInput.id = "cfg-radio-frequency_khz";
        freqInput.type = "number";
        freqInput.value = String(toNumberOr(radio.frequency_khz, 868950));
        freqLabel.appendChild(freqInput);
        advancedSection.appendChild(freqLabel);
        appendFieldHint(
            advancedSection,
            "Saved for compatibility and normal radio configuration. PRIOS experimental R3/R4 selection overrides the carrier/profile at runtime."
        );

        const summary = document.createElement("p");
        summary.id = "cfg-radio-campaign-summary";
        summary.className = "hint config-summary";
        card.appendChild(summary);

        function updateRadioCampaignSummary() {
            const selectedProfile =
                priosProfileSelect.value === "WMbusPriosR4" ? "PRIOS R4" : "PRIOS R3";
            const experimentalActive = priosModeSelect.value !== "off";
            setRadioSectionState(normalSection, !experimentalActive);
            setRadioSectionState(priosSection, experimentalActive);
            setRadioSectionState(advancedSection, false);
            normalWarning.hidden = !experimentalActive;
            variantOffIndicator.checked = !manchesterInput.checked;
            priosInfoBlock.textContent =
                "Target Frequency: " +
                (priosProfileSelect.value === "WMbusPriosR4" ? "868.30 MHz" : "868.95 MHz") +
                " | Variant: " +
                (manchesterInput.checked ? "B (Manchester ON)" : "A (Manchester OFF)");

            if (priosModeSelect.value === "campaign") {
                activeModeBanner.className = "config-mode-banner mode-prios";
                activeModeBanner.textContent =
                    "Active after reboot: PRIOS Experimental campaign overrides normal scheduler settings and locks the radio to " + selectedProfile + ".";
                summary.textContent =
                    "Experimental campaign mode keeps the current sync-based PRIOS path. It locks the radio to " + selectedProfile + ", suspends T-mode, and uses the selected variant after reboot.";
            } else if (priosModeSelect.value === "discovery") {
                activeModeBanner.className = "config-mode-banner mode-prios";
                activeModeBanner.textContent =
                    "Active after reboot: PRIOS discovery/sniffer overrides normal scheduler settings and locks the radio to " + selectedProfile + ".";
                summary.textContent =
                    "Discovery / sniffer mode locks the radio to " + selectedProfile + ", suspends T-mode, and captures bounded bursts using discovery-oriented radio activity gating instead of the placeholder sync word.";
            } else {
                activeModeBanner.className = "config-mode-banner mode-normal";
                activeModeBanner.textContent =
                    "Active after reboot: Normal Radio Scheduling uses Scheduler Mode plus Enabled Profiles on the primary radio runtime.";
                summary.textContent =
                    "Normal mode uses the saved primary-radio scheduler mode and enabled profiles after reboot. PRIOS profile selection is stored but not authoritative until an experimental PRIOS mode is turned on.";
            }
        }

        priosModeSelect.addEventListener("change", updateRadioCampaignSummary);
        priosModeSelect.dataset.allowWhileInactive = "true";
        priosProfileSelect.addEventListener("change", updateRadioCampaignSummary);
        priosProfileSelect.dataset.allowWhileInactive = "true";
        manchesterInput.addEventListener("change", updateRadioCampaignSummary);
        updateRadioCampaignSummary();
        container.appendChild(card);
    }

    function loadConfig() {
        return api("GET", "/api/config")
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
                renderRadioConfigSection(c, cfg.radio || {});
                renderConfigSection(c, "Auth", cfg.auth || {});
                renderConfigSection(c, "Logging", cfg.logging || {});
                setSettingsRebootButtonProminent(false);
            })
            .catch(() => {});
    }

    function collectConfigValues(cfg) {
        ["device", "wifi", "mqtt", "auth", "logging"].forEach((section) => {
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

        if (cfg.radio) {
            cfg.radio.frequency_khz = toNumberOr($("#cfg-radio-frequency_khz").value, cfg.radio.frequency_khz);
            cfg.radio.data_rate = toNumberOr($("#cfg-radio-data_rate").value, cfg.radio.data_rate);
            cfg.radio.auto_recovery = $("#cfg-radio-auto_recovery").checked;
            cfg.radio.scheduler_mode = toNumberOr($("#cfg-radio-scheduler_mode").value, cfg.radio.scheduler_mode);

            let enabledProfiles = 0;
            radioProfileOptions.forEach((profile) => {
                const checkbox = $("#cfg-radio-enabled-profile-" + profile.id);
                if (checkbox && checkbox.checked) {
                    enabledProfiles |= profile.bit;
                }
            });
            cfg.radio.enabled_profiles = enabledProfiles;
            cfg.radio.prios_profile = $("#cfg-radio-prios_profile").value;
            const priosMode = $("#cfg-radio-prios_mode").value;
            cfg.radio.prios_capture_campaign = priosMode === "campaign";
            cfg.radio.prios_discovery_mode = priosMode === "discovery";
            cfg.radio.prios_manchester_enabled = $("#cfg-radio-prios_manchester_enabled").checked;
        }
    }

    function loadOtaStatus() {
        return api("GET", "/api/ota/status")
            .then((d) => {
                const otaGrid = $("#ota-state-grid");
                clearChildren(otaGrid);
                otaGrid.appendChild(kvRow("State", d.state));
                otaGrid.appendChild(kvRow("Progress", text(d.progress_pct, "0") + "%"));
                otaGrid.appendChild(kvRow("Current Version", d.current_version));
                otaGrid.appendChild(kvRow("Message", d.message));
                $("#ota-status").textContent = "Status: " + text(d.state);
                maybeScheduleOtaAutoReboot(d);
                return d;
            })
            .catch(() => null);
    }

    function loadSupport() {
        const summary = $("#support-summary");
        clearChildren(summary);
        Promise.all([api("GET", "/api/status/full"), api("GET", "/api/ota/status"), api("GET", "/api/watchlist")])
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
                const lines = Array.isArray(data.entries) ? data.entries : [];
                const filter = ($("#log-filter") && $("#log-filter").value) || "";
                const filtered = filter
                    ? lines.filter((l) => String(l.severity || "").toLowerCase() === filter)
                    : lines;
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
                $("#logs-empty").hidden = filtered.length !== 0;
            })
            .catch((err) => {
                if (err && err.message === "unauthorized") {
                    return;
                }
                $("#log-output").textContent = "Failed to load logs.";
                $("#logs-empty").hidden = false;
            });
    }

    function startRefresh() {
        stopRefreshTimer();
        refreshTimer = setInterval(() => {
            refreshStatusOnly();
        }, 10000);
        heavyRefreshTimer = setInterval(() => {
            refreshHeavyIfNeeded();
        }, 60000);
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
            showSessionExpiredSignIn();
        });

        $("#login-form").addEventListener("submit", (e) => {
            e.preventDefault();
            if (isFirstBootProvisioning()) {
                setMsg(
                    $("#setup-msg"),
                    "warning",
                    "First boot requires Initial Setup. Configure Wi-Fi and admin password below."
                );
                showSetupScreen();
                return;
            }
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
                        showSessionExpiredSignIn();
                        return;
                    }
                    if (resp.reboot_required) {
                        setSettingsRebootButtonProminent(true);
                        setMsg(msg, "warning", "Saved. Reboot required to apply runtime settings.");
                    } else {
                        setSettingsRebootButtonProminent(false);
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

        $("#cfg-reboot").addEventListener("click", () => {
            if (!confirm("Reboot device now?")) {
                return;
            }
            sendRebootCommand($("#cfg-msg"), "Reboot command sent.", "Reboot failed.").catch(() => {});
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
            otaAutoRebootArmed = true;
            clearOtaAutoRebootTimer();
            stopOtaStatusPolling();
            setMsg($("#ota-status"), "warning", "Uploading firmware... 0%");
            const xhr = new XMLHttpRequest();
            xhr.upload.addEventListener("progress", (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    setMsg($("#ota-status"), "warning", "Uploading firmware... " + pct + "%");
                }
            });
            xhr.addEventListener("load", () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    let resp = {};
                    try {
                        resp = JSON.parse(xhr.responseText);
                    } catch (_) {}
                    const detail = resp.detail || "Upload complete. Reboot required.";
                    setMsg($("#ota-status"), "success", detail);
                    loadOtaStatus();
                } else {
                    otaAutoRebootArmed = false;
                    clearOtaAutoRebootTimer();
                    let resp = {};
                    try {
                        resp = JSON.parse(xhr.responseText);
                    } catch (_) {}
                    setMsg($("#ota-status"), "error", "Upload failed: " + (resp.error || "http_" + xhr.status));
                }
                stopOtaStatusPolling();
            });
            xhr.addEventListener("error", () => {
                otaAutoRebootArmed = false;
                clearOtaAutoRebootTimer();
                setMsg($("#ota-status"), "error", "Upload failed: network error");
                stopOtaStatusPolling();
            });
            xhr.open("POST", "/api/ota/upload");
            if (token) {
                xhr.setRequestHeader("Authorization", "Bearer " + token);
            }
            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.send(file);
        });

        $("#ota-url-btn").addEventListener("click", () => {
            const url = $("#ota-url").value.trim();
            if (!url) {
                setMsg($("#ota-status"), "warning", "Enter URL first.");
                return;
            }
            otaAutoRebootArmed = true;
            clearOtaAutoRebootTimer();
            api("POST", "/api/ota/url", { url: url })
                .then(() => {
                    setMsg($("#ota-status"), "success", "OTA URL update started.");
                    startOtaStatusPolling();
                })
                .catch((err) => {
                    otaAutoRebootArmed = false;
                    clearOtaAutoRebootTimer();
                    stopOtaStatusPolling();
                    setMsg($("#ota-status"), "error", "OTA URL failed: " + err.message);
                });
        });

        $("#sys-bundle").addEventListener("click", () => {
            apiRaw("GET", "/api/support-bundle")
                .then((r) => r.blob())
                .then((blob) => {
                    const a = document.createElement("a");
                    const url = URL.createObjectURL(blob);
                    a.href = url;
                    a.download = "support-bundle.json";
                    a.click();
                    setTimeout(() => URL.revokeObjectURL(url), 1000);
                })
                .catch((err) => {
                    if (err && err.message === "unauthorized") {
                        return;
                    }
                    setMsg($("#factory-msg"), "error", "Support bundle download failed.");
                });
        });

        $("#prios-export-link").addEventListener("click", (ev) => {
            ev.preventDefault();
            const msg = $("#prios-export-msg");
            if (msg) {
                msg.hidden = true;
                msg.textContent = "";
            }
            apiRaw("GET", "/api/diagnostics/prios/export")
                .then((r) => r.blob())
                .then((blob) => {
                    const a = document.createElement("a");
                    const url = URL.createObjectURL(blob);
                    a.href = url;
                    a.download = "prios-captures.json";
                    a.click();
                    setTimeout(() => URL.revokeObjectURL(url), 1000);
                })
                .catch((err) => {
                    if (err && err.message === "unauthorized") {
                        return;
                    }
                    if (msg) {
                        setMsg(msg, "error", "PRIOS export failed.");
                    }
                });
        });

        $("#prios-clear-btn").addEventListener("click", () => {
            const msg = $("#prios-export-msg");
            if (!confirm("Clear retained PRIOS captures and tracked device fingerprints?")) {
                return;
            }
            if (msg) {
                setMsg(msg, "warning", "Clearing PRIOS captures...");
            }
            api("POST", "/api/diagnostics/prios/clear")
                .then(() => {
                    if (msg) {
                        setMsg(msg, "success", "PRIOS captures cleared.");
                    }
                    loadDiagnostics();
                })
                .catch((err) => {
                    if (err && err.message === "unauthorized") {
                        return;
                    }
                    if (msg) {
                        setMsg(msg, "error", "PRIOS clear failed.");
                    }
                });
        });

        $("#sys-reboot").addEventListener("click", () => {
            if (!confirm("Reboot device now?")) {
                return;
            }
            sendRebootCommand($("#factory-msg"), "Reboot command sent.", "Reboot failed.").catch(() => {});
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
    $("#app-shell").hidden = true;
    $("#login-page").hidden = false;
    setHiddenIfPresent("#auth-startup-msg", false);
    setHiddenIfPresent("#login-form", true);
    setHiddenIfPresent("#setup-form", true);

    bootstrap().then((boot) => {
        bootstrapInfo = boot;
        const firstBoot = !!(boot.provisioning && !boot.password_set);
        const stored = (localStorage.getItem("wg_token") || "").trim();

        if (firstBoot) {
            forceFirstBootSetup();
            return;
        }

        if (!stored) {
            showStartupUnauthenticated(boot);
            return;
        }

        token = stored;
        api("GET", "/api/status")
            .then((status) => {
                cacheStatus = status;
                applyModeUi(status.mode);
                showApp();
            })
            .catch((err) => {
                if (err && err.message === "unauthorized") {
                    return;
                }
                showSessionExpiredSignIn();
            });
    });
})();
