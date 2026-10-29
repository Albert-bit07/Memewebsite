const { test, expect } = require('@playwright/test');
const { spawn } = require('node:child_process');
const { mkdtemp, rm } = require('node:fs/promises');
const { tmpdir } = require('node:os');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const base = 'http://127.0.0.1:18080';
const binary = process.env.MEME_SERVER_BINARY;
let server;
let directory;

async function start() {
  if (!binary || !path.isAbsolute(binary)) throw new Error('MEME_SERVER_BINARY must be an absolute executable path');
  let occupied = false;
  try { await fetch(base, { signal: AbortSignal.timeout(1000) }); occupied = true; } catch {}
  if (occupied) throw new Error('Port 18080 is already occupied; refusing to reuse another server');
  server = spawn(binary, [
    path.join(root, 'backend/data/catalog.json'),
    path.join(directory, 'users.sqlite3'),
    path.join(root, 'frontend'),
  ], { stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  let spawnError;
  server.on('error', error => { spawnError = error; });
  server.stdout.on('data', chunk => { output += chunk; });
  server.stderr.on('data', chunk => { output += chunk; });
  await expect.poll(async () => {
    if (spawnError) throw spawnError;
    if (server.exitCode !== null) throw new Error(`Server exited: ${output}`);
    try { return (await fetch(base + '/')).status; } catch { return 0; }
  }, { timeout: 10000 }).toBe(200);
}

async function stop() {
  if (!server?.pid || server.exitCode !== null || server.signalCode !== null) return;
  const child = server;
  await new Promise(resolve => {
    const timer = setTimeout(() => child.kill('SIGKILL'), 5000);
    child.once('exit', () => { clearTimeout(timer); resolve(); });
    child.kill('SIGTERM');
  });
}

test.beforeEach(async () => {
  directory = await mkdtemp(path.join(tmpdir(), 'meme-browser-'));
  await start();
});
test.afterEach(async () => {
  await stop();
  if (directory) await rm(directory, { recursive: true, force: true });
});

test('independent browser sessions, like/unlike and restart persistence', async ({ browser }) => {
  const alice = await browser.newContext();
  const bob = await browser.newContext();
  try {
    const a = await alice.newPage();
    const b = await bob.newPage();
    const errors = [];
    a.on('pageerror', error => errors.push(error.message));
    b.on('pageerror', error => errors.push(error.message));
    await Promise.all([a.goto(base), b.goto(base)]);
    const button = a.locator('.like-button').first();
    await expect(button).toBeVisible();
    await expect(a.locator('.recommendation-reason').first()).toHaveText('Explore the catalog · ordered for this session');
    await expect(b.locator('.meme-card').first()).toBeVisible();
    await expect.poll(() => a.locator('.meme-card img').first().evaluate(img => img.complete && img.naturalWidth > 0)).toBe(true);
    await button.click();
    await expect(a.locator('#likedCount')).toHaveText('1');
    await expect(button).toHaveClass(/liked/);
    await expect(b.locator('#likedCount')).toHaveText('0');
    expect((await (await bob.request.get(base + '/status')).json()).liked_count).toBe(0);
    await button.click();
    await expect(a.locator('#likedCount')).toHaveText('0');
    await expect(button).not.toHaveClass(/liked/);
    expect((await (await alice.request.get(base + '/status')).json()).liked_count).toBe(0);
    await button.click();
    await expect(a.locator('#likedCount')).toHaveText('1');
    const liked = (await (await alice.request.get(base + '/status')).json()).liked_ids;
    expect(liked).toHaveLength(1);
    await stop();
    await start();
    await Promise.all([a.reload(), b.reload()]);
    await expect(a.locator('#likedCount')).toHaveText('1');
    await expect(b.locator('#likedCount')).toHaveText('0');
    expect((await (await alice.request.get(base + '/status')).json()).liked_ids).toEqual(liked);
    await expect(a.locator('.meme-card').first()).toBeVisible();
    await expect(a.locator('.recommendation-reason').first()).toHaveText('Ranked by similarity to your likes');
    await expect(b.locator('.recommendation-reason').first()).toHaveText('Explore the catalog · ordered for this session');
    expect(errors).toEqual([]);
  } finally {
    await alice.close();
    await bob.close();
  }
});

test('failed recommendation request can be retried from the refresh button', async ({ page }) => {
  await page.route('**/recommend', route => route.abort('failed'), { times: 1 });
  await page.goto(base);
  await expect(page.locator('.empty-state')).toContainText('Failed to fetch');
  await page.getByTitle('Refresh recommendations').click();
  await expect(page.locator('.meme-card').first()).toBeVisible();
  await expect(page.locator('.empty-state')).toHaveCount(0);
  await expect(page.locator('#loading')).toBeHidden();
});
