(() => {
    const overlay = document.getElementById('loading');
    const label = document.getElementById('loading-label');
    const progress = document.getElementById('loading-progress');
    const detail = document.getElementById('loading-detail');
    const retry = document.getElementById('loading-retry');
    const picker = document.getElementById('rom-picker');
    const canvas = document.getElementById('canvas');

    const formatBytes = bytes => bytes < 1024 * 1024
        ? `${Math.round(bytes / 1024)} KB` : `${(bytes / (1024 * 1024)).toFixed(1)} MB`;

    function showLoading(message, received = 0, total = 0) {
        overlay.hidden = false;
        overlay.setAttribute('aria-busy', 'true');
        if (label.textContent !== message) label.textContent = message;
        retry.hidden = true;
        if (total > 0) {
            progress.value = Math.min(received / total, 1);
            detail.textContent = `${formatBytes(received)} / ${formatBytes(total)}`;
        } else {
            progress.removeAttribute('value');
            detail.textContent = received ? `${formatBytes(received)} downloaded` : '';
        }
    }

    function hideLoading() {
        if (overlay.hidden) return;
        overlay.hidden = true;
        overlay.setAttribute('aria-busy', 'false');
    }

    function showError(message, action) {
        showLoading(message);
        overlay.setAttribute('aria-busy', 'false');
        progress.value = 0;
        retry.hidden = false;
        retry.onclick = action;
    }

    async function download(url, message, signal, current = () => true) {
        const response = await fetch(url, { signal });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        // Compressed Content-Length describes wire bytes; response streams are
        // decoded by the browser, so only use the length for unencoded bodies.
        const total = response.headers.get('content-encoding') ? 0
            : Number(response.headers.get('content-length')) || 0;
        if (!response.body) return new Uint8Array(await response.arrayBuffer());
        const reader = response.body.getReader();
        const chunks = [];
        let received = 0;
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            received += value.length;
            if (current()) showLoading(message, received, total);
        }
        const bytes = new Uint8Array(received);
        let offset = 0;
        for (const chunk of chunks) {
            bytes.set(chunk, offset);
            offset += chunk.length;
        }
        return bytes;
    }

    const skins = new Map();
    let activeSkin;
    window.Module = {
        canvas,
        instantiateWasm(imports, receiveInstance) {
            showLoading('Downloading emulator...');
            download(new URL('index.wasm', document.baseURI), 'Downloading emulator...')
                .then(bytes => {
                    showLoading('Starting emulator...');
                    return WebAssembly.instantiate(bytes, imports);
                })
                .then(result => receiveInstance(result.instance, result.module))
                .catch(error => showError(`Cannot start emulator: ${error.message}`, () => location.reload()));
            return {};
        },
        preRun: [() => {
            const { FS, IDBFS } = Module;
            FS.mkdir('/persist');
            FS.mount(IDBFS, {}, '/persist');
            Module.addRunDependency('restore-state');
            FS.syncfs(true, error => {
                if (error) console.warn('Saved state unavailable:', error);
                Module.removeRunDependency('restore-state');
            });
        }],
        onRuntimeInitialized: hideLoading,
        onAbort: reason => showError(`Cannot start emulator: ${reason}`, () => location.reload()),
        requestSkin(name) {
            if (activeSkin && activeSkin.name !== name && activeSkin.phase === 'loading') {
                activeSkin.controller.abort();
                skins.delete(activeSkin.name);
            }
            let entry = skins.get(name);
            if (entry) {
                activeSkin = entry;
                if (entry.phase === 'ready') {
                    hideLoading();
                    return 1;
                }
                return entry.phase === 'error' ? -1 : 0;
            }
            entry = { name, phase: 'loading', controller: new AbortController() };
            skins.set(name, entry);
            activeSkin = entry;
            showLoading('Downloading keyboard image...');
            download(new URL(`assets/${encodeURIComponent(name)}`, document.baseURI),
                'Downloading keyboard image...', entry.controller.signal, () => activeSkin === entry)
                .then(bytes => {
                    if (activeSkin !== entry) return;
                    Module.FS.mkdirTree('/assets');
                    Module.FS.writeFile(`/assets/${name}`, bytes);
                    entry.phase = 'ready';
                })
                .catch(error => {
                    if (activeSkin !== entry || error.name === 'AbortError') return;
                    entry.phase = 'error';
                    showError(`Cannot load keyboard image: ${error.message}`, () => {
                        skins.delete(name);
                        activeSkin = undefined;
                        Module.requestSkin(name);
                    });
                });
            return 0;
        },
        skinFailed(name) {
            const entry = skins.get(name);
            if (entry) entry.phase = 'error';
            showError('Cannot display keyboard image.', () => {
                skins.delete(name);
                activeSkin = undefined;
                Module.requestSkin(name);
            });
        }
    };

    picker.addEventListener('change', async () => {
        if (!picker.files.length) return;
        const file = picker.files[0];
        picker.value = ''; // Allow retrying the same ROM after a failed upload.
        try {
            const bytes = new Uint8Array(await file.arrayBuffer());
            Module.FS.writeFile('/selected.bin', bytes);
            Module.ccall('el9950_web_load_rom', 'number', ['string'], ['/selected.bin']);
        } catch (error) {
            showError(`Cannot load ROM: ${error.message}`, hideLoading);
        }
    });
    window.addEventListener('error', event => {
        if (event.target instanceof HTMLScriptElement)
            showError('Cannot download emulator.', () => location.reload());
    }, true);
})();
