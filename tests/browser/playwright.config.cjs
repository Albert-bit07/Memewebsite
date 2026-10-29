const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: '.',
  testMatch: '*.spec.cjs',
  workers: 1,
  retries: 0,
  timeout: 30000,
  use: { browserName: 'chromium', baseURL: 'http://127.0.0.1:18080' },
});
