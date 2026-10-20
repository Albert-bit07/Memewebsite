const API_URL = '';
let memeQueue = [];
let likedMemes = new Set();
let isLoadingMore = false;
let exhausted = false;
let observer;
const seen = new Set();

async function api(path, body) {
    const response = await fetch(API_URL + path, body === undefined ? {} : {
        method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)
    });
    if (!response.ok) throw new Error(`Request failed (${response.status})`);
    return response.json();
}
async function init() {
    try {
        await api('/session', {});
        const status = await api('/status');
        likedMemes = new Set(status.liked_indices);
        document.getElementById('totalCount').textContent = status.paths_loaded;
        document.getElementById('likedCount').textContent = status.liked_count;
        await loadMoreMemes();
    } catch (error) { showError(error.message); }
}
async function loadMoreMemes() {
    if (isLoadingMore || exhausted) return;
    isLoadingMore = true;
    document.getElementById('loading').style.display = 'block';
    try {
        const data = await api('/recommend');
        exhausted = data.recommendations.length === 0;
        for (const meme of data.recommendations) {
            if (!memeQueue.some(existing => existing.index === meme.index)) {
                memeQueue.push(meme);
                appendCard(meme);
            }
        }
        if (!memeQueue.length) showError('You have seen every image in this catalog. New catalog items will appear here.');
    } catch (error) { showError(error.message); }
    finally {
        document.getElementById('loading').style.display = 'none';
        isLoadingMore = false;
    }
}
function appendCard(meme) {
    const card = document.createElement('div');
    card.className = 'meme-card';
    const image = document.createElement('img');
    image.src = `/image/${meme.index}`;
    image.alt = 'Meme ' + meme.id;
    const button = document.createElement('button');
    button.className = 'like-button';
    const update = () => {
        button.classList.toggle('liked', likedMemes.has(meme.index));
        button.textContent = likedMemes.has(meme.index) ? '❤️' : '🤍';
    };
    update();
    button.onclick = async event => {
        event.stopPropagation(); button.disabled = true;
        try {
            const liked = likedMemes.has(meme.index);
            const result = await api('/feedback', {index:meme.index,action:liked ? 'unlike':'like'});
            if (liked) likedMemes.delete(meme.index); else likedMemes.add(meme.index);
            document.getElementById('likedCount').textContent = result.liked_count;
            update();
        } catch (error) { alert(error.message); }
        finally { button.disabled = false; }
    };
    card.append(image, button);
    document.getElementById('feedContainer').append(card);
    card.dataset.index = meme.index;
    observer.observe(card);
}
observer = new IntersectionObserver(async entries => {
    for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        const index = Number(entry.target.dataset.index);
        if (seen.has(index)) continue;
        seen.add(index);
        try {
            await api('/feedback', {index,action:'seen'});
            observer.unobserve(entry.target);
            if (memeQueue.slice(-3).some(meme => meme.index === index)) await loadMoreMemes();
        } catch (error) { seen.delete(index); console.error(error); }
    }
}, {threshold:0.5});
async function getRecommendations() {
    observer.disconnect(); memeQueue = []; exhausted = false;
    document.getElementById('feedContainer').replaceChildren();
    await loadMoreMemes();
}
function showError(message) {
    document.getElementById('loading').style.display = 'none';
    const notice = document.createElement('p');
    notice.className = 'empty-state'; notice.textContent = message;
    document.getElementById('feedContainer').append(notice);
}
document.addEventListener('keydown', event => {
    if (event.key.toLowerCase() === 'r') getRecommendations();
});
init();
