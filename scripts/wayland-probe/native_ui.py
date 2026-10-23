"""Build a reviewable, pinned experimental UI jar; never modify the packaged dependency.

Only compile.py --native-ui calls the builder. run.py substitutes the resulting artifact
for the original UI jar in named experimental cases, rather than shadowing classes.
"""
import hashlib
from pathlib import Path
import subprocess
import zipfile

SOURCE_SHA256 = 'ae1ccb98d6d7335c7e2d0ec5024e6cda800d1737bab3477f81c45914e27b786e'
BINARY_SHA256 = 'eaee42812c81cf1f1a925088bd7e847a343decbc3ccb1682f00f203123a51a17'
SOURCE_ENTRY = 'desktopMain/androidx/compose/ui/awt/ComposeWindowPanel.desktop.kt'
CLASS_PREFIX = 'androidx/compose/ui/awt/ComposeWindowPanel'
PATCH = Path(__file__).with_name('compose-1.12.1-native-wayland.patch')


def explicit_classpath_jars(directory):
    # Launcher-only manifest redirects back to the ORIGINAL UI dependency. It is redundant
    # with an explicit classpath and must not be allowed to defeat dependency substitution.
    return [p for p in sorted(directory.glob('*.jar')) if p.name != 'pathing-classpath.jar']


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_hash(path, expected):
    if not path.is_file() or sha256(path) != expected:
        raise ValueError(f'Missing or unreviewed input: {path}; see README.md for the pinned artifact.')


def prepare_source(archive, destination):
    require_hash(archive, SOURCE_SHA256)
    with zipfile.ZipFile(archive) as jar:
        original = jar.read(SOURCE_ENTRY)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(original)  # retains upstream license
    subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', str(destination), str(PATCH)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def is_panel_class(name):
    return name == CLASS_PREFIX + '.class' or (name.startswith(CLASS_PREFIX + '$') and name.endswith('.class'))


def build_jar(original, classes, destination):
    if original.resolve() == destination.resolve():
        raise ValueError('Experimental output must never replace the original dependency.')
    require_hash(original, BINARY_SHA256)
    replacements = {str(p.relative_to(classes)): p.read_bytes() for p in classes.rglob('*.class')}
    if CLASS_PREFIX + '.class' not in replacements or not all(map(is_panel_class, replacements)):
        raise ValueError('Compiler output is not limited to the reviewed ComposeWindowPanel class family.')
    # Do not replace upstream Kotlin module metadata with metadata for a one-file compilation.
    with zipfile.ZipFile(original) as source:
        names = source.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate entries in upstream jar.')
        if any(n.upper().startswith('META-INF/') and n.upper().endswith(('.SF', '.RSA', '.DSA')) for n in names):
            raise ValueError('Refusing to invalidate a signed upstream jar.')
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix('.tmp')
        try:
            with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as target:
                for info in source.infolist():
                    if not is_panel_class(info.filename):
                        target.writestr(info, source.read(info.filename))
                for name, data in sorted(replacements.items()):
                    # A fixed timestamp makes repeated builds comparable.
                    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    target.writestr(info, data)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    return {
        'compose': '1.12.1',
        'source_sha256': SOURCE_SHA256,
        'binary_sha256': BINARY_SHA256,
        'patch_sha256': sha256(PATCH),
        'artifact_sha256': sha256(destination),
    }


def validate_artifact(original, artifact, manifest):
    require_hash(original, BINARY_SHA256)
    expected = {'compose': '1.12.1', 'source_sha256': SOURCE_SHA256,
                'binary_sha256': BINARY_SHA256, 'patch_sha256': sha256(PATCH),
                'artifact_sha256': sha256(artifact)}
    if manifest != expected:
        raise ValueError('Stale/unreviewed experimental UI artifact; recompile with --native-ui.')
