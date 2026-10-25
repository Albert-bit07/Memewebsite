"""Exercise independent sessions and persistence against the real native server."""
import concurrent.futures
import http.cookiejar
import json
import shutil
import urllib.parse
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:18080'
DATA = Path(__file__).resolve().parents[1] / 'backend/data'
CATALOG = json.loads((DATA / 'catalog.json').read_text())
IDS = [item['id'] for item in CATALOG['items']]
def image_path(item_id):
    return '/image?' + urllib.parse.urlencode({'id': item_id})
class Client:
    def __init__(self):
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))
    def request(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(BASE + path, data=data, headers={'Content-Type':'application/json'})
        with self.opener.open(req, timeout=5) as response:
            return json.load(response)

def stop(process):
    process.terminate()
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill(); process.wait()

def start(binary, database, manifest=DATA / 'catalog.json'):
    process = subprocess.Popen([binary, str(manifest), str(database)], cwd=Path(__file__).resolve().parents[1] / 'backend', stdout=subprocess.DEVNULL)
    for _ in range(100):
        if process.poll() is not None: raise RuntimeError('Server exited before readiness')
        try:
            with urllib.request.urlopen(BASE + image_path(IDS[0]), timeout=1) as response:
                assert response.status == 200 and response.read()
            return process
        except urllib.error.URLError: time.sleep(.1)
    stop(process)
    raise RuntimeError('Server did not become ready')

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as temporary:
    database = Path(temporary) / 'users.sqlite3'
    process = start(binary, database)
    try:
        alice, bob = Client(), Client()
        try:
            alice.request('/recommend'); raise AssertionError('Unauthenticated request accepted')
        except urllib.error.HTTPError as error: assert error.code == 401
        for client in [alice, bob]: client.request('/session', {})
        status = alice.request('/status')
        assert status['catalog_version'] == 'legacy-20261020'
        assert status['embeddings_loaded'] == status['paths_loaded'] > 20
        assert alice.request('/feedback', {'id':IDS[0],'action':'like'})['liked_count'] == 1
        assert alice.request('/feedback', {'id':IDS[0],'action':'like'})['liked_count'] == 1
        assert bob.request('/status')['liked_count'] == 0
        assert bob.request('/feedback', {'id':IDS[1],'action':'like'})['liked_count'] == 1
        assert alice.request('/status')['liked_indices'] == [0]
        assert bob.request('/status')['liked_indices'] == [1]
        assert alice.request('/feedback', {'id':IDS[2],'action':'seen'})['liked_count'] == 1
        a = alice.request('/recommend')['recommendations']
        b = bob.request('/recommend')['recommendations']
        assert len(a) == len(b) == 20
        assert not {0,2}.intersection(row['index'] for row in a)
        assert 1 not in [row['index'] for row in b]
        assert a != b
        for body in [{'id':'unknown','action':'like'}, {'id':1,'action':'like'}, {'index':0,'action':'like'}, {'index':-1,'action':'like'}, {'id':IDS[0],'action':'bad'}, {'index':1.5,'action':'like'}, {'index':2**40,'action':'like'}]:
            try:
                alice.request('/feedback', body); raise AssertionError('Invalid feedback accepted')
            except urllib.error.HTTPError as error: assert error.code == 400
        for path in [image_path('unknown'), '/image/0', '/image']:
            try:
                urllib.request.urlopen(BASE + path, timeout=5)
                raise AssertionError('Invalid image request accepted')
            except urllib.error.HTTPError as error: assert error.code == 404
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: alice.request('/recommend'), range(12)))
        assert all(result['recommendations'] == a for result in results)
        with sqlite3.connect(database) as connection:
            connection.execute("CREATE TRIGGER fail_event BEFORE INSERT ON feedback_events BEGIN SELECT RAISE(ABORT,'injected failure'); END")
        try:
            alice.request('/feedback', {'id':IDS[3],'action':'like'}); raise AssertionError('Failed transaction accepted')
        except urllib.error.HTTPError as error: assert error.code == 500
        assert alice.request('/status')['liked_indices'] == [0]
        with sqlite3.connect(database) as connection:
            assert connection.execute("SELECT COUNT(*) FROM preferences").fetchone()[0] == 3
            connection.execute('DROP TRIGGER fail_event')
        # Separate process, same database and browser cookies: reconstruction must match.
        stop(process); process = start(binary, database)
        assert alice.request('/status')['liked_indices'] == [0]
        assert bob.request('/status')['liked_indices'] == [1]
        assert alice.request('/recommend')['recommendations'] == a
        assert alice.request('/feedback', {'id':IDS[0],'action':'unlike'})['liked_count'] == 0
        assert alice.request('/feedback', {'id':IDS[0],'action':'unlike'})['liked_count'] == 0
        assert bob.request('/status')['liked_count'] == 1
        stop(process); process = start(binary, database)
        assert alice.request('/status')['liked_count'] == 0
        assert bob.request('/status')['liked_count'] == 1
        with sqlite3.connect(database) as connection:
            assert connection.execute('SELECT COUNT(*) FROM feedback_events').fetchone()[0] == 4
        # An already-open client must still address the same item after rows move.
        with urllib.request.urlopen(BASE + image_path(IDS[1])) as response:
            before = response.read()
        replacement = Path(temporary) / 'reordered'
        shutil.copytree(DATA, replacement)
        manifest = json.loads((replacement / 'catalog.json').read_text())
        manifest['items'].reverse()
        manifest['catalog_version'] = 'reordered-test'
        (replacement / 'catalog.json').write_text(json.dumps(manifest))
        vectors = replacement / manifest['vectors']
        vectors.write_text('\n'.join(reversed(vectors.read_text().splitlines())) + '\n')
        stop(process); process = start(binary, database, replacement / 'catalog.json')
        assert bob.request('/status')['liked_ids'] == [IDS[1]]
        assert bob.request('/status')['liked_indices'] == [len(IDS)-2]
        assert bob.request('/feedback', {'id':IDS[1], 'action':'unlike'})['liked_count'] == 0
        with urllib.request.urlopen(BASE + image_path(IDS[1])) as response:
            assert response.read() == before
        print('Independent sessions, duplicate feedback, seen filtering, concurrent ranking and restart recovery and reordered-catalog identity passed')
    finally:
        stop(process)
