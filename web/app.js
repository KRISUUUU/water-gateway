import { $, $$, setHiddenIfPresent } from "./ui_common.js";
import { clearMsg } from "./page_messages.js";
import { createApiClient } from "./api.js";
import { createBannerController } from "./ui_banner.js";
import { createAuthController } from "./auth.js";
import { createDashboardController } from "./dashboard.js";
import { createDataViewsController } from "./data_views.js";
import { createDiagnosticsController } from "./diagnostics.js";
import { createOtaController } from "./ota.js";
import { createSettingsController } from "./settings.js";
import { createWatchlistController } from "./watchlist.js";

const state = {
    token: sessionStorage.getItem("wg_token") || "",
    currentPage: "dashboard",
    cacheStatus: null,
    cacheWatchlist: [],
    refreshTimer: null,
    heavyRefreshTimer: null,
    dashboardCache: { duplicateCount: 0, detected: 0, watchlistCount: 0 },
    bootstrapInfo: null,
    cacheOtaStatus: null,
    stickyBanner: null,
};

let authController = null;

const bannerController = createBannerController({ state });
const apiClient = createApiClient({
    getToken: () => state.token,
    onUnauthorized: () => {
        if (authController) {
            authController.showSessionExpiredSignIn(
                "Session expired. Sign in again to continue.",
                "warning"
            );
        }
    },
});

function updateStatus(status) {
    state.cacheStatus = status;
    if (authController) {
        authController.applyModeUi(status.mode);
    }
}

function updateWatchlist(watchlist) {
    state.cacheWatchlist = watchlist || [];
}

function updateOtaStatus(status) {
    state.cacheOtaStatus = status || null;
}

function stopRefreshTimer() {
    if (state.refreshTimer) {
        clearInterval(state.refreshTimer);
        state.refreshTimer = null;
    }
    if (state.heavyRefreshTimer) {
        clearInterval(state.heavyRefreshTimer);
        state.heavyRefreshTimer = null;
    }
}

function showApp() {
    $("#login-page").hidden = true;
    $("#app-shell").hidden = false;
    bannerController.sync();
    showPage(state.currentPage);
    startRefresh();
}

authController = createAuthController({
    state,
    apiClient,
    bannerController,
    stopRefreshTimer,
    showApp,
    updateStatus,
});

const watchlistController = createWatchlistController({
    state,
    apiClient,
    updateWatchlist,
    getCurrentPage: () => state.currentPage,
    showPage,
});

const dataViewsController = createDataViewsController({
    apiClient,
    updateWatchlist,
    watchlistController,
});
watchlistController.setDetectedMetersLoader(dataViewsController.loadDetectedMeters);

const dashboardController = createDashboardController({
    state,
    apiClient,
    updateStatus,
    updateWatchlist,
    bannerController,
});

const diagnosticsController = createDiagnosticsController({
    apiClient,
    updateStatus,
    updateOtaStatus,
    updateWatchlist,
    bannerController,
});

const settingsController = createSettingsController({
    state,
    apiClient,
    bannerController,
    showSessionExpiredSignIn: authController.showSessionExpiredSignIn,
});

const otaController = createOtaController({
    apiClient,
    updateOtaStatus,
    bannerController,
    refreshStatusOnly: () => dashboardController.refreshStatusOnly(),
});

function loadPage(name) {
    if (name === "dashboard") {
        dashboardController.loadDashboard();
    } else if (name === "telegrams") {
        dataViewsController.loadTelegrams();
    } else if (name === "meters") {
        dataViewsController.loadDetectedMeters();
    } else if (name === "watchlist") {
        watchlistController.loadWatchlist();
    } else if (name === "diagnostics") {
        diagnosticsController.loadDiagnostics();
    } else if (name === "logs") {
        diagnosticsController.loadLogs();
    } else if (name === "ota") {
        otaController.loadOtaStatus();
    } else if (name === "settings") {
        settingsController.loadConfig();
    } else if (name === "support") {
        diagnosticsController.loadSupport();
    } else if (name === "factory-reset") {
        clearMsg("#factory-msg");
    }
}

function showPage(name) {
    state.currentPage = name;
    $$(".page").forEach((page) => {
        if (page.id !== "login-page") {
            page.hidden = true;
        }
    });
    const page = $("#" + name + "-page");
    if (page) {
        page.hidden = false;
    }
    $$(".nav-btn[data-page]").forEach((button) => {
        button.classList.toggle("active", button.dataset.page === name);
    });
    $("#sidebar").classList.remove("open");
    loadPage(name);
}

function refreshHeavyIfNeeded() {
    if (state.currentPage === "telegrams") {
        dataViewsController.loadTelegrams();
    } else if (state.currentPage === "meters") {
        dataViewsController.loadDetectedMeters();
    } else if (state.currentPage === "watchlist") {
        watchlistController.loadWatchlist();
    } else if (state.currentPage === "dashboard") {
        dashboardController.loadDashboard();
    }
}

function startRefresh() {
    stopRefreshTimer();
    state.refreshTimer = setInterval(() => {
        dashboardController.refreshStatusOnly();
    }, 10000);
    state.heavyRefreshTimer = setInterval(() => {
        refreshHeavyIfNeeded();
    }, 60000);
}

function bindAppEvents() {
    $$(".nav-btn[data-page]").forEach((button) => {
        button.addEventListener("click", () => showPage(button.dataset.page));
    });
    $("#menu-toggle").addEventListener("click", () => {
        $("#sidebar").classList.toggle("open");
    });
    $$("[data-jump]").forEach((button) => {
        button.addEventListener("click", () => showPage(button.dataset.jump));
    });

    authController.bindEvents();
    watchlistController.bindEvents();
    dataViewsController.bindEvents();
    diagnosticsController.bindEvents();
    settingsController.bindEvents();
    otaController.bindEvents();
}

bindAppEvents();
authController.applySetupMqttEnabled(false);
$("#app-shell").hidden = true;
$("#login-page").hidden = false;
setHiddenIfPresent("#auth-startup-msg", false);
setHiddenIfPresent("#login-form", true);
setHiddenIfPresent("#setup-form", true);

authController.initializeApp();
