"""Read Office NetUI framework class metadata without executing target code.

Only immutable class-info getter instructions are decoded. Object references in
the report are not assumed to be the visual child tree or a writable layout API.
"""
import argparse
import ctypes as C
import json
import pathlib
import runpy
import struct
import uuid

from ctypes import wintypes as W

Reader = runpy.run_path(str(pathlib.Path(__file__).with_name('read-framework-objects.py')))['Reader']
EXPECTED = {
    'mso40uiwin32client.dll': ('2D264595-7EA8-4318-8AA1-897691F9D8EE', 2),
    'mso98win32client.dll': ('7DA1C564-0BEE-44F1-833B-5036838211D4', 2),
    'mso.dll': ('609D7177-47D3-4508-B59D-AB1FD4FB1624', 2),
}


def identity(reader, module):
    base = module['base']
    header = reader.read(base, 64)
    if header[:2] != b'MZ':
        raise ValueError('Invalid module DOS header')
    nt = struct.unpack_from('<I', header, 60)[0]
    if nt > module['size'] - 264:
        raise ValueError('Invalid module NT header')
    header = reader.read(base + nt, 264)
    if header[:4] != b'PE\0\0' or struct.unpack_from('<H', header, 24)[0] != 0x20b:
        raise ValueError('This profile requires a PE32+ module')
    address, size = struct.unpack_from('<II', header, 24 + 112 + 6 * 8)
    if size > 4096 or address + size > module['size']:
        raise ValueError('Invalid CodeView directory')
    for offset in range(0, size - 27, 28):
        entry = reader.read(base + address + offset, 28)
        kind, length, rva = struct.unpack_from('<III', entry, 12)
        if kind != 2 or length < 24 or rva + 24 > module['size']:
            continue
        value = reader.read(base + rva, 24)
        if value[:4] == b'RSDS':
            return str(uuid.UUID(bytes_le=value[4:20])).upper(), struct.unpack_from('<I', value, 20)[0]
    raise ValueError('CodeView identity is missing')


class Metadata:
    def __init__(self, reader, modules):
        self.reader = reader
        self.modules = modules

    def module(self, address, length=1):
        return next((m for m in self.modules if m['base'] <= address and
                     address + length <= m['base'] + m['size']), None)

    def string(self, address):
        module = self.module(address)
        if not module:
            raise ValueError('Class name is outside the verified framework image')
        value = self.reader.read(address, min(160, module['base'] + module['size'] - address))
        end = next((i for i in range(0, len(value) - 1, 2) if value[i:i + 2] == b'\0\0'), None)
        if end is None:
            raise ValueError('Unterminated class name')
        text = value[:end].decode('utf-16-le')
        if not text or len(text) > 70 or not all(c.isascii() and (c.isalnum() or c in '_:<>') for c in text):
            raise ValueError('Not a framework class name')
        return text

    def class_info(self, address):
        chain = []
        visited = set()
        while self.module(address, 40) and address not in visited and len(chain) < 12:
            visited.add(address)
            # Verified against HWNDElement's immutable class record: +0x20 is
            # the UTF-16 framework class name; +0x18 links to base class info.
            try:
                name = self.string(self.reader.integer(address + 0x20))
            except (OSError, ValueError, UnicodeError):
                break
            module = self.module(address)
            chain.append({'name': name, 'module': module['name'], 'classInfoRva': hex(address - module['base'])})
            address = self.reader.integer(address + 0x18)
        return chain

    def properties(self, address):
        """Read immutable PropertyInfo declarations, never object values."""
        result = []
        table = self.reader.integer(address + 8)
        count = self.reader.integer(address + 16, 4)
        if count > 512 or (count and not self.module(table, count * 8)):
            return result
        for index in range(count):
            try:
                descriptor = self.reader.integer(table + index * 8)
                module = self.module(descriptor, 56)
                if not module:
                    continue
                pointer = self.reader.integer(descriptor)
                if not self.module(pointer, 96):
                    continue
                name = self.reader.read(pointer, 96).split(b'\0')[0].decode('ascii')
                if not name or not all(c.isalnum() or c in '_:' for c in name):
                    continue
                record = {'name': name, 'module': module['name'],
                          'descriptorRva': hex(descriptor - module['base'])}
                enumeration = self.reader.integer(descriptor + 0x18)
                choices = []
                if enumeration and self.module(enumeration, 16):
                    for i in range(64):
                        item = enumeration + i * 16
                        if not self.module(item, 16):
                            break
                        label = self.reader.integer(item)
                        if not label:
                            break
                        choices.append({'name': self.string(label),
                                        'value': self.reader.integer(item + 8, 4)})
                if choices:
                    record['enumValues'] = choices
                result.append(record)
            except (OSError, ValueError, UnicodeError, struct.error):
                continue
        return result

    def object(self, address):
        try:
            table = self.reader.integer(address)
            module = self.module(table, 26 * 8)
            if not module:
                return None
            getter = self.reader.integer(table + 25 * 8)
            function_module = self.module(getter, 18)
            if not function_module:
                return None
            code = self.reader.read(getter, 18)
            if code[:3] == b'\x48\x8d\x05' and code[7] == 0xc3:
                info = getter + 7 + struct.unpack_from('<i', code, 3)[0]
            elif code[:5] == b'\x48\x83\x79\x68\x00' and code[5:8] == b'\x48\x8d\x05' and \
                    code[12:] == b'\x48\x0f\x45\x41\x68\xc3':
                # Element's alternate getter returns its dynamic class-info
                # field when set, otherwise its immutable default class info.
                info = self.reader.integer(address + 0x68) or getter + 12 + struct.unpack_from('<i', code, 8)[0]
            else:
                return None
            classes = self.class_info(info)
            if not classes:
                return None
            return {'address': hex(address), 'module': module['name'], 'vtableRva': hex(table - module['base']),
                    'classGetterRva': hex(getter - function_module['base']), 'classes': classes}
        except (OSError, ValueError, UnicodeError, struct.error):
            return None


