"""Read the profiled Office Node links, with parent and sibling validation.

Unlike office-metadata.py's reference graph this follows only the original
Node parent/first-child/next-sibling fields. Unknown node classes remain opaque;
their bytes are never interpreted as Element layout or executed.
"""
import argparse
import ctypes as C
import json
import pathlib
import runpy
import struct

metadata = runpy.run_path(str(pathlib.Path(__file__).with_name('office-metadata.py')))
Reader, Metadata, EXPECTED = (metadata[name] for name in ('Reader', 'Metadata', 'EXPECTED'))


def inspect(hwnd, budget):
    reader = Reader(hwnd)
    try:
        modules = [m for m in reader.modules if m['name'].lower() in EXPECTED]
        if len(modules) != len(EXPECTED):
            raise ValueError('The complete Office ABI profile is required')
        for module in modules:
            if metadata['identity'](reader, module) != EXPECTED[module['name'].lower()]:
                raise ValueError('Unknown Office ABI: ' + module['name'])
        types = Metadata(reader, modules)
        ui = next(m for m in modules if m['name'].lower() == 'mso40uiwin32client.dll')
        for rva, expected in ((0x1af790, '488b4110c3'), (0x1dc470, '488b4108c3'),
                              (0x1dbc60, '488b4118c3'), (0x1dc4a0, '488b4120c3')):
            if reader.read(ui['base'] + rva, len(bytes.fromhex(expected))) != bytes.fromhex(expected):
                raise ValueError('Original Node link accessor does not match the ABI')
        user = C.WinDLL('user32')
        user.GetClassNameW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_int]
        user.GetWindowLongPtrW.argtypes = [C.c_void_p, C.c_int]
        user.GetWindowLongPtrW.restype = C.c_ssize_t
        user.IsWindowVisible.argtypes = [C.c_void_p]
        callback_type = C.WINFUNCTYPE(C.c_int, C.c_void_p, C.c_ssize_t)
        user.EnumChildWindows.argtypes = [C.c_void_p, callback_type, C.c_ssize_t]
        roots, nodes, errors = [], [], []
        seen = set()

        def walk(address, parent, root, depth):
            if len(nodes) >= budget or depth > 64:
                errors.append({'kind': 'budget', 'address': hex(address)})
                return
            if address in seen:
                errors.append({'kind': 'duplicateNode', 'address': hex(address)})
                return
            table = reader.integer(address)
            if not types.module(table, 26 * 8):
                raise ValueError('Node vtable is outside the verified framework images')
            if reader.integer(address + 8) != parent:
                raise ValueError('Node parent link is inconsistent')
            seen.add(address)
            record = types.object(address)
            node = {'address': hex(address), 'parent': hex(parent), 'rootHwnd': hex(root),
                    'depth': depth, 'vtableRva': hex(table - types.module(table)['base']),
                    'class': record['classes'][0]['name'] if record else None,
                    'children': []}
            # A nameless metadata record must not prevent traversal of Node
            # links. Only verified AppFrameHelper instances get geometry fields.
            if node['class'] == 'AppFrameHelper':
                x, y, width, height = struct.unpack('<4i', reader.read(address + 0x78, 16))
                node['localLayoutCandidate'] = [x, y, width, height]
            nodes.append(node)
            child = first = reader.integer(address + 0x10)
            siblings = set()
            while child:
                if child in siblings:
                    raise ValueError('Next-sibling cycle')
                siblings.add(child)
                node['children'].append(hex(child))
                walk(child, address, root, depth + 1)
                following = reader.integer(child + 0x18)
                if not following:
                    if reader.integer(first + 0x20) != child:
                        raise ValueError('First-child tail link is inconsistent')
                    break
                if reader.integer(following + 0x20) != child:
                    raise ValueError('Sibling back link is inconsistent')
                child = following
                if len(siblings) > budget:
                    raise ValueError('Sibling budget exceeded')

        def visit(window, unused):
            name = C.create_unicode_buffer(128)
            user.GetClassNameW(window, name, len(name))
            if name.value != 'NetUIHWND':
                return 1
            address = user.GetWindowLongPtrW(window, 0)
            roots.append({'hwnd': hex(window), 'object': hex(address),
                          'windowVisible': bool(user.IsWindowVisible(window))})
            try:
                walk(address, 0, window, 0)
            except (OSError, ValueError) as error:
                errors.append({'kind': 'invalidTree', 'rootHwnd': hex(window), 'error': str(error)})
            return 1

        callback = callback_type(visit)
        user.EnumChildWindows(hwnd, callback, 0)
        if not roots:
            errors.append({'kind': 'missingRoots'})
        return {'pid': reader.pid, 'targetCodeExecuted': False, 'layoutModified': False,
                'roots': roots, 'nodes': nodes, 'errors': errors, 'complete': not errors}
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hwnd', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--max-nodes', type=int, default=3000)
    args = parser.parse_args()
    if not 1 <= args.max_nodes <= 10000:
        parser.error('max-nodes must be between 1 and 10000')
    report = inspect(args.hwnd, args.max_nodes)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'nodes': len(report['nodes']), 'complete': report['complete'],
                      'errors': report['errors']}))
