#!/usr/bin/env python3
"""Real existing-engine + private IPC checks, ONLY disposable build/qt-service profiles.
No production endpoints, profiles, registrations or desktop input. Evidence stays local.
"""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'desktop/service/build/install/service/bin/service'


def frame(sock, value):
    payload = json.dumps(value).encode()
    sock.sendall(struct.pack('>I', len(payload)) + payload)
    def read(n):
        data = b''
        while len(data) < n:
            part = sock.recv(n - len(data))
            if not part:
                raise EOFError('Service closed connection')
            data += part
        return data
    size, = struct.unpack('>I', read(4))
    assert 0 < size <= 2 * 1024 * 1024
    return json.loads(read(size))


class Client:
    def __init__(self, profile, client_id='smoke', role=None):
        self.descriptor = json.loads((profile / 'qt-service.json').read_text())
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(8)
        self.sock.connect(self.descriptor['socket'])
        hello = dict(type='hello', version=1, token=self.descriptor['token'], clientId=client_id)
        if role:
            hello['role'] = role
        reply = frame(self.sock, hello)
        assert reply['epoch'] == self.descriptor['epoch'] and reply['version'] == 1
        assert {'snapshot', 'add', 'pause', 'resume', 'settings.update', 'queue.create', 'queue.start', 'category.create', 'category.assign', 'monitor.stats', 'settings.speed', 'download.details', 'download.forget', 'download.parts'} <= set(reply['capabilities'])
        self.sequence = 0

    def snapshot(self):
        return frame(self.sock, dict(type='snapshot'))['snapshot']

    def command(self, op, command_id=None, **kwargs):
        self.sequence += 1
        return frame(self.sock, dict(type='command', epoch=self.descriptor['epoch'],
                                    commandId=command_id or f'command-{self.sequence}', op=op, **kwargs))['result']

    def close(self):
        self.sock.close()