def inspect(hwnd, budget):
    reader = Reader(hwnd)
    try:
        modules, identities = [], []
        for module in reader.modules:
            name = module['name'].lower()
            if name not in EXPECTED:
                continue
            current = identity(reader, module)
            if current != EXPECTED[name]:
                raise ValueError('Unknown Office NetUI ABI: ' + name + ' ' + repr(current))
            modules.append(module)
            identities.append({'module': module['name'], 'codeView': current[0], 'age': current[1]})
        if not modules:
            raise ValueError('No verified Office NetUI implementation is loaded')
        metadata = Metadata(reader, modules)
        windows = []
        callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, C.c_ssize_t)

        def visit(window, unused):
            name = C.create_unicode_buffer(128)
            reader_window = C.WinDLL('user32')
            reader_window.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
            reader_window.GetClassNameW(window, name, len(name))
            if name.value == 'NetUIHWND':
                windows.append(window)
            return True

        user = C.WinDLL('user32')
        user.EnumChildWindows.argtypes = [W.HWND, callback_type, C.c_ssize_t]
        user.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
        user.GetWindowLongPtrW.restype = C.c_ssize_t
        callback = callback_type(visit)
        user.EnumChildWindows(hwnd, callback, 0)
        roots, queue, objects = [], [], {}
        for window in windows[:256]:
            pointer = user.GetWindowLongPtrW(window, 0)
            record = metadata.object(pointer)
            roots.append({'hwnd': hex(window), 'object': hex(pointer), 'recognized': record is not None})
            if record:
                queue.append(pointer)
        while queue and len(objects) < budget:
            pointer = queue.pop(0)
            if pointer in objects:
                continue
            record = metadata.object(pointer)
            if not record:
                continue
            objects[pointer] = record
            record['references'] = []
            for offset in range(8, 0x100, 8):
                try:
                    reference = reader.integer(pointer + offset)
                except OSError:
                    continue
                target = metadata.object(reference)
                if target:
                    record['references'].append({'field': hex(offset), 'object': hex(reference),
                                                 'class': target['classes'][0]['name']})
                    queue.append(reference)
        schemas = {}
        for obj in objects.values():
            for info in obj['classes']:
                if info['name'] not in ('AppFrameHelper', 'SimpleButton', 'HWNDElement'):
                    continue
                key = info['module'] + ':' + info['classInfoRva']
                if key in schemas:
                    continue
                module = next(m for m in modules if m['name'] == info['module'])
                schemas[key] = {'name': info['name'], 'properties': metadata.properties(
                    module['base'] + int(info['classInfoRva'], 16))}
        return {'pid': reader.pid, 'modules': identities, 'classSchemas': list(schemas.values()),
                'targetCodeExecuted': False, 'layoutModified': False, 'roots': roots,
                'objects': list(objects.values()), 'budgetReached': bool(queue)}
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hwnd', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--max-objects', type=int, default=256)
    arguments = parser.parse_args()
    if not 1 <= arguments.max_objects <= 1000:
        parser.error('max-objects must be between 1 and 1000')
    result = inspect(arguments.hwnd, arguments.max_objects)
    arguments.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'pid': result['pid'], 'roots': len(result['roots']), 'objects': len(result['objects']),
                      'budgetReached': result['budgetReached']}))
