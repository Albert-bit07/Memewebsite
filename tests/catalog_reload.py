"""Native HTTP reload regression using synthetic vectors, not relevance evidence."""
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:18080'
TOKEN = 'test-only-reload-token-not-a-secret-12345'

def request(path, body=None, cookie='', token=None):
    headers = {'Content-Type': 'application/json', 'Cookie': cookie}
    if token is not None:
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request(BASE + path, data=None if body is None else json.dumps(body).encode(), headers=headers)
    with urllib.request.urlopen(req, timeout=10) as response:
        return json.load(response), response.headers

def expect_error(code, path, **kwargs):
    try:
        request(path, **kwargs)
        raise AssertionError('Request unexpectedly succeeded')
    except urllib.error.HTTPError as error:
        assert error.code == code, (error.code, code)

def stop(process):
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()

def start(binary, manifest, database, enabled=True):
    environment = os.environ.copy()
    environment.pop('MEME_ADMIN_TOKEN', None)
    if enabled:
        environment['MEME_ADMIN_TOKEN'] = TOKEN
    process = subprocess.Popen([binary, str(manifest), str(database)], env=environment, stdout=subprocess.DEVNULL)
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError('Server exited before readiness')
        try:
            _, headers = request('/session', {})
            return process, headers['Set-Cookie'].split(';')[0]
        except urllib.error.URLError:
            time.sleep(.1)
    stop(process)
    raise RuntimeError('Server did not become ready')

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    manifests = []
    for version, ids, vectors in [('v1', ['a','b','c'], ['1,0','0,1','1,0']),
                                  ('v2', ['c','a','b'], ['0,1,0','1,0,0','1,0,0'])]:
        directory = root / version
        directory.mkdir()
        (directory / 'vectors.csv').write_text('\n'.join(vectors) + '\n')
        for item in ids:
            (directory / (item+'.jpg')).write_bytes((version+item).encode())
        manifests.append({'schema_version':1,'catalog_version':version,'model_version':'synthetic',
            'dimension':len(vectors[0].split(',')), 'vectors':version+'/vectors.csv',
            'items':[{'id':item,'path':version+'/'+item+'.jpg'} for item in ids]})
    manifest = root / 'catalog.json'
    def publish(value):
        staged = root / 'next.json'
        staged.write_text(json.dumps(value))
        staged.replace(manifest)
    publish(manifests[0])
    database = root / 'users.sqlite3'
    process, alice = start(binary, manifest, database)
    try:
        _, headers = request('/session', {})
        bob = headers['Set-Cookie'].split(';')[0]
        request('/feedback', {'id':'a','action':'like'}, alice)
        request('/feedback', {'id':'b','action':'like'}, bob)
        expect_error(401, '/admin/reload', body={})
        expect_error(401, '/admin/reload', body={}, token='wrong-token')
        def check_feed():
            feed, _ = request('/recommend', cookie=alice)
            assert feed['catalog_version'] in ('v1','v2')
            assert [item['id'] for item in feed['recommendations']] == (
                ['c','b'] if feed['catalog_version']=='v1' else ['b','c'])
        check_feed()
        publish(manifests[1])
        result, _ = request('/admin/reload', {}, token=TOKEN)
        assert result['catalog_version'] == 'v2'
        with urllib.request.urlopen(BASE + '/image?id=a', timeout=5) as image:
            assert image.read() == b'v2a'
        check_feed()
        assert request('/status', cookie=alice)[0]['liked_ids'] == ['a']
        assert request('/status', cookie=bob)[0]['liked_ids'] == ['b']
        publish({**manifests[1], 'dimension':99})
        expect_error(422, '/admin/reload', body={}, token=TOKEN)
        check_feed()
        assert request('/status', cookie=alice)[0]['catalog_version'] == 'v2'
        def read_many():
            for _ in range(50):
                check_feed()
        def write_many():
            for _ in range(25):
                request('/feedback', {'id':'a','action':'like'}, bob)
                request('/feedback', {'id':'a','action':'unlike'}, bob)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            readers = [pool.submit(read_many), pool.submit(read_many), pool.submit(write_many)]
            for index in range(10):
                publish(manifests[index % 2])
                request('/admin/reload', {}, token=TOKEN)
            for future in readers:
                future.result()
        assert request('/status', cookie=bob)[0]['liked_ids'] == ['b']
        # Removing a liked item must drop its derived vector, not its history.
        removed = {**manifests[1], 'catalog_version':'removed', 'vectors':'removed.csv',
                   'items':[item for item in manifests[1]['items'] if item['id'] != 'a']}
        (root / 'removed.csv').write_text('0,1,0\n1,0,0\n')
        publish(removed)
        request('/admin/reload', {}, token=TOKEN)
        assert request('/status', cookie=alice)[0]['liked_ids'] == []
        expect_error(404, '/image?id=a')
        expect_error(400, '/feedback', body={'id':'a','action':'like'}, cookie=alice)
        publish(manifests[1])
        request('/admin/reload', {}, token=TOKEN)
        assert request('/status', cookie=alice)[0]['liked_ids'] == ['a']
        check_feed()
        stop(process)
        process, _ = start(binary, manifest, database, enabled=False)
        expect_error(404, '/admin/reload', body={}, token=TOKEN)
        assert request('/status', cookie=alice)[0]['liked_ids'] == ['a']
        assert request('/status', cookie=bob)[0]['liked_ids'] == ['b']
        check_feed()
        print('Reload authorization, dimension/reorder reconstruction, invalid update rollback, concurrent reads/writes and restart passed')
    finally:
        stop(process)
