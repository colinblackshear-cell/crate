# One-name check for the iPhone build (run by .github/workflows/ios.yml):
#   python3 tools/pocket/check_class_names.py <repo root> [vocoder dir]
# Two global classes with the same name in one program break C++'s one-definition rule and the
# linker silently keeps one of them: that is how Pocket's DRUMS grid ended up running the MPC
# StepGrid's paint and froze the phone. Put file-local classes in an unnamed namespace.
#
# Lists class/struct names defined at namespace scope (outside anonymous namespaces) in the sources the
# iPhone build links, with the files defining them, and prints names defined in more than one place.
import re, sys, os, glob
root = sys.argv[1]
T = root + '/tools'
files = sorted(glob.glob(T + '/pocket/*.cpp') + glob.glob(T + '/pocket/*.h') + glob.glob(T + '/pocket/*.mm'))
files = [f for f in files if not f.endswith('PocketRoute.cpp') and not f.endswith('PocketStress.cpp')]
extra = ['core/link/PocketLink.cpp', 'core/AudioFiles.cpp', 'core/Edits.cpp', 'core/KeyboardSampler.cpp', 'core/SoundFx.cpp',
         'core/SoundLibrary.cpp', 'core/SoundBank.cpp', 'core/ui/EffectsPanel.cpp', 'core/ui/LibraryList.cpp', 'core/ui/WaveView.cpp',
         'tools/synth/SynthEngine.cpp', 'tools/rig/StripFx.cpp', 'tools/rig/Pedalboard.cpp', 'tools/drums/DrumEngine.cpp',
         'tools/mpc/mpc/MpcFiles.cpp', 'tools/mpc/mpc/BankWriter.cpp',
         'tools/drums/DrumProcessor.cpp', 'tools/drums/DrumView.cpp', 'tools/mpc/BankBrowser.cpp', 'tools/mpc/BankModel.cpp',
         'tools/mpc/LivePanel.cpp', 'tools/mpc/MpcProcessor.cpp', 'tools/mpc/MpcView.cpp', 'tools/mpc/PadEditor.cpp',
         'tools/mpc/PadGrid.cpp', 'tools/mpc/PadSoundEditor.cpp', 'tools/mpc/SampleTools.cpp', 'tools/mpc/Sequence.cpp',
         'tools/mpc/SequencerEngine.cpp', 'tools/mpc/SequencerView.cpp', 'core/control/Controllers.cpp']
files += [root + '/' + e for e in extra]
voc = sys.argv[2] if len(sys.argv) > 2 else None
if voc:
    files += [voc + '/Source/' + n for n in ('PluginProcessor.cpp', 'PluginEditor.cpp', 'MainPanel.cpp', 'EasyPanel.cpp')]
# headers next to each source, too (classes are usually defined there)
more = []
for f in files:
    h = os.path.splitext(f)[0] + '.h'
    if os.path.exists(h) and h not in files:
        more.append(h)
files += more
if voc:
    files += glob.glob(voc + '/Source/**/*.h', recursive=True)
files += glob.glob(root + '/core/**/*.h', recursive=True)
files = sorted(set(f for f in files if os.path.exists(f)))

defs = {}
tok = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{};]|\b(?:class|struct|namespace|enum)\b[^{;]*', re.S)
for f in files:
    src = open(f, errors='replace').read()
    src = re.sub(r'^\s*#.*$', '', src, flags=re.M)
    stack = []   # entries: ('ns', name) or ('other', None)
    pending = None
    for m in tok.finditer(src):
        t = m.group(0)
        if t.startswith('//') or t.startswith('/*') or t.startswith('"') or t.startswith("'"):
            continue
        if t == '{':
            if pending is not None:
                if len(pending) == 3:
                    defs.setdefault(pending[2], set()).add(os.path.relpath(f, root) if f.startswith(root) else f)
                    pending = ('other', None)
                stack.append(pending)
                pending = None
            else:
                stack.append(('other', None))
        elif t == '}':
            if stack:
                stack.pop()
        elif t == ';':
            pending = None
        else:
            kw = t.split()[0]
            if kw == 'namespace':
                parts = t.split()
                name = parts[1] if len(parts) > 1 else ''
                pending = ('ns', name)
            elif kw == 'enum':
                pending = ('other', None)
            else:
                # class/struct Name [final] [: bases]
                mm = re.match(r'(?:class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?(?:alignas\([^)]*\)\s*)?([A-Za-z_]\w*(?:::\w+)*)', t)
                if not mm or '(' in t.split(':')[0]:
                    pending = ('other', None)
                    continue
                name = mm.group(1)
                inside_type = any(k == 'other' for k, _ in stack)
                anon = any(k == 'ns' and n == '' for k, n in stack)
                if not inside_type and not anon and '::' not in name:
                    ns = '::'.join(n for k, n in stack if k == 'ns')
                    full = (ns + '::' if ns else '') + name
                    pending = ('other', None, full)
                else:
                    pending = ('other', None)
dups = {k: v for k, v in defs.items() if len(v) > 1}
for k in sorted(dups):
    print(k, '->', ', '.join(sorted(dups[k])))
print('scanned', len(files), 'files,', len(defs), 'namespace-scope classes,', len(dups), 'defined in more than one file')
sys.exit(1 if dups else 0)
