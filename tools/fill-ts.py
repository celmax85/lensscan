#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Fills i18n/lensscan_<lang>.ts from tools/i18n/<lang>.py (TR: source -> text,
# NUMERUS: source -> forms). Run after `cmake --build <dir> --target update_translations`.
# Usage: tools/fill-ts.py fr de es it pt   (no argument = every table in tools/i18n)
import html, importlib.util, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def load(lang):
    spec = importlib.util.spec_from_file_location(lang, os.path.join(HERE, 'i18n', lang + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.TR, getattr(mod, 'NUMERUS', {})

def fill(lang):
    tr, numerus = load(lang)
    path = os.path.join(ROOT, 'i18n', 'lensscan_%s.ts' % lang)
    ts = open(path, encoding='utf-8').read()
    missing = []
    esc = lambda t: html.escape(t, quote=False)
    def repl(m):
        head, src = m.group(1), m.group(2)
        key = html.unescape(src)
        if 'type="vanished"' in m.group(0) or 'type="obsolete"' in m.group(0):
            return m.group(0)
        if key in numerus:
            forms = ''.join('\n            <numerusform>%s</numerusform>' % esc(f) for f in numerus[key])
            return '%s<source>%s</source>\n        <translation>%s\n        </translation>' % (head, src, forms)
        if key in tr:
            return '%s<source>%s</source>\n        <translation>%s</translation>' % (head, src, esc(tr[key]))
        missing.append(key)
        return m.group(0)
    ts = re.sub(r'(<message[^>]*>\s*(?:<location[^>]*/>\s*)*)<source>(.*?)</source>\s*<translation[^>]*>(?:.*?)</translation>',
                repl, ts, flags=re.S)
    open(path, 'w', encoding='utf-8').write(ts)
    print('%s missing: %s' % (lang, missing))

langs = sys.argv[1:] or sorted(f[:-3] for f in os.listdir(os.path.join(HERE, 'i18n')) if f.endswith('.py'))
for l in langs:
    fill(l)
