// Controlled DOM/fetch contract test; this is not browser end-to-end coverage.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

function element() {
    return {style: {}, dataset: {}, children: [], classList: {toggle() {}},
        append(...children) { this.children.push(...children); },
        replaceChildren() { this.children = []; }};
}
const elements = new Map();
const requests = [];
const item = {id: 'folder/a + b?.jpg', index: 7};
let observe;
const context = vm.createContext({
    console,
    document: {
        getElementById(id) {
            if (!elements.has(id)) elements.set(id, element());
            return elements.get(id);
        },
        createElement: element,
        addEventListener() {},
    },
    IntersectionObserver: class {
        constructor(callback) { observe = callback; }
        observe() {}
        unobserve() {}
        disconnect() {}
    },
    alert(message) { throw new Error(message); },
    fetch: async (url, options) => {
        requests.push({url, body: options.body && JSON.parse(options.body)});
        const result = url === '/status' ? {liked_ids: [], paths_loaded: 1, liked_count: 0}
            : url === '/recommend' ? {recommendations: [item]} : {liked_count: 1};
        return {ok: true, json: async () => result};
    },
});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../frontend/script.js'), 'utf8'), context);

(async () => {
    // Drain initialization's asynchronous fetch/json continuations.
    await new Promise(resolve => setImmediate(resolve));
    const card = elements.get('feedContainer').children[0];
    const [image, button] = card.children;
    assert.equal(image.src, '/image?id=' + encodeURIComponent(item.id));
    assert.equal(card.dataset.id, item.id);
    await button.onclick({stopPropagation() {}});
    await button.onclick({stopPropagation() {}});
    await observe([{isIntersecting: true, target: card}]);
    assert.deepEqual(requests.filter(r => r.url === '/feedback').map(r => r.body), [
        {id: item.id, action: 'like'},
        {id: item.id, action: 'unlike'},
        {id: item.id, action: 'seen'},
    ]);
    console.log('Frontend stable-ID image, like, unlike and visibility contracts passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
