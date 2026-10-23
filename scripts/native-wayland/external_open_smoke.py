#!/usr/bin/env python3
"""Exercise the real file/folder/URL openers in a disposable desktop, never launch real media apps."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import unquote, urlsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUTPUT = ROOT / 'build/native-wayland'


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


launcher = load('external_open_launcher', 'launch.py')
smoke = load('external_open_protection', 'smoke.py')

HARNESS = '''
import java.io.File;
import java.nio.file.*;
import com.abdownloadmanager.desktop.utils.EntrypointInitializer;
import com.abdownloadmanager.desktop.utils.EntryType;
import ir.amirab.util.osfileutil.FileUtils;
import ir.amirab.util.URLOpener;

public class ExternalOpenSmoke {
    public static void main(String[] args) throws Exception {
        EntrypointInitializer.INSTANCE.boot(false, EntryType.CLI);
        String isolated = System.getProperty("abdm.experimental.profileRoot") + "/home";
        if (!isolated.equals(System.getenv("HOME")) || !isolated.equals(System.getProperty("user.home")))
            throw new AssertionError("ABDM's own HOME lost isolation");
        if (!FileUtils.Companion.openFile(new File(args[0]))) throw new AssertionError("file open failed");
        if (!FileUtils.Companion.openFolder(new File(args[1]))) throw new AssertionError("folder open failed");
        URLOpener.INSTANCE.openUrl(args[2]);
        Path record = Path.of(args[3]);
        long deadline = System.nanoTime() + 5_000_000_000L;
        while ((!Files.exists(record) || Files.readAllLines(record).size() < 3) && System.nanoTime() < deadline)
            Thread.sleep(50);
        if (!Files.exists(record) || Files.readAllLines(record).size() != 3) throw new AssertionError("missing external launches");
        if (!isolated.equals(System.getenv("HOME"))) throw new AssertionError("external launch changed parent environment");
    }
}
'''


def main():
    bundle = (OUTPUT / 'latest').resolve()
    launcher.verify(bundle)
    case = Path(tempfile.mkdtemp(prefix='external-open-', dir=OUTPUT))
    home = case / 'home'
    home.mkdir()
    profile = launcher.prepare_profile(home / 'profile')
    data, config = home / 'custom-data', home / 'custom-config'
    system = case / 'system/share'
    for directory in [data / 'applications', config, system / 'applications', home / 'tmp']:
        directory.mkdir(parents=True, exist_ok=True)
    # Ownership-valid integration helpers, never executed by this non-window harness.
    for name in ['ABDownloadManagerNativeWayland', 'ABDownloadManagerNativeWaylandCli', 'ABDownloadManagerNativeWaylandHost']:
        helper = home / '.local/bin' / name
        helper.parent.mkdir(parents=True, exist_ok=True)
        helper.write_text('#!/bin/sh\nexit 99\n')
        helper.chmod(0o755)
    (profile / '.abdm-desktop-integration.properties').write_text(
        f'kind=abdm-native-desktop-integration-v1\nhome={home}\ntestLabel=DISPOSABLE EXTERNAL OPEN TEST\n')
    mime = 'video/mp4'
    scheme = 'abdm-external-open-test'
    desktop_id = 'abdm-external-open-test.desktop'
    record = case / 'opened.jsonl'
    recorder = case / 'record.py'
    recorder.write_text('import json,os,sys\nfrom pathlib import Path\n'
                        'keys=' + repr(list(launcher.DESKTOP_VARIABLES) + [launcher.DESKTOP_CONTEXT]) + '\n'
                        f'with Path({str(record)!r}).open("a") as out:\n'
                        ' out.write(json.dumps({"args":sys.argv[1:],"environment":{k:os.environ.get(k) for k in keys}})+"\\n")\n')
    for apps, which in [(system / 'applications', 'WRONG-SYSTEM-BUILD'), (data / 'applications', 'CUSTOM-USER-BUILD')]:
        (apps / desktop_id).write_text('[Desktop Entry]\nType=Application\nName=ABDM disposable recorder\n'
                                      f'Exec={sys.executable} {recorder} {which} %u\n'
                                      f'MimeType={mime};inode/directory;x-scheme-handler/{scheme};\nTerminal=false\n')
    (config / 'mimeapps.list').write_text('[Default Applications]\n' + ''.join(
        f'{kind}={desktop_id}\n' for kind in [mime, 'inode/directory', 'x-scheme-handler/' + scheme]))
    env = os.environ.copy()
    env.pop(launcher.DESKTOP_CONTEXT, None)
    env.pop('XDG_STATE_HOME', None)
    env.update(HOME=str(home), XDG_DATA_HOME=str(data), XDG_CONFIG_HOME=str(config),
               XDG_CACHE_HOME=str(home / 'custom-cache'), TMPDIR=str(home / 'tmp'),
               XDG_DATA_DIRS=str(system) + ':/usr/share', XDG_CURRENT_DESKTOP='X-Generic',
               DISPLAY=':65530', WAYLAND_DISPLAY='abdm-test-no-display',
               DBUS_SESSION_BUS_ADDRESS='unix:path=' + str(case / 'no-session-bus'))
    # All database updates are scoped to our owned fake desktop; no KDE/global cache tools.
    for apps in [system / 'applications', data / 'applications']:
        subprocess.run(['update-desktop-database', str(apps)], env=env, check=True, timeout=15)
    video = home / 'video ü ; spaces.mp4'
    video.write_bytes(b'\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom')
    detected = subprocess.check_output(['xdg-mime', 'query', 'filetype', str(video)], env=env, text=True, timeout=10).strip()
    assert detected == mime, 'Refuse to launch anything unless the disposable MIME fixture is recognized: ' + detected
    for kind in [mime, 'inode/directory', 'x-scheme-handler/' + scheme]:
        selected = subprocess.check_output(['xdg-mime', 'query', 'default', kind], env=env, text=True, timeout=10).strip()
        assert selected == desktop_id, 'Refuse to open anything with a non-fixture handler: ' + selected
    source = case / 'ExternalOpenSmoke.java'
    source.write_text(HARNESS)
    jars = sorted((bundle / 'lib/app').glob('*.jar'))
    subprocess.run(['/usr/lib/jvm/java-25-openjdk/bin/javac', '-cp', os.pathsep.join(map(str, jars)), str(source)], check=True, timeout=30)
    cmd, isolated = launcher.command(bundle, profile, 'wayland', False, env, cli_args=[])
    cmd[cmd.index('-cp') + 1] += os.pathsep + str(case)
    assert cmd[-1] == 'com.abdownloadmanager.desktop.cli.CliAppKt'
    url = scheme + '://example/path?from=ABDM'
    cmd[-1:] = ['ExternalOpenSmoke', str(video), str(home), url, str(record)]
    # An unset original value must remove a later profile-local override as well.
    isolated['XDG_STATE_HOME'] = str(profile / 'private-state')
    before = smoke.protected_snapshot()
    vlc = Path.home() / '.local/share/applications/vlc.desktop'
    vlc_before = vlc.read_bytes() if vlc.exists() else None
    result = {'case': str(case), 'bundle': str(bundle), 'errors': []}
    try:
        process = subprocess.run(cmd, env=isolated, cwd=profile, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
        (case / 'harness.log').write_text(process.stdout)
        assert process.returncode == 0, process.stdout
        rows = [json.loads(line) for line in record.read_text().splitlines()]
        assert len(rows) == 3
        opened = set()
        for row in rows:
            assert row['args'][0] == 'CUSTOM-USER-BUILD', 'User desktop override was not honored: ' + repr(row)
            value = row['args'][1]
            opened.add(unquote(urlsplit(value).path) if value.startswith('file:') else value)
            for name in launcher.DESKTOP_VARIABLES:
                assert row['environment'][name] == env.get(name), (name, row)
            assert row['environment'][launcher.DESKTOP_CONTEXT] is None
        assert opened == {str(video), str(home), url}, opened
        result['launches'] = rows
        result['parent_profile_isolated'] = True
        result['unset_variables_removed'] = True
    except Exception as error:
        result['errors'].append(repr(error))
        raise
    finally:
        assert smoke.protected_snapshot() == before, 'Protected production files changed'
        assert (vlc.read_bytes() if vlc.exists() else None) == vlc_before, 'Real VLC desktop entry changed'
        (OUTPUT / 'external-open-results.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
