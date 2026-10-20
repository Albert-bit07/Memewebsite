"""Exercise the real local HTTP server with the committed vector dataset."""
import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.request


def request(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request('http://127.0.0.1:18080' + path, data=data, headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.load(response)


process = subprocess.Popen([str(Path(sys.argv[1]).resolve())], cwd=Path(__file__).resolve().parents[1] / 'backend', stdout=subprocess.DEVNULL)
try:
    for attempt in range(100):
        if process.poll() is not None:
            raise RuntimeError('Server exited before readiness')
        try:
            status = request('/status')
            break
        except urllib.error.URLError:
            time.sleep(0.1)
    else:
        raise RuntimeError('Server did not become ready')
    assert status['embeddings_loaded'] == status['paths_loaded'] > 0
    assert request('/feedback', {'index': 0, 'action': 'like'})['liked_count'] == 1
    assert request('/feedback', {'index': 0, 'action': 'like'})['liked_count'] == 1
    assert request('/feedback', {'index': 0, 'action': 'unlike'})['liked_count'] == 0
    for body in [{'index': -1, 'action': 'like'}, {'index': 0, 'action': 'bad'}]:
        try:
            request('/feedback', body)
            raise AssertionError('Invalid feedback accepted')
        except urllib.error.HTTPError as error:
            assert error.code == 400
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda _: request('/recommend'), range(12)))
    assert all(len(result['recommendations']) == 20 for result in results)
    with urllib.request.urlopen('http://127.0.0.1:18080/image/0', timeout=5) as response:
        assert response.status == 200 and len(response.read()) > 0
    print('HTTP feedback, concurrent Python ranking and image smoke checks passed')
finally:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
