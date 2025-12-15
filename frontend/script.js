const API_URL = 'http://127.0.0.1:18080';
let memeQueue = [];
let likedMemes = new Set();
let likedCount = 0;
let isLoadingMore = false;

async function init() {
    try {
        const status = await fetch(`${API_URL}/status`).then(r => r.json());
        document.getElementById('totalCount').textContent = status.paths_loaded;
        likedCount = status.liked_count;
        document.getElementById('likedCount').textContent = likedCount;
        
        // Load initial batch of memes
        await loadMoreMemes();
    } catch (error) {
        console.error('Init error:', error);
        showError('Failed to connect to server. Make sure the backend is running on port 18080!');
    }
}

async function loadMoreMemes() {
    if (isLoadingMore) return;
    isLoadingMore = true;
    
    try {
        document.getElementById('loading').style.display = 'block';
        
        // Get personalized recommendations based on current likes
        const response = await fetch(`${API_URL}/recommend`);
        const data = await response.json();
        
        console.log('Received recommendations:', data);
        
        const newMemes = data.recommendations.map(meme => ({
            path: meme.path,
            index: meme.index,
            url: `${API_URL}/image/${meme.index}`,
            filename: meme.path.split('\\').pop().split('/').pop()
        }));
        
        // Add new memes to queue (avoid duplicates)
        newMemes.forEach(meme => {
            if (!memeQueue.some(m => m.index === meme.index)) {
                memeQueue.push(meme);
            }
        });
        
        console.log('Meme queue now has', memeQueue.length, 'memes');
        
        renderFeed();
        document.getElementById('loading').style.display = 'none';
        
    } catch (error) {
        console.error('Error loading memes:', error);
        document.getElementById('loading').style.display = 'none';
    } finally {
        isLoadingMore = false;
    }
}

function getCurrentMemeIndexFromPath(path) {
    // Get all paths and find the index
    // For now, we'll extract from the recommendation endpoint
    // The backend should send the actual index, but we can work around it
    const filename = path.split('\\').pop().split('/').pop();
    return memeQueue.length; // Simple incrementing for now
}

function renderFeed() {
    const container = document.getElementById('feedContainer');
    container.innerHTML = '';

    if (memeQueue.length === 0) {
        showEmpty();
        return;
    }

    console.log('Rendering', memeQueue.length, 'memes');

    memeQueue.forEach((meme, idx) => {
        const card = document.createElement('div');
        card.className = 'meme-card';
        card.dataset.memeIndex = idx;
        
        const isLiked = likedMemes.has(meme.index);
        
        card.innerHTML = `
            <img src="${meme.url}" 
                 alt="Meme ${idx + 1}" 
                 onerror="handleImageError(this, ${idx})">
            <div class="meme-index">${idx + 1} / ${memeQueue.length}</div>
            <button class="like-button ${isLiked ? 'liked' : ''}" 
                    data-index="${meme.index}"
                    onclick="toggleLike(${meme.index}, ${idx}, this, event)">
                ${isLiked ? '❤️' : '🤍'}
            </button>
            ${idx === 0 ? '<div class="scroll-hint">👇 Scroll for more</div>' : ''}
        `;
        container.appendChild(card);
    });
    
    // Set up intersection observer to load more when near the end
    setupInfiniteScroll();
}

function setupInfiniteScroll() {
    const container = document.getElementById('feedContainer');
    const cards = container.querySelectorAll('.meme-card');
    
    if (cards.length === 0) return;
    
    const lastCard = cards[cards.length - 1];
    
    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                console.log('Near end of feed, loading more...');
                loadMoreMemes();
            }
        });
    }, {
        threshold: 0.5
    });
    
    observer.observe(lastCard);
}

function handleImageError(img, idx) {
    console.error('Failed to load image:', img.src);
    const meme = memeQueue[idx];
    img.src = `data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' width='400' height='400'><rect width='400' height='400' fill='%23333'/><text x='50%' y='45%' dominant-baseline='middle' text-anchor='middle' font-size='20' fill='%23999'>Meme ${idx + 1}</text><text x='50%' y='55%' dominant-baseline='middle' text-anchor='middle' font-size='14' fill='%23666'>${meme.filename}</text></svg>`;
}

async function toggleLike(memeIndex, queueIndex, button, event) {
    if (event) event.stopPropagation();
    
    console.log('Toggle like for meme index:', memeIndex);
    
    const isLiked = likedMemes.has(memeIndex);
    
    try {
        const response = await fetch(`${API_URL}/feedback`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                action: isLiked ? 'skip' : 'like', 
                index: memeIndex 
            })
        });
        
        if (!response.ok) {
            throw new Error('Feedback request failed');
        }
        
        const result = await response.json();
        console.log('Feedback response:', result);
        
        if (isLiked) {
            likedMemes.delete(memeIndex);
            button.classList.remove('liked');
            button.textContent = '🤍';
            likedCount--;
        } else {
            likedMemes.add(memeIndex);
            button.classList.add('liked');
            button.textContent = '❤️';
            likedCount++;
            
            // Load new personalized recommendations after liking
            console.log('Liked a meme! Loading fresh recommendations...');
            setTimeout(() => loadMoreMemes(), 500);
        }
        
        document.getElementById('likedCount').textContent = likedCount;
        console.log('Like toggled successfully. Total likes:', likedCount);
        
    } catch (error) {
        console.error('Error sending feedback:', error);
        alert('Failed to like meme. Check console for details.');
    }
}

async function getRecommendations() {
    console.log('Manual refresh - clearing queue and getting fresh recommendations');
    memeQueue = [];
    await loadMoreMemes();
}

function showEmpty() {
    const container = document.getElementById('feedContainer');
    container.innerHTML = `
        <div class="empty-state">
            <h2>🎉 All done!</h2>
            <p>No more memes to show right now.</p>
            <p>Like more memes to get better recommendations!</p>
        </div>
    `;
}

function showError(message) {
    const container = document.getElementById('feedContainer');
    document.getElementById('loading').style.display = 'none';
    container.innerHTML = `
        <div class="empty-state">
            <h2>😕 Oops!</h2>
            <p>${message}</p>
        </div>
    `;
}

// Keyboard shortcuts
document.addEventListener('keydown', (e) => {
    if (e.key === 'r' || e.key === 'R') {
        console.log('R key pressed - refreshing');
        getRecommendations();
    }
});

console.log('Script loaded, initializing...');
init();