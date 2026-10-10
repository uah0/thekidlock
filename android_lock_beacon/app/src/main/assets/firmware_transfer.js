(() => {
  if (window.bleLockTransferHook) return;
  window.bleLockTransferHook = true;
  const originalFetch = window.fetch.bind(window);
  let imageVersion = '';
  window.fetch = async function(input, options) {
    const target = new URL(typeof input === 'string' ? input : input.url, location.href);
    const method = (options?.method || input?.method || 'GET').toUpperCase();
    if (target.origin === location.origin && target.pathname === '/api/ota' && method === 'POST') {
      if (!imageVersion || !BleLockFirmwareTransfer.started(imageVersion)) {
        throw new Error('Не удалось сохранить номер прошивки. Подключите телефон к интернету и дождитесь скачивания обновления.');
      }
    }
    const response = await originalFetch(input, options);
    if (target.origin === location.origin && target.pathname === '/__app/firmware' && response.ok) {
      imageVersion = response.headers.get('X-Firmware-Version') || '';
    }
    return response;
  };
})();