def wait_for(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise TimeoutError('Fixture did not reach expected state')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-qml', action='store_true', help='Run the QML workflow test on native Wayland with private HOME/XDG state; not manual input acceptance')
    args = parser.parse_args()
    os.umask(0o077)
    base = ROOT / 'build/qt-service'
    base.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix='smoke-', dir=base))
    payload = hashlib.sha256(b'ABDM isolated real-engine fixture').digest() * (256 * 1024)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            # Recovery is controlled only by owned fixture markers, not a service backdoor.
            recovery = self.path.partition('?recovery=')[2]
            if recovery in ('python', 'qml', 'window') and not (profile / f'recover-{recovery}').exists():
                self.send_error(404, 'Fixture unavailable until explicit retry')
                return
            start, end = 0, len(payload) - 1
            requested = self.headers.get('Range')
            if requested:
                bounds = requested.removeprefix('bytes=').split('-')
                start = int(bounds[0]); end = int(bounds[1]) if bounds[1] else end
                end = min(end, len(payload) - 1)
                self.send_response(206)
                self.send_header('Content-Range', f'bytes {start}-{end}/{len(payload)}')
            else:
                self.send_response(200)
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('ETag', '"isolated-v1"')
            self.end_headers()
            try:
                for offset in range(start, end + 1, 8192):
                    self.wfile.write(payload[offset:min(offset + 8192, end + 1)])
                    self.wfile.flush(); time.sleep(.006)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    assert server.server_port != 15151
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, JAVA_HOME='/usr/lib/jvm/java-25-openjdk')
    process = None
    log = (profile / 'service.log').open('wb')
    checks = []

    def start():
        nonlocal process
        process = subprocess.Popen([str(BINARY), '--isolated-profile', str(profile)], cwd=ROOT, env=env,
                                   stdout=log, stderr=log)
        wait_for(lambda: (profile / 'qt-service.json').exists() or process.poll() is not None)
        assert process.poll() is None, 'Service startup failed; inspect private service.log'

    def stop():
        nonlocal process
        if process and process.poll() is None:
            process.terminate(); process.wait(timeout=30)
        assert not (profile / 'qt-service.json').exists(), 'Descriptor was not cleaned'

    try:
        start()
        descriptor = json.loads((profile / 'qt-service.json').read_text())
        assert (profile.stat().st_mode & 0o077) == 0
        assert ((profile / 'qt-service.json').stat().st_mode & 0o077) == 0
        assert (Path(descriptor['socket']).parent.stat().st_mode & 0o077) == 0
        checks.append('private descriptor/socket permissions')
        for hello in [dict(type='hello', version=1, token='wrong', clientId='bad'),
                      dict(type='hello', version=99, token=descriptor['token'], clientId='bad')]:
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(3); sock.connect(descriptor['socket'])
                try:
                    frame(sock, hello)
                    raise AssertionError('Invalid authentication/version accepted')
                except (EOFError, ConnectionResetError):
                    pass
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(3); sock.connect(descriptor['socket']); sock.sendall(struct.pack('>I', 2**31))
            assert sock.recv(1) == b''
        checks.append('wrong credential/version and oversized frame rejected')
        client = Client(profile)
        assert client.snapshot()['rows'] == []
        url = f'http://127.0.0.1:{server.server_port}/fixture'
        add = client.command('add', command_id='deduplicated-add', url=url, name='fixture.bin')
        assert add['ok'], add
        download_id = add['id']
        assert client.command('add', command_id='deduplicated-add', url=url, name='fixture.bin') == add
        assert len(client.snapshot()['rows']) == 1
        client.close(); client = Client(profile)  # Same stable client identity, new connection.
        assert client.command('add', command_id='deduplicated-add', url=url, name='fixture.bin') == add
        assert len(client.snapshot()['rows']) == 1
        checks.append('stable IDs and command deduplication across reconnect')
        history_reply = frame(client.sock, dict(type='history.page', limit=1))
        assert history_reply['type'] == 'history.page'
        history = history_reply['history']
        assert history['total'] == '1' and history['rows'][0]['id'] == download_id
        assert not history['detailsIncluded'] and history['nextCursor'] is None
        delta = frame(client.sock, dict(type='history.delta', limit=1, cursor=history['cursor'], known=history['hashes']))
        assert delta['history']['rows'] == []
        refused = frame(client.sock, dict(type='history.page', limit=201))
        assert refused['type'] == 'historyError'
        checks.append('separate bounded history summary/delta IPC and limit rejection; full Qt paging not claimed')
        assert not client.command('add', url='http://127.0.0.1:15151/forbidden', name='bad')['ok']
        assert not client.command('add', url='https://example.invalid/file', name='bad')['ok']
        assert not client.command('add', url=url, name='../escape')['ok']
        assert not client.command('remove', id=download_id)['ok']
        checks.append('fixture destination/URL policy and unsupported commands enforced')
        second = subprocess.run([str(BINARY), '--isolated-profile', str(profile)], cwd=ROOT, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        assert second.returncode != 0 and b'Profile already has a persistence owner' in second.stderr
        assert json.loads((profile / 'qt-service.json').read_text()) == descriptor
        checks.append('second writer rejected without replacing descriptor')
        assert not list((ROOT / 'desktop/service/build/install/service/lib').glob('*compose*'))
        assert client.command('settings.update', settings={'useSpeedLimit': 'Enabled', 'speedLimit': '524288', 'useAverageSpeed': False})['ok']
        assert client.command('resume', id=download_id)['ok']
        wait_for(lambda: int(client.snapshot()['rows'][0]['downloaded']) > 65536)
        client.close(); client = Client(profile, 'frontend-restarted')
        wait_for(lambda: int(client.snapshot()['rows'][0]['downloaded']) > 131072)
        checks.append('transfer continues while frontend disconnects/reconnects')
        measurement = wait_for(lambda: next((r for r in client.snapshot()['rows'] if int(r['speed']) > 0 and int(r['remainingTime']) >= 0), None))
        assert measurement['state'] == 'downloading'
        projection = frame(client.sock, dict(type='snapshot', partsFor=[download_id]))['snapshot']['downloadParts']
        parts = projection[download_id]
        assert parts['available'] and 0 < len(parts['items']) <= 256 and parts['totalCount'] >= len(parts['items'])
        assert all(set(p) == {'id', 'offset', 'length', 'downloaded', 'state'} for p in parts['items'])
        assert sum(int(p['downloaded']) for p in parts['items']) > 0
        # Malformed read-only selection closes only that authenticated connection.
        invalid_parts = Client(profile, 'invalid-parts-query')
        try:
            frame(invalid_parts.sock, dict(type='snapshot', partsFor=[download_id, download_id]))
            raise AssertionError('Duplicate part selection accepted')
        except (EOFError, ConnectionResetError):
            pass
        finally:
            invalid_parts.close()
        checks.append('download.parts bounded stable-ID projection carries only engine range counters/states; malformed selection rejected without mutation')
        before_bytes = int(measurement['downloaded']); time.sleep(1.2)
        progress = int(client.snapshot()['rows'][0]['downloaded']) - before_bytes
        assert 0 < progress <= 3 * 524288, 'Fixed core speed limiter failed generous burst allowance'
        checks.append('existing DownloadMonitor supplies measured speed/ETA; persisted fixed core speed limit constrains real transfer (not a benchmark)')
        assert client.command('pause', id=download_id)['ok']
        row = wait_for(lambda: next((r for r in client.snapshot()['rows'] if r['state'] == 'paused'), None))
        received = int(row['downloaded']); assert 0 < received < len(payload)
        old_epoch = client.descriptor['epoch']; client.close(); stop(); start()
        client = Client(profile, 'after-service-restart')
        assert client.descriptor['epoch'] != old_epoch
        row = client.snapshot()['rows'][0]
        assert row['id'] == download_id and int(row['downloaded']) == received
        assert row['speed'] == '0' and row['remainingTime'] == '-1'
        assert client.snapshot()['settings']['useSpeedLimit'] == 'Enabled'
        assert client.snapshot()['settings']['speedLimit'] == '524288'
        assert client.command('resume', id=download_id)['ok']
        wait_for(lambda: client.snapshot()['rows'][0]['state'] == 'completed', timeout=30)
        client.close()
        output = profile / 'Downloads/fixture.bin'
        assert hashlib.sha256(output.read_bytes()).digest() == hashlib.sha256(payload).digest()
        checks.append('pause, service restart, persisted byte count, resume and exact SHA-256')
        stop(); start(); client = Client(profile, 'verify-completed')
        assert client.snapshot()['rows'][0]['state'] == 'completed'
        assert client.snapshot()['rows'][0]['id'] == download_id
        settings_file = profile / 'app-data/config/appSettings.json'
        assert client.command('settings.update', settings={'threadCount': 2, 'maxConcurrentDownloads': 1, 'useSpeedLimit': 'Disabled', 'useAverageSpeed': True})['ok']
        saved = settings_file.read_bytes()
        rejected = client.command('settings.update', settings={'threadCount': 4, 'maxConcurrentDownloads': -1})
        assert not rejected['ok'] and rejected['code'] == 'command-rejected'
        assert settings_file.read_bytes() == saved
        assert not client.command('settings.update', settings={'apiEnabled': True})['ok']
        one = client.command('add', url=url, name='queue-one.bin')['id']
        two = client.command('add', url=url, name='queue-two.bin')['id']
        def row_by_id(item_id):
            return next(r for r in client.snapshot()['rows'] if r['id'] == item_id)
        assert client.command('resume', id=one)['ok']
        wait_for(lambda: int(row_by_id(one)['downloaded']) > 32768)
        assert client.command('resume', id=two)['ok']
        wait_for(lambda: two in client.snapshot()['manualPending'])
        assert row_by_id(two)['downloaded'] == '0'
        assert client.command('pause', id=two)['ok']
        assert client.command('pause', id=one)['ok']
        time.sleep(.8)  # Past the core's debounced refill: paused pending work must not restart.
        assert two not in client.snapshot()['manualPending'] and row_by_id(two)['downloaded'] == '0'
        checks.append('existing manual queue enforces manual-request concurrency and pause removes pending work')
        queue_id = client.command('queue.create', name='Nightly fixtures')['queueId']
        assert client.command('queue.update', queueId=queue_id, name='Nightly fixtures', maxConcurrent=1, stopOnEmpty=True)['ok']
        for item_id in [one, two]:
            assert client.command('queue.add', queueId=queue_id, id=item_id)['ok']
        assert client.command('queue.move', queueId=queue_id, id=two, direction=-1)['ok']
        queue = next(q for q in client.snapshot()['queues'] if q['id'] == queue_id)
        assert queue['items'] == [two, one]
        assert client.command('queue.start', queueId=queue_id)['ok']
        wait_for(lambda: int(row_by_id(two)['downloaded']) > 32768)
        assert row_by_id(one)['state'] == 'paused'
        assert client.command('queue.stop', queueId=queue_id)['ok']
        client.close(); stop()
        queue_extra = profile / f'app-data/config/download_db/extra_queue_settings/{queue_id}.json'
        queue_extra_document = dict(id=int(queue_id), powerActionTypeOnFinish='Shutdown', powerActionUseForceOnFinish=True,
                                    future={'preserve': ['original', 42]})
        queue_extra.write_text(json.dumps(queue_extra_document))  # Stopped disposable profile only.
        start(); client = Client(profile, 'queue-restart')
        assert json.loads(queue_extra.read_text()) == queue_extra_document
        assert 'powerActionTypeOnFinish' not in json.dumps(client.snapshot())
        snapshot = client.snapshot()
        assert snapshot['settings']['maxConcurrentDownloads'] == 1 and snapshot['settings']['threadCount'] == 2
        queue = next(q for q in snapshot['queues'] if q['id'] == queue_id)
        assert queue['items'] == [two, one] and not queue['active'] and queue['maxConcurrent'] == 1
        assert not client.command('queue.delete', queueId='0')['ok']
        assert client.command('queue.start', queueId=queue_id)['ok']
        def queue_finished():
            snapshot = client.snapshot()
            rows = [r for r in snapshot['rows'] if r['id'] in [one, two]]
            assert sum(r['state'] == 'downloading' for r in rows) <= 1
            return all(r['state'] == 'completed' for r in rows)
        wait_for(queue_finished, timeout=35)
        wait_for(lambda: not next(q for q in client.snapshot()['queues'] if q['id'] == queue_id)['active'])
        for name in ['queue-one.bin', 'queue-two.bin']:
            assert hashlib.sha256((profile / 'Downloads' / name).read_bytes()).digest() == hashlib.sha256(payload).digest()
        assert client.command('queue.delete', queueId=queue_id)['ok']
        assert not (profile / f'app-data/config/download_db/queues/{queue_id}.json').exists()
        assert not queue_extra.exists()
        checks.append('named core queue reorder/concurrency/stop/restart/persisted settings, completion and exact SHA-256')
        partial_queue = client.command('queue.create', name='Extra cleanup failure')['queueId']
        extra_obstruction = profile / f'app-data/config/download_db/extra_queue_settings/{partial_queue}.json'
        extra_obstruction.mkdir()
        marker = extra_obstruction / 'evidence'; marker.write_text('preserve injected evidence')
        try:
            partial_delete = client.command('queue.delete', command_id='partial-extra-queue-delete', queueId=partial_queue)
            assert not partial_delete['ok'] and partial_delete['code'] == 'queue-delete-partially-applied'
            assert partial_delete['queueId'] == partial_queue
            assert client.command('queue.delete', command_id='partial-extra-queue-delete', queueId=partial_queue) == partial_delete
            assert all(q['id'] != partial_queue for q in client.snapshot()['queues'])
            assert marker.read_text() == 'preserve injected evidence'
        finally:
            marker.unlink(); extra_obstruction.rmdir()  # Only this test's injected obstruction.
        checks.append('facade queue extra-settings round-trip/cleanup; IPC partial cleanup is correlated, deduplicated and never replayed')
        category_file = profile / 'app-data/config/download_db/categories/categories.json'
        blocked_temp = category_file.with_suffix('.json.tmp')
        blocked_temp.mkdir()
        try:
            partial = client.command('add', command_id='partial-category-add', url=url, name='committed.zip')
            assert not partial['ok'] and partial['code'] == 'add-partially-applied' and partial['id']
            assert client.command('add', command_id='partial-category-add', url=url, name='committed.zip') == partial
            assert row_by_id(partial['id'])['name'] == 'committed.zip'
        finally:
            blocked_temp.rmdir()
        assert all(partial['id'] not in c['items'] for c in client.snapshot()['categories'])
        checks.append('category disk-write failure reports the committed Add ID and explicit partial outcome; retry dedup does not create another record')
        category_id = client.command('category.create', command_id='create-category', name='Fixture source',
                                     acceptedFileTypes=['bin'], acceptedUrlPatterns=[f'http://127.0.0.1:{server.server_port}/*'])['categoryId']
        assert client.command('category.create', command_id='create-category', name='Fixture source',
                              acceptedFileTypes=['bin'], acceptedUrlPatterns=[f'http://127.0.0.1:{server.server_port}/*'])['categoryId'] == category_id
        saved = category_file.read_bytes()
        assert not client.command('category.update', categoryId=category_id, name='Invalid', acceptedFileTypes=['bad/type'], acceptedUrlPatterns=[])['ok']
        assert not client.command('category.update', categoryId=category_id, name='Escape', acceptedFileTypes=['bin'], acceptedUrlPatterns=[], path=str(base))['ok']
        assert category_file.read_bytes() == saved
        assert client.command('category.assign', categoryId=category_id, id=download_id)['ok']
        assert client.command('category.move', categoryId=category_id, direction=-1)['ok']
        client.close(); stop(); start(); client = Client(profile, 'category-restart')
        category = next(c for c in client.snapshot()['categories'] if c['id'] == category_id)
        assert category['items'] == [download_id] and category['name'] == 'Fixture source'
        assert client.command('category.assign', categoryId='none', id=download_id)['ok']
        assert client.command('category.delete', categoryId=category_id)['ok']
        assert row_by_id(download_id)['state'] == 'completed' and output.exists()
        checks.append('existing category manager/storage CRUD, deduplication, invalid-patch atomicity, membership/reorder/restart and nondestructive delete')
        failed = client.command('add', url=url + '?recovery=python', name='recovery.dat', start=True)
        assert failed['ok']
        failed_id = failed['id']
        def failed_row():
            return next(r for r in client.snapshot()['rows'] if r['id'] == failed_id)
        reason = wait_for(lambda: failed_row()['details']['error'])
        assert '404' in reason['description'] and reason['title'] == 'Something Went Wrong'
        assert failed_row()['state'] == 'failed'
        client.close(); stop(); start(); client = Client(profile, 'failure-restart')
        assert failed_row()['details']['error'] is None  # Original errors are memory-only.
        (profile / 'recover-python').touch()
        assert client.command('resume', id=failed_id)['ok']
        wait_for(lambda: failed_row()['state'] == 'completed')
        assert failed_row()['details']['error'] is None
        assert hashlib.sha256((profile / 'Downloads/recovery.dat').read_bytes()).digest() == hashlib.sha256(payload).digest()
        checks.append('real HTTP failure details, memory-only reason across restart, explicit retry/completion and exact checksum')
        # New metadata-only core path: both real incomplete-file layouts survive byte
        # for byte, including mtime; no destination cleanup/rename is allowed.
        forgotten_ids = []
        def payload_manifest():
            return {str(p.relative_to(profile / 'Downloads')): (p.stat().st_size, p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest())
                    for p in (profile / 'Downloads').rglob('*') if p.is_file()}
        for extension in (False, True):
            client.close(); stop()
            settings_document = json.loads(settings_file.read_text())
            settings_document['appendExtensionToIncompleteDownloads'] = extension
            settings_file.write_text(json.dumps(settings_document))  # Only stopped disposable profile.
            start(); client = Client(profile, f'forget-incomplete-{extension}')
            victim = client.command('add', url=url, name=f'keep-incomplete-{extension}.bin', categoryId='0', start=True)['id']
            wait_for(lambda: int(row_by_id(victim)['downloaded']) > 32768)
            assert not client.command('download.forget', id=victim, keepFiles=True)['ok']
            assert client.command('pause', id=victim)['ok']
            assert client.command('queue.add', queueId='0', id=victim)['ok']
            assert client.command('queue.start', queueId='0')['ok']
            wait_for(lambda: row_by_id(victim)['state'] == 'downloading')
            assert client.command('pause', id=victim)['ok']
            wait_for(lambda: row_by_id(victim)['state'] == 'paused')
            assert next(q for q in client.snapshot()['queues'] if q['id'] == '0')['active']
            membership_files = [category_file, profile / 'app-data/config/download_db/queues/0.json']
            membership_before = [p.read_bytes() for p in membership_files]
            rejected = client.command('download.forget', id=victim, keepFiles=True)
            assert not rejected['ok'] and rejected['code'] == 'command-rejected' and 'Stop containing queues' in rejected['error']
            assert [p.read_bytes() for p in membership_files] == membership_before
            assert victim in next(q for q in client.snapshot()['queues'] if q['id'] == '0')['items']
            assert client.command('queue.stop', queueId='0')['ok']
            before = payload_manifest()
            kept_name = f'keep-incomplete-{extension}.bin' + (f'.dl-{victim}.abdm.part' if extension else '')
            assert kept_name in before
            result = client.command('download.forget', command_id='forget-incomplete', id=victim, keepFiles=True)
            assert result['ok'], result
            assert client.command('download.forget', command_id='forget-incomplete', id=victim, keepFiles=True) == result
            assert payload_manifest() == before
            snapshot = client.snapshot()
            assert all(r['id'] != victim for r in snapshot['rows'])
            assert all(victim not in q['items'] for q in snapshot['queues'])
            assert all(victim not in c['items'] for c in snapshot['categories'])
            assert not (profile / f'app-data/config/download_db/parts/{victim}.json').exists()
            forgotten_ids.append(victim)
        blocker = client.command('add', url=url, name='forget-blocker.bin', start=True)['id']
        wait_for(lambda: int(row_by_id(blocker)['downloaded']) > 32768)
        pending = client.command('add', url=url, name='forget-pending.zip', start=True)['id']
        wait_for(lambda: pending in client.snapshot()['manualPending'])
        before = payload_manifest()
        assert client.command('download.forget', id=pending, keepFiles=True)['ok']
        assert client.command('pause', id=blocker)['ok']
        time.sleep(1.1)
        assert all(r['id'] != pending for r in client.snapshot()['rows'])
        assert pending not in client.snapshot()['manualPending']
        assert not (profile / 'Downloads/forget-pending.zip').exists()
        forgotten_ids.append(pending)
        complete = client.command('add', url=url, name='keep-complete.bin', start=True)['id']
        wait_for(lambda: row_by_id(complete)['state'] == 'completed')
        assert hashlib.sha256((profile / 'Downloads/keep-complete.bin').read_bytes()).digest() == hashlib.sha256(payload).digest()
        client.close(); stop()
        download_extra = profile / f'app-data/config/download_db/extra_download_settings/{complete}.json'
        download_extra_document = dict(id=int(complete), powerActionTypeOnFinish='Shutdown', powerActionUseForceOnFinish=True,
                                       future={'preserve': ['original', 42]})
        download_extra.write_text(json.dumps(download_extra_document))
        start(); client = Client(profile, 'download-extra-restart')
        assert json.loads(download_extra.read_text()) == download_extra_document
        before = payload_manifest()
        assert client.command('download.forget', id=complete, keepFiles=True)['ok']
        assert not download_extra.exists()
        assert payload_manifest() == before
        forgotten_ids.append(complete)
        # Inject the final internal-store failure after primary deletion. It must be
        # an explicit deduplicated partial result, not success or an automatic retry.
        broken = client.command('add', url=url, name='forget-partial.zip')['id']
        part_block = profile / f'app-data/config/download_db/parts/{broken}.json'
        part_block.mkdir(); (part_block / 'block').write_text('prevent metadata unlink')
        before = payload_manifest()
        partial = client.command('download.forget', command_id='forget-partial', id=broken, keepFiles=True)
        assert not partial['ok'] and partial['code'] == 'remove-partially-applied' and partial['id'] == broken
        assert client.command('download.forget', command_id='forget-partial', id=broken, keepFiles=True) == partial
        assert payload_manifest() == before
        (part_block / 'block').unlink(); part_block.rmdir()
        forgotten_ids.append(broken)
        client.close(); stop(); start(); client = Client(profile, 'forget-restart')
        assert all(r['id'] not in forgotten_ids for r in client.snapshot()['rows'])
        assert payload_manifest() == before
        checks.append('metadata-only removal: active/delete-files rejection, both real incomplete layouts unchanged, completed SHA-256 kept, pending producer detached, memberships persisted, no shutdown/restart resurrection, deduplicated partial cleanup outcome')
        client.close()
        qt_test = ROOT / 'build/qt-capability/qt-ui/tst_servicedownloads'
        qt_env = dict(env, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                      QT_SERVICE_TEST_DESCRIPTOR=str(profile / 'qt-service.json'), QT_SERVICE_TEST_URL=url,
                      QT_SERVICE_TEST_RECOVERY_MARKER=str(profile / 'recover-qml'),
                      QT_SERVICE_TEST_CAPTURE_DIR=str(profile))
        qt_command = [str(qt_test), 'realEngineViaQml']
        if args.native_qml:
            qt_env.pop('QT_QUICK_BACKEND', None)
            qt_env['QT_QPA_PLATFORM'] = 'wayland'
            qt_command = ['python3', str(ROOT / 'scripts/qt-capability/run.py'), '--snapshot-desktop-style',
                          '--binary', str(qt_test), '--', 'realEngineViaQml']
        with (profile / 'qt-adapter-qml.log').open('wb') as qt_log:
            qt = subprocess.run(qt_command, cwd=ROOT, env=qt_env,
                                stdout=qt_log, stderr=qt_log, timeout=100)
        assert qt.returncode == 0, 'Qt real-engine adapter/QML test failed; inspect qt-adapter-qml.log'
        # Separate invocation: a fresh frontend baselines this epoch's earlier window hints.
        with (profile / 'qt-download-windows.log').open('wb') as qt_log:
            qt = subprocess.run(qt_command[:-1] + ['realEngineDownloadWindows'], cwd=ROOT, env=qt_env,
                                stdout=qt_log, stderr=qt_log, timeout=150)
        assert qt.returncode == 0, 'Qt real-engine download-window test failed; inspect qt-download-windows.log'
        for name in ['window-a.bin', 'window-b.bin', 'window-failed.dat']:
            assert hashlib.sha256((profile / 'Downloads' / name).read_bytes()).digest() == hashlib.sha256(payload).digest()
        checks.append('Qt native review reuses its surface for the first accepted stable ID, batch obeys the automatic cap; engine parts, window pause/resume, completion, local close/reopen, real 404 and Retry, exact SHA-256')
        assert hashlib.sha256((profile / 'Downloads/qt-client.bin').read_bytes()).digest() == hashlib.sha256(payload).digest()
        assert hashlib.sha256((profile / 'Downloads/qt-direct.bin').read_bytes()).digest() == hashlib.sha256(payload).digest()
        assert hashlib.sha256((profile / 'Downloads/qt-queued.bin').read_bytes()).digest() == hashlib.sha256(payload).digest()
        assert hashlib.sha256((profile / 'Downloads/qt-recovery.dat').read_bytes()).digest() == hashlib.sha256(payload).digest()
        checks.append('Qt/QML Add, Download, persisted engine settings, queue creation/items/start, schedule editing and real error/details/explicit retry with exact SHA-256')
        stop(); start(); client = Client(profile, 'qml-category-restart')
        snapshot = client.snapshot()
        assert snapshot['settings']['useSpeedLimit'] == 'Enabled' and snapshot['settings']['speedLimit'] == '4194304'
        assert snapshot['settings']['useAverageSpeed'] is False
        assert all(r['name'] != 'qt-recovery.dat' for r in snapshot['rows'])
        assert all(r['id'] not in forgotten_ids for r in snapshot['rows'])
        checks.append('real Qt captured-target confirmation removes completed history only, preserving the exact payload checksum after restart')
        qt_category = next(c for c in snapshot['categories'] if c['name'] == 'Qt fixtures')
        members = {r['name'] for r in snapshot['rows'] if r['id'] in qt_category['items']}
        assert {'qt-client.bin', 'qt-direct.bin', 'qt-queued.bin'} <= members
        # Same JSON schema as Compose: numeric IDs/items on disk, strings only on IPC.
        stored = next(c for c in json.loads(category_file.read_text()) if c['name'] == 'Qt fixtures')
        assert isinstance(stored['id'], int) and all(isinstance(i, int) for i in stored['items'])
        assert stored['icon'] == 'icon:videoFile' and not stored['usePath']
        client.close(); stop()
        checks.append('real QML category create/assignment, Add fixed/Auto selection, draft retention, persisted membership after service restart')
        checks.append('completed history survives restart and clean shutdown removes endpoint')
        # Refuse persisted paths outside the fixture, even when JSON was copied into build/.
        tampered = Path(tempfile.mkdtemp(prefix='invalid-stored-path-', dir=base))
        item_file = profile / f'app-data/config/download_db/downloadlist/{download_id}.json'
        item = json.loads(item_file.read_text())
        outside = Path(tempfile.mkdtemp(prefix='outside-sentinel-', dir=base))
        sentinel = outside / 'sentinel'; sentinel.write_text('must remain unchanged')
        item['folder'] = str(outside)
        target = tampered / 'app-data/config/download_db/downloadlist'
        target.mkdir(parents=True)
        (target / f'{download_id}.json').write_text(json.dumps(item))
        (target / 'last_id.txt').write_text(download_id)
        rejected = subprocess.run([str(BINARY), '--isolated-profile', str(tampered)], cwd=ROOT, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        assert rejected.returncode != 0 and b'Stored destination escapes' in rejected.stderr
        assert sentinel.read_text() == 'must remain unchanged'
        assert not (tampered / 'qt-service.json').exists()
        linked = Path(tempfile.mkdtemp(prefix='invalid-symlink-', dir=base))
        (linked / 'app-data').symlink_to(profile / 'app-data', target_is_directory=True)
        rejected = subprocess.run([str(BINARY), '--isolated-profile', str(linked)], cwd=ROOT, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        assert rejected.returncode != 0 and b'Symlinks in fixture profile refused' in rejected.stderr
        assert not (linked / 'qt-service.json').exists()
        checks.append('persisted destination escape and symlinked profile refused before engine boot')
        result = dict(status='passed', checks=checks, qt_backend='native-wayland' if args.native_qml else 'offscreen', payload_bytes=len(payload),
                      sha256=hashlib.sha256(payload).hexdigest(), profile=str(profile))
        (profile / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    finally:
        if process and process.poll() is None:
            process.terminate(); process.wait(timeout=30)
        server.shutdown(); server.server_close(); log.close()


if __name__ == '__main__':
    main()
