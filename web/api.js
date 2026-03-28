export function createApiClient({ getToken, onUnauthorized }) {
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
        const token = getToken ? getToken() : "";
        if (settings.authorize && token) {
            headers.Authorization = "Bearer " + token;
        }
        requestOptions.headers = headers;

        const response = await fetch(path, requestOptions);
        if (settings.handleUnauthorized && response.status === 401) {
            if (onUnauthorized) {
                onUnauthorized();
            }
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

    return {
        readJsonBody,
        request,
        requestJson,
        api,
        downloadBlob,
    };
}
