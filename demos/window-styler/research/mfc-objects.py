"""Bounded, read-only discovery of CWnd objects through MFC runtime classes.

Search only committed private writable memory for exact target HWND values.
Validate a candidate's immutable GetRuntimeClass method and CRuntimeClass chain;
never execute or alter target code and never enable a layout backend from a hit.
"""
import argparse
import ctypes as C
import json
import pathlib
import runpy
import struct

from ctypes import wintypes as W

Reader = runpy.run_path(str(pathlib.Path(__file__).with_name('read-framework-objects.py')))['Reader']


class MemoryRegion(C.Structure):
    _fields_ = [('base', C.c_void_p), ('allocation', C.c_void_p), ('protect0', W.DWORD),
                ('partition', W.WORD), ('size', C.c_size_t), ('state', W.DWORD),
                ('protect', W.DWORD), ('kind', W.DWORD)]


def describe(reader, candidate, required='CWnd'):
    try:
        table = reader.integer(candidate)
        module = reader.module(table)
        if not module:
            return None
        function = reader.integer(table)
        if reader.module(function) != module:
            return None
        code = reader.read(function, 16)
        if code[:3] == b'\x48\x8d\x05' and code[7] == 0xc3:
            runtime = function + 7 + struct.unpack_from('<i', code, 3)[0]
        elif code[0] == 0xb8 and code[5] == 0xc3:
            runtime = struct.unpack_from('<I', code, 1)[0]
        else:
            return None
        classes, visited = [], set()
        while runtime and runtime not in visited and len(classes) < 20:
            visited.add(runtime)
            image = reader.module(runtime)
            if not image:
                break
            name_pointer = reader.integer(runtime)
            name = reader.read(name_pointer, 100).split(b'\0')[0].decode('ascii')
            if not name or not all(c.isalnum() or c in '_:?$<>, ' for c in name):
                break
            size = reader.integer(runtime + reader.ptr, 4)
            if not 8 <= size <= 0x1000000:
                break
            classes.append({'name': name, 'objectSize': size, 'runtimeClass': hex(runtime)})
            runtime = reader.integer(runtime + (24 if reader.ptr == 8 else 16))
        if not any(c['name'] == 'CObject' for c in classes) or \
                (required and not any(c['name'] == required for c in classes)):
            return None
        return {'address': hex(candidate), 'module': module['name'], 'vtableRva': hex(table - module['base']),
                'runtimeGetterRva': hex(function - module['base']), 'classes': classes}
    except (OSError, ValueError, UnicodeError, struct.error):
        return None


def framework_references(reader, roots, budget=128):
    """Explore typed CObject references only, not arbitrary application data."""
    queue = [int(root['address'], 16) for root in roots]
    records = {}
    while queue and len(records) < budget:
        pointer = queue.pop(0)
        if pointer in records:
            continue
        record = describe(reader, pointer, required=None)
        if not record:
            continue
        framework = next((c for c in record['classes'] if c['name'].startswith('CBCGP')), None)
        if not framework:
            continue
        records[pointer] = record
        record['references'] = []
        # Respect the framework's runtime-class size; do not inspect fields
        # added by a host's derived frame class.
        size = min(framework['objectSize'], 16384)
        try:
            raw = reader.read(pointer, size)
        except OSError:
            continue
        added = set()

        def add(candidate, field, kind):
            if candidate == pointer or candidate in added:
                return
            target = describe(reader, candidate, required=None)
            if not target or not any(c['name'].startswith('CBCGP') for c in target['classes']):
                return
            added.add(candidate)
            record['references'].append({'field': hex(field), 'kind': kind, 'object': hex(candidate),
                                         'class': target['classes'][0]['name']})
            queue.append(candidate)

        for offset in range(0, size - reader.ptr + 1, reader.ptr):
            value = int.from_bytes(raw[offset:offset + reader.ptr], 'little')
            if value < 65536:
                continue
            add(value, offset, 'pointer')
            if offset:
                add(pointer + offset, offset, 'embedded')
            if reader.module(value):
                continue
            try:
                array = reader.read(value, reader.ptr * 16)
            except OSError:
                continue
            for i in range(16):
                add(int.from_bytes(array[i * reader.ptr:(i + 1) * reader.ptr], 'little'), offset, 'array-prefix')
    return list(records.values()), bool(queue)


