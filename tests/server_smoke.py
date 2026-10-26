"""Exercise independent sessions and persistence against the real native server."""
import concurrent.futures
import http.cookiejar
import json
import os
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
    def request(self, path, body=None, method=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(BASE + path, data=data, headers={'Content-Type':'application/json'}, method=method)
        with self.opener.open(req, timeout=5) as response:
            return json.load(response)

def stop(process):
    process.terminate()
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill(); process.wait()

def start(binary, database, manifest=DATA / 'catalog.json', max_sessions=None):
    environment = os.environ.copy()
    if max_sessions is not None: environment['MEME_MAX_SESSIONS'] = str(max_sessions)
    process = subprocess.Popen([binary, str(manifest), str(database)], cwd=Path(__file__).resolve().parents[1] / 'backend', stdout=subprocess.DEVNULL, env=environment)
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
        # Revocation removes only the caller's data and survives restart.
        assert bob.request('/feedback', {'id':IDS[1], 'action':'like'})['liked_count'] == 1
        carol = Client()
        carol.request('/session', {})
        carol.request('/feedback', {'id':IDS[0], 'action':'like'})
        token = next(iter(carol.cookies)).value
        with sqlite3.connect(database) as connection:
            connection.execute("CREATE TRIGGER fail_delete BEFORE DELETE ON sessions BEGIN SELECT RAISE(ABORT,'injected failure'); END")
        try:
            carol.request('/session', method='DELETE')
            raise AssertionError('Failed deletion accepted')
        except urllib.error.HTTPError as error: assert error.code == 500
        assert next(iter(carol.cookies)).value == token
        assert carol.request('/status')['liked_count'] == 1
        with sqlite3.connect(database) as connection:
            for table, column in [('sessions','token'),('preferences','session'),('feedback_events','session')]:
                assert connection.execute(f'SELECT COUNT(*) FROM {table} WHERE {column}=?',(token,)).fetchone()[0] == 1
            connection.execute('DROP TRIGGER fail_delete')
        assert carol.request('/session', method='DELETE')['status'] == 'ok'
        assert len(carol.cookies) == 0
        with sqlite3.connect(database) as connection:
            for table, column in [('sessions','token'),('preferences','session'),('feedback_events','session')]:
                assert connection.execute(f'SELECT COUNT(*) FROM {table} WHERE {column}=?',(token,)).fetchone()[0] == 0
        stop(process); process = start(binary, database, replacement / 'catalog.json')
        revoked = urllib.request.Request(BASE+'/status',headers={'Cookie':'meme_session='+token})
        try:
            urllib.request.urlopen(revoked,timeout=5)
            raise AssertionError('Revoked cookie accepted after restart')
        except urllib.error.HTTPError as error: assert error.code == 401
        assert alice.request('/status')['liked_count'] == 0
        assert bob.request('/status')['liked_count'] == 1
        assert carol.request('/session', method='DELETE')['status'] == 'ok'
        carol.request('/session', {})
        assert next(iter(carol.cookies)).value != token
        assert carol.request('/status')['liked_count'] == 0
        cross_site = urllib.request.Request(BASE+'/session',method='DELETE',headers={'Cookie':'meme_session='+next(iter(carol.cookies)).value,'Origin':'https://hostile.test'})
        try:
            urllib.request.urlopen(cross_site,timeout=5)
            raise AssertionError('Cross-site revocation accepted')
        except urllib.error.HTTPError as error: assert error.code == 403
        assert carol.request('/status')['liked_count'] == 0
        print('Independent sessions, duplicate feedback, seen filtering, concurrent ranking and restart recovery, reordered-catalog identity and transactional session revocation passed')
    finally:
        stop(process)

with tempfile.TemporaryDirectory() as temporary:
    database = Path(temporary) / 'capacity.sqlite3'
    process = start(binary, database, max_sessions=2)
    try:
        clients = [Client() for _ in range(12)]
        def register(client):
            try:
                client.request('/session', {})
                return client
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert error.headers.get('Cache-Control') == 'no-store'
                assert 'capacity' in json.load(error)['error'].lower()
                assert len(client.cookies) == 0
                return None
        with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
            accepted = [client for client in pool.map(register, clients) if client is not None]
        assert len(accepted) == 2, 'Concurrent registration exceeded session capacity'
        alice, bob = accepted
        alice.request('/feedback', {'id':IDS[0], 'action':'like'})
        old_cookie = next(iter(alice.cookies)).value
        alice.request('/session', {})
        assert next(iter(alice.cookies)).value == old_cookie
        assert alice.request('/status')['liked_count'] == 1
        with sqlite3.connect(database) as connection:
            assert connection.execute('SELECT COUNT(*) FROM sessions').fetchone()[0] == 2
        # Lowering the limit must preserve existing users, even above the new cap.
        stop(process); process = start(binary, database, max_sessions=1)
        assert alice.request('/status')['liked_count'] == 1
        assert register(Client()) is None
        bob.request('/session', method='DELETE')
        assert register(Client()) is None
        alice.request('/session', method='DELETE')
        assert register(Client()) is not None
    finally:
        stop(process)
    for invalid in ['', '0', '-1', '2abc', '1000001', '999999999999999999999', ' 2', '+2']:
        unused = Path(temporary) / 'invalid.sqlite3'
        result = subprocess.run([binary, str(DATA / 'catalog.json'), str(unused)],
            env={**os.environ, 'MEME_MAX_SESSIONS':invalid}, capture_output=True, timeout=5)
        assert result.returncode != 0
        assert b'MEME_MAX_SESSIONS' in result.stderr
        assert not unused.exists(), 'Invalid config opened a database'
    print('Concurrent session cap, reuse, restart, reduced limit, freed capacity and invalid configuration passed')
