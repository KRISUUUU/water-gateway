(function () {
    "use strict";

    let token = sessionStorage.getItem("wg_token") || "";
    let currentPage = "dashboard";
    let cacheStatus = null;
    let cacheWatchlist = [];
    let refreshTimer = null;
    let heavyRefreshTimer = null;
    const dashboardCache = { duplicateCount: 0, detected: 0, watchlistCount: 0 };
    let bootstrapInfo = null;
    let cacheOtaStatus = null;
    let stickyBanner = null;

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

    function clearMsg(el) {
        if (!el) {
            return;
        }
        el.className = "msg";
        el.textContent = "";
        el.hidden = true;
    }

    function toBool(value) {
        return !!value;
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

    function setBanner(kind, message) {
        const banner = $("#app-banner");
        if (!banner) {
            return;
        }
        setMsg(banner, kind, message);
        banner.classList.add("app-banner");
    }

    function clearBanner() {
        const banner = $("#app-banner");
        if (!banner) {
            return;
        }
        banner.className = "msg app-banner";
        banner.textContent = "";
        banner.hidden = true;
    }

    function setStickyBanner(kind, message) {
        stickyBanner = message ? { kind: kind || "warning", message: message } : null;
        syncOperatorBanner();
    }

    /** Sign-in only: session invalid, logout, or API 401. Never shows Initial Setup. */
    function showSessionExpiredSignIn(message, kind) {
        stopRefreshTimer();
        token = "";
        sessionStorage.removeItem("wg_token");
        stickyBanner = null;
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        setHiddenIfPresent("#auth-startup-msg", true);
        setHiddenIfPresent("#login-form", false);
        setHiddenIfPresent("#setup-form", true);
        $("#login-subtitle").textContent = "Sign in to manage your device.";
        const err = $("#login-error");
        if (err) {
            if (message) {
                setMsg(err, kind || "warning", message);
            } else {
                err.hidden = true;
                err.textContent = "";
            }
        }
    }

    /** Cold start with no token after bootstrap (not first-boot provisioning). */
    function showStartupUnauthenticated(boot) {
        stopRefreshTimer();
        token = "";
        sessionStorage.removeItem("wg_token");
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

    async function readJsonBody(response) {
        const txt = await response.text();
        if (!txt) {
            return {};
        }
        try {
            return JSON.parse(txt);
        } catch (_) {
            return {};
        }
    }

    async function request(path, options, opts) {
        const requestOptions = Object.assign({}, options || {});
        const settings = Object.assign(
            { authorize: true, handleUnauthorized: true },
            opts || {}
        );
        const headers = Object.assign({}, requestOptions.headers || {});
        if (settings.authorize && token) {
            headers.Authorization = "Bearer " + token;
        }
        requestOptions.headers = headers;

        const response = await fetch(path, requestOptions);
        if (settings.handleUnauthorized && response.status === 401) {
            showSessionExpiredSignIn("Session expired. Sign in again to continue.", "warning");
            throw new Error("unauthorized");
        }
        return response;
    }

    async function requestJson(path, options, opts) {
        const response = await request(path, options, opts);
        const data = await readJsonBody(response);
        if (!response.ok) {
            const error = new Error(data.error || ("http_" + response.status));
            error.status = response.status;
            error.data = data;
            throw error;
        }
        return data;
    }

    async function api(method, path, body, opts) {
        const requestOptions = {
            method: method,
            headers: {},
        };
        if (body !== undefined) {
            requestOptions.headers["Content-Type"] = "application/json";
            requestOptions.body = JSON.stringify(body);
        }
        return requestJson(path, requestOptions, opts);
    }

    async function downloadBlob(path, opts) {
        const response = await request(path, { method: "GET" }, opts);
        if (!response.ok) {
            throw new Error("download_failed");
        }
        return response.blob();
    }

    function renderDashboard(status, counts) {
        const health = status.health || {};
        const metrics = status.metrics || {};
        const wifi = status.wifi || {};
        const mqtt = status.mqtt || {};
        const radio = status.radio || {};

        const statusGrid = $("#dashboard-status-grid");
        clearChildren(statusGrid);
        statusGrid.appendChild(statCard("Health", text(health.state), badgeClassByState(health.state)));
        statusGrid.appendChild(statCard("Wi-Fi", text(wifi.state), badgeClassByState(wifi.state)));
        statusGrid.appendChild(statCard("MQTT", text(mqtt.state), badgeClassByState(mqtt.state)));
        statusGrid.appendChild(statCard("Radio", text(radio.state), badgeClassByState(radio.state)));
        statusGrid.appendChild(statCard("Mode", status.mode || "--"));
        statusGrid.appendChild(statCard("Firmware", status.firmware_version || "--"));

        const metricGrid = $("#dashboard-metrics-grid");
        clearChildren(metricGrid);
        metricGrid.appendChild(statCard("Uptime", formatUptime(metrics.uptime_s)));
        metricGrid.appendChild(statCard("Frames Received", radio.frames_received || 0));
        metricGrid.appendChild(statCard("CRC Fail", radio.frames_crc_fail || 0));
        metricGrid.appendChild(statCard("Duplicates", counts.duplicateCount));
        metricGrid.appendChild(statCard("Incomplete Frames", radio.frames_incomplete || 0));
        metricGrid.appendChild(statCard("Dropped Too Long", radio.frames_dropped_too_long || 0));
        metricGrid.appendChild(statCard("MQTT Publish Failures", mqtt.publish_failures || 0));
        metricGrid.appendChild(statCard("Detected Meters", counts.detected));
        metricGrid.appendChild(statCard("Watchlist", counts.watchCount));
        metricGrid.appendChild(statCard("Wi-Fi RSSI", text(wifi.rssi_dbm, "--") + " dBm"));
        metricGrid.appendChild(
            statCard("Free Heap", Math.round((metrics.free_heap_bytes || 0) / 1024) + " KB")
        );
        metricGrid.appendChild(statCard("IP Address", wifi.ip_address || "--"));
    }

    function errorMessage(err, fallback) {
        if (err && err.data && err.data.detail) {
            return String(err.data.detail);
        }
        if (err && err.message && !String(err.message).startsWith("http_")) {
            return String(err.message);
        }
        return fallback;
    }

    function setEmptyState(sel, message, hidden) {
        const el = $(sel);
        if (!el) {
            return;
        }
        if (message !== undefined) {
            el.textContent = message;
        }
        el.hidden = hidden;
    }

    function renderKvLoadError(sel, message) {
        const el = $(sel);
        if (!el) {
            return;
        }
        clearChildren(el);
        el.appendChild(kvRow("Error", message));
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
        sessionStorage.removeItem("wg_token");
        stickyBanner = null;
        $("#app-shell").hidden = true;
        $("#login-page").hidden = false;
        showSetupScreen();
    }

    function showApp() {
        $("#login-page").hidden = true;
        $("#app-shell").hidden = false;
        syncOperatorBanner();
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

    function appendKvRows(container, rows) {
        rows.forEach((row) => container.appendChild(kvRow(row[0], row[1])));
    }

    function mqttQueueSummary(mqtt) {
        const depth = Number(mqtt && mqtt.outbox_depth ? mqtt.outbox_depth : 0);
        const capacity = Number(mqtt && mqtt.outbox_capacity ? mqtt.outbox_capacity : 0);
        const held = !!(mqtt && mqtt.held_item);
        const retryFailures = Number(mqtt && mqtt.retry_failure_count ? mqtt.retry_failure_count : 0);
        const retries = Number(mqtt && mqtt.retry_count ? mqtt.retry_count : 0);
        const details = [];
        if (capacity > 0) {
            details.push(depth + "/" + capacity + " queued");
        } else if (depth > 0) {
            details.push(depth + " queued");
        } else {
            details.push("idle");
        }
        if (held) {
            details.push("held item");
        }
        if (retryFailures > 0) {
            details.push(retryFailures + " retry failures");
        } else if (retries > 0) {
            details.push(retries + " retries");
        }
        if (capacity > 0 && depth >= capacity) {
            return "High pressure: " + details.join(", ");
        }
        if (held || depth > 0) {
            return "Active queue: " + details.join(", ");
        }
        return "Clear: " + details.join(", ");
    }

    function radioSummary(radioData) {
        const rsm = radioData && radioData.rsm ? radioData.rsm : {};
        const diag = radioData && radioData.diagnostics ? radioData.diagnostics : {};
        const counters = diag.radio_counters || {};
        const issues = [];
        if (Number(rsm.consecutive_errors || 0) > 0) {
            issues.push(rsm.consecutive_errors + " consecutive errors");
        }
        if (Number(counters.spi_errors || 0) > 0) {
            issues.push(counters.spi_errors + " SPI errors");
        }
        if (Number(counters.fifo_overflows || 0) > 0) {
            issues.push(counters.fifo_overflows + " FIFO overflows");
        }
        if (issues.length > 0) {
            return "Needs attention: " + issues.join(", ");
        }
        return "Stable: " + text(diag.radio_state || rsm.state, "unknown");
    }

    function otaSummary(ota) {
        const state = String(ota && ota.state ? ota.state : "").toLowerCase();
        const progress = text(ota && ota.progress_pct, "0") + "%";
        if (!state || state === "idle") {
            return "Idle";
        }
        if (state.includes("error") || state.includes("fail")) {
            return "Problem: " + text(ota && ota.message, state);
        }
        if (state.includes("success") || state.includes("complete")) {
            return "Ready: " + text(ota && ota.message, "reboot may be required");
        }
        return "In progress: " + progress + " (" + text(ota && ota.message, state) + ")";
    }

    function healthSummary(health) {
        if (!health) {
            return "--";
        }
        const errors = Number(health.error_count || 0);
        const warnings = Number(health.warning_count || 0);
        if (errors > 0) {
            return errors + " error(s): " + text(health.last_error_msg, "check diagnostics");
        }
        if (warnings > 0) {
            return warnings + " warning(s): " + text(health.last_warning_msg, "check diagnostics");
        }
        return "No active health alerts";
    }

    function currentOperatorNotice() {
        const status = cacheStatus || {};
        const ota = cacheOtaStatus || {};
        const health = status.health || {};
        const mqtt = status.mqtt || {};

        if (String(status.mode || "").toLowerCase() === "provisioning") {
            return {
                kind: "warning",
                message:
                    "Device is in provisioning mode. Finish required settings and reboot to enter normal operation.",
            };
        }

        const otaState = String(ota.state || "").toLowerCase();
        if (otaState && otaState !== "idle") {
            if (otaState.includes("error") || otaState.includes("fail")) {
                return {
                    kind: "error",
                    message: "OTA reports a problem: " + text(ota.message, ota.state),
                };
            }
            if (otaState.includes("success") || otaState.includes("complete")) {
                return {
                    kind: "success",
                    message: "OTA completed. Reboot the device if activation is still pending.",
                };
            }
            return {
                kind: "warning",
                message: "OTA is in progress: " + text(ota.progress_pct, "0") + "% complete.",
            };
        }

        if (Number(health.error_count || 0) > 0) {
            return {
                kind: "error",
                message: "Health monitor reports an error: " + text(health.last_error_msg, "check diagnostics"),
            };
        }
        if (Number(health.warning_count || 0) > 0) {
            return {
                kind: "warning",
                message:
                    "Health monitor has warnings: " + text(health.last_warning_msg, "check diagnostics"),
            };
        }
        if (stickyBanner) {
            return stickyBanner;
        }
        if ((mqtt.held_item || Number(mqtt.outbox_depth || 0) > 0) &&
            String(mqtt.state || "").toLowerCase() !== "connected") {
            return {
                kind: "warning",
                message: "MQTT has queued data waiting for connectivity. Check broker reachability and queue pressure.",
            };
        }
        return null;
    }

    function syncOperatorBanner() {
        const notice = currentOperatorNotice();
        if (!notice) {
            clearBanner();
            return;
        }
        setBanner(notice.kind, notice.message);
    }

    async function bootstrap() {
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

        try {
            const data = await requestJson("/api/bootstrap", fetchOpts, {
                authorize: false,
                handleUnauthorized: false,
            });
            bootstrapInfo = {
                mode: data.mode || "unknown",
                provisioning: toBool(data.provisioning),
                password_set: toBool(data.password_set),
                bootstrap_failed: false,
            };
            return bootstrapInfo;
        } catch (_) {
            bootstrapInfo = {
                mode: "unknown",
                provisioning: false,
                password_set: true,
                bootstrap_failed: true,
            };
            return bootstrapInfo;
        } finally {
            if (timeoutId) {
                clearTimeout(timeoutId);
            }
        }
    }

    async function checkFirstBootByLiveConfig(status) {
        if (!status || String(status.mode || "").toLowerCase() !== "provisioning") {
            return false;
        }
        try {
            const cfg = await api("GET", "/api/config");
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
        } catch (_) {
            return false;
        }
    }

    async function runInitialSetup() {
        const msg = $("#setup-msg");
        msg.hidden = true;

        const ssid = $("#setup-ssid").value.trim();
        const wifiPassword = $("#setup-wifi-password").value;
        const adminPassword = $("#setup-admin-password").value;
        const mqttEnabled = $("#setup-mqtt-enabled").checked;
        const mqttHost = $("#setup-mqtt-host").value.trim();

        if (!ssid) {
            setMsg(msg, "error", "Wi-Fi SSID is required.");
            return;
        }
        if (!adminPassword) {
            setMsg(msg, "error", "Admin password is required.");
            return;
        }
        if (mqttEnabled && !mqttHost) {
            setMsg(msg, "error", "MQTT host is required when MQTT is enabled.");
            return;
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

        try {
            await api("POST", "/api/bootstrap/setup", payload, {
                authorize: false,
                handleUnauthorized: false,
            });
            token = "";
            sessionStorage.removeItem("wg_token");
            setMsg(
                msg,
                "success",
                "Initial setup saved. Reboot is required. After reboot, use normal admin login."
            );
        } catch (err) {
            const issues = err && err.data && Array.isArray(err.data.issues)
                ? err.data.issues.map((i) => i.field + ": " + i.message).join("; ")
                : "";
            const suffix = issues ? " (" + issues + ")" : "";
            setMsg(
                msg,
                "error",
                "Initial setup failed: " + ((err && err.message) || "unknown_error") + suffix
            );
        }
    }

    async function submitWatchlistEntry() {
        const msg = $("#wl-msg");
        clearMsg(msg);
        const key = $("#wl-key").value.trim();
        if (!key) {
            setMsg(msg, "error", "Meter key is required.");
            return;
        }
        try {
            await api("POST", "/api/watchlist", {
                key: key,
                alias: $("#wl-alias").value || "",
                note: $("#wl-note").value || "",
                enabled: $("#wl-enabled").checked,
            });
            setMsg(msg, "success", "Watchlist updated.");
            await loadWatchlist();
            if (currentPage === "meters") {
                await loadDetectedMeters();
            }
        } catch (err) {
            setMsg(msg, "error", errorMessage(err, "Watchlist update failed."));
        }
    }

    async function deleteWatchlistEntry() {
        const msg = $("#wl-msg");
        clearMsg(msg);
        const key = $("#wl-key").value.trim();
        if (!key) {
            setMsg(msg, "error", "Meter key is required.");
            return;
        }
        try {
            await api("POST", "/api/watchlist/delete", { key: key });
            setMsg(msg, "success", "Watchlist entry removed.");
            $("#wl-key").value = "";
            $("#wl-alias").value = "";
            $("#wl-note").value = "";
            $("#wl-enabled").checked = true;
            await loadWatchlist();
            await loadDetectedMeters();
        } catch (err) {
            setMsg(msg, "error", errorMessage(err, "Watchlist delete failed."));
        }
    }

    async function startOtaFromUrl() {
        const url = $("#ota-url").value.trim();
        if (!url) {
            setMsg($("#ota-status"), "warning", "Enter URL first.");
            return;
        }
        try {
            await api("POST", "/api/ota/url", { url: url });
            setMsg($("#ota-status"), "success", "OTA URL update started.");
            cacheOtaStatus = {
                state: "starting",
                progress_pct: 0,
                message: "Waiting for OTA worker to report progress.",
            };
            syncOperatorBanner();
            await loadOtaStatus();
        } catch (err) {
            setMsg($("#ota-status"), "error", "OTA URL failed: " + errorMessage(err, "request failed"));
        }
    }

    async function sendRebootCommand(messageSelector, options) {
        const target = messageSelector ? $(messageSelector) : null;
        try {
            await api("POST", "/api/system/reboot");
            if (target) {
                setMsg(target, "warning", "Reboot command sent. Connection may drop shortly.");
            }
            setStickyBanner("warning", "Reboot command sent. Wait for the device to come back online.");
        } catch (err) {
            if (target) {
                setMsg(target, "error", errorMessage(err, "Reboot failed."));
            }
        }
        if (options && options.refreshStatus) {
            await refreshStatusOnly();
        }
    }

    async function sendFactoryResetCommand() {
        const msg = $("#factory-msg");
        try {
            await api("POST", "/api/system/factory-reset");
            setMsg(msg, "warning", "Factory reset command sent. Device will erase settings and reboot.");
            setStickyBanner("warning", "Factory reset started. Stored settings will be cleared during reboot.");
        } catch (err) {
            setMsg(msg, "error", errorMessage(err, "Factory reset failed."));
        }
    }

    async function exportConfigFile() {
        try {
            const cfg = await api("GET", "/api/config");
            const blob = new Blob([JSON.stringify(cfg, null, 2)], {
                type: "application/json",
            });
            const a = document.createElement("a");
            a.href = URL.createObjectURL(blob);
            a.download = "wmbus-gw-config.json";
            a.click();
        } catch (err) {
            setMsg($("#cfg-msg"), "error", errorMessage(err, "Config export failed."));
        }
    }

    async function downloadSupportBundle() {
        const msg = $("#support-msg");
        clearMsg(msg);
        try {
            const blob = await downloadBlob("/api/support-bundle");
            const a = document.createElement("a");
            a.href = URL.createObjectURL(blob);
            a.download = "support-bundle.json";
            a.click();
            setMsg(msg, "success", "Support bundle downloaded.");
        } catch (err) {
            setMsg(msg, "error", errorMessage(err, "Support bundle download failed."));
        }
    }

    async function loadDashboard() {
        try {
            const [status, metersData, watchlistData] = await Promise.all([
                api("GET", "/api/status"),
                api("GET", "/api/meters/detected"),
                api("GET", "/api/watchlist"),
            ]);
            const meters = metersData.meters || [];
            const counts = {
                detected: meters.length,
                watchCount: (watchlistData.watchlist || []).length,
                duplicateCount: meters.reduce(
                    (sum, meter) => sum + Number(meter.duplicate_count || 0),
                    0
                ),
            };

            cacheStatus = status;
            cacheWatchlist = watchlistData.watchlist || [];
            applyModeUi(status.mode);
            clearMsg($("#dashboard-msg"));
            dashboardCache.duplicateCount = counts.duplicateCount;
            dashboardCache.detected = counts.detected;
            dashboardCache.watchlistCount = counts.watchCount;
            renderDashboard(status, counts);
            syncOperatorBanner();
        } catch (err) {
            setMsg($("#dashboard-msg"), "error", errorMessage(err, "Unable to load dashboard."));
        }
    }

    /** Light dashboard refresh: /api/status only; keeps last meter/watchlist/duplicate totals from full load. */
    async function loadDashboardLight() {
        try {
            const status = await api("GET", "/api/status");
            cacheStatus = status;
            applyModeUi(status.mode);
            clearMsg($("#dashboard-msg"));
            renderDashboard(status, {
                duplicateCount: dashboardCache.duplicateCount,
                detected: dashboardCache.detected,
                watchCount: dashboardCache.watchlistCount,
            });
            syncOperatorBanner();
        } catch (err) {
            setMsg(
                $("#dashboard-msg"),
                "warning",
                errorMessage(err, "Live refresh failed. Showing last known dashboard values.")
            );
        }
    }

    async function refreshStatusOnly() {
        if (currentPage === "dashboard") {
            await loadDashboardLight();
            return;
        }
        try {
            const status = await api("GET", "/api/status");
            cacheStatus = status;
            applyModeUi(status.mode);
            syncOperatorBanner();
        } catch (_) {}
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

    async function loadTelegrams() {
        const filter = $("#tg-filter").value || "all";
        const apiFilter = filter === "problematic" ? "crc_fail" : filter;
        const suffix = apiFilter === "all" ? "" : ("?filter=" + encodeURIComponent(apiFilter));
        const body = $("#tg-body");
        try {
            const [data, watchlist] = await Promise.all([
                api("GET", "/api/telegrams" + suffix),
                api("GET", "/api/watchlist"),
            ]);
            cacheWatchlist = watchlist.watchlist || [];
            const aliases = watchAliasMap();
            const arr = data.telegrams || [];
            arr.sort((a, b) => Number(b.timestamp_ms || 0) - Number(a.timestamp_ms || 0));
            clearChildren(body);
            setEmptyState("#tg-empty", "No telegrams available yet.", arr.length > 0);

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
                    const prev = copyBtn.textContent;
                    copyBtn.textContent = "Copied!";
                    copyBtn.disabled = true;
                    window.setTimeout(() => {
                        copyBtn.textContent = prev;
                        copyBtn.disabled = false;
                    }, 2000);
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
        } catch (err) {
            clearChildren(body);
            setEmptyState("#tg-empty", errorMessage(err, "Unable to load telegrams."), false);
        }
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

    async function loadDetectedMeters() {
        const filter = $("#meters-filter").value || "all";
        const suffix = filter === "all" ? "" : ("?filter=" + encodeURIComponent(filter));
        const search = ($("#meters-search").value || "").trim();
        const body = $("#meters-body");
        try {
            const data = await api("GET", "/api/meters/detected" + suffix);
            const arr = (data.meters || []).filter((m) => matchesMeterSearch(m, search));
            clearChildren(body);
            setEmptyState("#meters-empty", "No meters detected yet.", arr.length > 0);
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
        } catch (err) {
            clearChildren(body);
            setEmptyState("#meters-empty", errorMessage(err, "Unable to load detected meters."), false);
        }
    }

    async function loadWatchlist() {
        const body = $("#watchlist-body");
        try {
            const data = await api("GET", "/api/watchlist");
            cacheWatchlist = data.watchlist || [];
            clearChildren(body);
            setEmptyState("#watchlist-empty", "Watchlist is empty.", cacheWatchlist.length > 0);
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
        } catch (err) {
            clearChildren(body);
            setEmptyState("#watchlist-empty", errorMessage(err, "Unable to load watchlist."), false);
        }
    }

    async function loadDiagnostics() {
        try {
            const [radioData, mqttData, statusData, otaData] = await Promise.all([
                api("GET", "/api/diagnostics/radio"),
                api("GET", "/api/diagnostics/mqtt"),
                api("GET", "/api/status"),
                api("GET", "/api/ota/status"),
            ]);
            const radioEl = $("#rf-stats");
            const mqttEl = $("#mqtt-stats");
            const sysEl = $("#sys-stats");
            const otaEl = $("#diag-ota-stats");
            const summaryEl = $("#diag-summary");
            clearChildren(radioEl);
            clearChildren(mqttEl);
            clearChildren(sysEl);
            clearChildren(otaEl);
            clearChildren(summaryEl);
            cacheStatus = statusData;
            cacheOtaStatus = otaData;
            syncOperatorBanner();

            const rsm = radioData.rsm || {};
            const diag = radioData.diagnostics || {};
            const rc = diag.radio_counters || {};
            appendKvRows(summaryEl, [
                ["Health", healthSummary(statusData.health || {})],
                ["MQTT Queue", mqttQueueSummary(mqttData)],
                ["Radio", radioSummary(radioData)],
                ["OTA", otaSummary(otaData)],
            ]);

            appendKvRows(radioEl, [
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
            ]);

            appendKvRows(mqttEl, [
                ["State", mqttData.state],
                ["Broker", mqttData.broker_uri],
                ["Queue State", mqttQueueSummary(mqttData)],
                ["Publish Count", mqttData.publish_count],
                ["Publish Failures", mqttData.publish_failures],
                ["Reconnects", mqttData.reconnect_count],
                ["Held Item", mqttData.held_item ? "yes" : "no"],
                ["Hold Count", mqttData.hold_count],
                ["Retry Attempts", mqttData.retry_count],
                ["Retry Failures", mqttData.retry_failure_count],
                ["Outbox", text(mqttData.outbox_depth, 0) + " / " + text(mqttData.outbox_capacity, 0)],
                ["Last Publish (ms)", mqttData.last_publish_epoch_ms],
            ]);

            const health = statusData.health || {};
            const metrics = statusData.metrics || {};
            const wifi = statusData.wifi || {};
            appendKvRows(sysEl, [
                ["Health", health.state],
                ["Health Detail", healthSummary(health)],
                ["Uptime", formatUptime(metrics.uptime_s)],
                ["Heap Free", metrics.free_heap_bytes],
                ["Heap Min", metrics.min_free_heap_bytes],
                ["Largest Block", metrics.largest_free_block],
                ["Wi-Fi State", wifi.state],
                ["Wi-Fi IP", wifi.ip_address],
                ["Wi-Fi RSSI", wifi.rssi_dbm],
            ]);

            appendKvRows(otaEl, [
                ["State", otaData.state],
                ["Summary", otaSummary(otaData)],
                ["Progress", text(otaData.progress_pct, "0") + "%"],
                ["Message", otaData.message],
                ["Version", otaData.current_version],
            ]);
        } catch (err) {
            const message = errorMessage(err, "Unable to load diagnostics.");
            renderKvLoadError("#diag-summary", message);
            renderKvLoadError("#rf-stats", message);
            renderKvLoadError("#mqtt-stats", message);
            renderKvLoadError("#sys-stats", message);
            renderKvLoadError("#diag-ota-stats", message);
        }
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

    async function loadConfig() {
        try {
            $("#cfg-msg").hidden = true;
            const cfg = await api("GET", "/api/config");
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
        } catch (err) {
            clearChildren($("#config-form-container"));
            setMsg($("#cfg-msg"), "error", errorMessage(err, "Unable to load settings."));
        }
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

    async function loadOtaStatus() {
        try {
            const d = await api("GET", "/api/ota/status");
            cacheOtaStatus = d;
            const otaGrid = $("#ota-state-grid");
            clearChildren(otaGrid);
            appendKvRows(otaGrid, [
                ["State", d.state],
                ["Summary", otaSummary(d)],
                ["Progress", text(d.progress_pct, "0") + "%"],
                ["Current Version", d.current_version],
                ["Message", d.message],
            ]);
            $("#ota-status").textContent = "Status: " + text(d.state);
            syncOperatorBanner();
        } catch (err) {
            renderKvLoadError("#ota-state-grid", errorMessage(err, "Unable to load OTA status."));
            setMsg($("#ota-status"), "error", errorMessage(err, "Unable to load OTA status."));
        }
    }

    async function loadSupport() {
        const summary = $("#support-summary");
        clearChildren(summary);
        try {
            const [status, ota, watch] = await Promise.all([
                api("GET", "/api/status"),
                api("GET", "/api/ota/status"),
                api("GET", "/api/watchlist"),
            ]);
            cacheStatus = status;
            cacheOtaStatus = ota;
            syncOperatorBanner();
            appendKvRows(summary, [
                ["Firmware", status.firmware_version],
                ["Mode", status.mode],
                ["Health", healthSummary(status.health || {})],
                ["MQTT", status.mqtt ? status.mqtt.state : "--"],
                ["MQTT Queue", mqttQueueSummary(status.mqtt || {})],
                ["Watchlist Entries", (watch.watchlist || []).length],
                ["OTA", otaSummary(ota)],
            ]);
        } catch (err) {
            renderKvLoadError("#support-summary", errorMessage(err, "Unable to load support summary."));
        }
    }

    async function loadLogs() {
        try {
            const data = await api("GET", "/api/logs");
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
            setEmptyState("#logs-empty", "No logs available.", filtered.length > 0);
        } catch (err) {
            $("#log-output").textContent = "";
            setEmptyState("#logs-empty", errorMessage(err, "Unable to load logs."), false);
        }
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

        $("#btn-logout").addEventListener("click", async () => {
            try {
                await api("POST", "/api/auth/logout");
            } catch (_) {}
            showSessionExpiredSignIn("Signed out.", "success");
        });

        $("#login-form").addEventListener("submit", async (e) => {
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
            try {
                const data = await api("POST", "/api/auth/login", { password: pwd }, {
                    authorize: false,
                    handleUnauthorized: false,
                });
                if (!data.token) {
                    throw new Error("No auth token");
                }
                token = data.token;
                sessionStorage.setItem("wg_token", token);
                $("#login-error").hidden = true;
                showApp();
            } catch (err) {
                const retry = err && err.data && err.data.retry_after_s
                    ? " Try again in " + err.data.retry_after_s + "s."
                    : "";
                setMsg($("#login-error"), "error", (err.message || "Login failed") + retry);
            }
        });

        $("#setup-mqtt-enabled").addEventListener("change", (e) => {
            applySetupMqttEnabled(e.target.checked);
        });

        $("#setup-form").addEventListener("submit", async (e) => {
            e.preventDefault();
            await runInitialSetup();
        });

        $("#cfg-save").addEventListener("click", async () => {
            const msg = $("#cfg-msg");
            msg.hidden = true;
            try {
                const cfg = await api("GET", "/api/config");
                collectConfigValues(cfg);
                const resp = await api("POST", "/api/config", cfg);
                if (resp.relogin_required) {
                    stickyBanner = null;
                    showSessionExpiredSignIn(
                        "Settings saved. Sign in again because authentication settings changed.",
                        "warning"
                    );
                    return;
                }
                if (resp.reboot_required) {
                    setMsg(msg, "warning", "Saved. Reboot required to apply runtime settings.");
                    setStickyBanner(
                        "warning",
                        "Runtime settings changed. Reboot the device to apply the new configuration."
                    );
                } else {
                    setMsg(msg, "success", "Settings saved.");
                    syncOperatorBanner();
                }
            } catch (err) {
                const issues = err && err.data && err.data.issues ? err.data.issues : [];
                if (issues.length > 0) {
                    const detail = issues.map((i) => i.field + ": " + i.message).join("; ");
                    setMsg(msg, "error", "Validation failed: " + detail);
                } else {
                    setMsg(msg, "error", "Save failed.");
                }
            }
        });

        $("#cfg-export").addEventListener("click", async () => {
            await exportConfigFile();
        });

        $("#wl-save").addEventListener("click", submitWatchlistEntry);

        $("#wl-delete").addEventListener("click", deleteWatchlistEntry);

        $("#ota-upload-btn").addEventListener("click", async () => {
            const file = $("#ota-file").files[0];
            if (!file) {
                setMsg($("#ota-status"), "warning", "Select a firmware file first.");
                return;
            }
            setMsg($("#ota-status"), "warning", "Uploading firmware...");
            try {
                const resp = await requestJson("/api/ota/upload", {
                    method: "POST",
                    headers: { "Content-Type": "application/octet-stream" },
                    body: file,
                });
                const detail = resp.detail || "Upload complete. Reboot required.";
                cacheOtaStatus = {
                    state: "uploaded",
                    progress_pct: 100,
                    message: detail,
                    current_version: cacheOtaStatus ? cacheOtaStatus.current_version : "",
                };
                setMsg($("#ota-status"), "success", detail);
                setStickyBanner(
                    "warning",
                    "Firmware upload finished. Reboot the device when you are ready to activate it."
                );
                loadOtaStatus();
            } catch (err) {
                setMsg($("#ota-status"), "error", "Upload failed: " + err.message);
            }
        });

        $("#ota-url-btn").addEventListener("click", startOtaFromUrl);

        $("#sys-bundle").addEventListener("click", async () => {
            await downloadSupportBundle();
        });

        $("#sys-reboot").addEventListener("click", async () => {
            if (!confirm("Reboot device now?")) {
                return;
            }
            await sendRebootCommand("#factory-msg");
        });

        $("#dash-reboot").addEventListener("click", async () => {
            if (confirm("Reboot device now?")) {
                await sendRebootCommand(null, { refreshStatus: true });
            }
        });

        $("#sys-reset").addEventListener("click", async () => {
            if (!confirm("Factory reset will erase configuration and reboot. Continue?")) {
                return;
            }
            await sendFactoryResetCommand();
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

    async function initializeApp() {
        const boot = await bootstrap();
        bootstrapInfo = boot;
        const firstBoot = !!(boot.provisioning && !boot.password_set);
        const stored = (sessionStorage.getItem("wg_token") || "").trim();

        if (firstBoot) {
            forceFirstBootSetup();
            return;
        }

        if (!stored) {
            showStartupUnauthenticated(boot);
            return;
        }

        token = stored;
        try {
            const status = await api("GET", "/api/status");
            cacheStatus = status;
            applyModeUi(status.mode);
            showApp();
        } catch (err) {
            if (err && err.message === "unauthorized") {
                return;
            }
            showSessionExpiredSignIn(
                "Stored session could not be restored. Sign in again to continue.",
                "warning"
            );
        }
    }

    initializeApp();
})();