def inspect(hwnd, limit):
    reader = Reader(hwnd)
    try:
        user = C.WinDLL('user32')
        user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, C.c_ssize_t)
        user.EnumChildWindows.argtypes = [W.HWND, callback_type, C.c_ssize_t]
        windows = {hwnd: 'frame'}

        def visit(window, unused):
            name = C.create_unicode_buffer(256)
            user.GetClassNameW(window, name, len(name))
            if name.value.startswith('BCGPRibbon'):
                windows[window] = name.value
            return True

        callback = callback_type(visit)
        user.EnumChildWindows(hwnd, callback, 0)
        kernel = C.WinDLL('kernel32', use_last_error=True)
        kernel.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MemoryRegion), C.c_size_t]
        kernel.VirtualQueryEx.restype = C.c_size_t
        patterns = {window: window.to_bytes(reader.ptr, 'little') for window in windows}
        address, read_bytes, regions, candidates, visited = 0, 0, 0, [], set()
        while read_bytes < limit:
            region = MemoryRegion()
            if not kernel.VirtualQueryEx(reader.h, address, C.byref(region), C.sizeof(region)):
                break
            next_address = (region.base or address) + region.size
            if next_address <= address:
                break
            address = next_address
            if region.state != 0x1000 or region.kind != 0x20000 or region.protect & 0x100 or \
                    region.protect & 0xff not in (4, 8, 0x40, 0x80):
                continue
            regions += 1
            for offset in range(0, region.size, 65536):
                size = min(65536, region.size - offset, limit - read_bytes)
                if size < reader.ptr:
                    break
                start = region.base + offset
                try:
                    raw = reader.read(start, size)
                except OSError:
                    continue
                read_bytes += size
                for window, pattern in patterns.items():
                    cursor = raw.find(pattern)
                    while cursor >= 0:
                        hit = start + cursor
                        if hit % reader.ptr == 0:
                            for displacement in range(reader.ptr, 0x101, reader.ptr):
                                candidate = hit - displacement
                                key = candidate, window
                                if key in visited:
                                    continue
                                visited.add(key)
                                record = describe(reader, candidate)
                                if record and displacement < record['classes'][0]['objectSize']:
                                    record.update(hwnd=hex(window), windowClass=windows[window],
                                                  hwndOffset=hex(displacement))
                                    candidates.append(record)
                        cursor = raw.find(pattern, cursor + 1)
        # A CWnd's own HWND field has one layout across frame/ribbon/statusbar.
        # Correlate independent window identities; discard tooltip backrefs at
        # another offset rather than treating every HWND-valued field as owner.
        offsets = {}
        for candidate in candidates:
            offsets.setdefault(candidate['hwndOffset'], set()).add(candidate['hwnd'])
        consistent = [offset for offset, values in offsets.items() if len(values) == len(windows)]
        verified_offset = consistent[0] if len(consistent) == 1 and len(windows) >= 3 else None
        roots = [c for c in candidates if verified_offset and c['hwndOffset'] == verified_offset]
        references, reference_limit = framework_references(reader, roots)
        return {'pid': reader.pid, 'targetCodeExecuted': False, 'layoutModified': False,
                'bytesRead': read_bytes, 'privateRegions': regions, 'scanLimitReached': read_bytes >= limit,
                'windows': [{'hwnd': hex(w), 'class': name} for w, name in windows.items()],
                'candidates': candidates, 'correlatedCWndHwndOffset': verified_offset,
                'frameworkObjects': references, 'referenceBudgetReached': reference_limit}
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hwnd', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--max-mib', type=int, default=256)
    args = parser.parse_args()
    if not 1 <= args.max_mib <= 512:
        parser.error('max-mib must be between 1 and 512')
    result = inspect(args.hwnd, args.max_mib * 1024 * 1024)
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'pid': result['pid'], 'bytesRead': result['bytesRead'],
                      'candidates': len(result['candidates'])}))
