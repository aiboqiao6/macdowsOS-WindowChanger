"""Read-only Chromium Views ABI investigation; never writes target memory."""
import argparse
import ctypes as C
import json
import struct
import capstone
from ctypes import wintypes as W
from pe_inspect import PE


class Process:
    def __init__(self, hwnd):
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.u = C.WinDLL('user32', use_last_error=True)
        self.u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.u.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
        self.u.GetWindowLongPtrW.restype = C.c_ssize_t
        self.k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.k.OpenProcess.restype = W.HANDLE
        self.k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p]
        self.k.CloseHandle.argtypes = [W.HANDLE]
        self.pid = W.DWORD()
        self.thread = self.u.GetWindowThreadProcessId(hwnd, C.byref(self.pid))
        self.h = self.k.OpenProcess(0x410, False, self.pid.value)
        if not self.h:
            raise C.WinError(C.get_last_error())
        self.userdata = self.u.GetWindowLongPtrW(hwnd, -21)

    def read(self, address, size):
        if size < 0 or size > 0x100000:
            raise ValueError('invalid read size')
        data = C.create_string_buffer(size)
        if not self.k.ReadProcessMemory(self.h, address, data, size, None):
            raise C.WinError(C.get_last_error())
        return data.raw

    def q(self, address):
        return struct.unpack('<Q', self.read(address, 8))[0]

    def close(self):
        self.k.CloseHandle(self.h)


def cached_metadata(process, pe, base, view):
    """Recognize only a metadata accessor's cache load; do not execute it."""
    vtable = process.q(view + getattr(process, 'metadata_offset', 0x100))
    getter = process.q(vtable + 8) - base
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for ins in md.disasm(pe.image[getter:getter+160], getter):
        if ins.mnemonic == 'mov' and len(ins.operands) == 2:
            src = ins.operands[1]
            if src.type == capstone.x86.X86_OP_MEM and src.mem.base == capstone.x86.X86_REG_RIP:
                address = ins.address + ins.size + src.mem.disp
                candidate = process.q(base + address)
                if candidate:
                    try:
                        raw = process.read(candidate, 104)
                        name, length = struct.unpack_from('<QQ', raw, 8)
                        if 0 < length < 180 and base < name < base + len(pe.image):
                            value = process.read(name, length).decode('ascii')
                            return {'address': hex(candidate), 'name': value, 'raw': raw.hex(), 'getter': hex(getter)}
                    except (OSError, UnicodeError):
                        pass
    return {'getter': hex(getter), 'uninitialized': True}


def libcxx_string(process, address):
    raw = process.read(address, 24)
    # Target clang/libc++ uses _LIBCPP_ABI_ALTERNATE_STRING_LAYOUT.
    if raw[23] & 0x80:
        pointer, length = struct.unpack_from('<QQ', raw)
        if length > 1000:
            raise ValueError('oversized string')
        return process.read(pointer, length).decode('utf-8')
    return raw[:raw[23]].decode('utf-8')


def describe_metadata(process, pe, base, address):
    raw = process.read(address, 104)
    name, length = struct.unpack_from('<QQ', raw, 8)
    members, size, capacity, parent = struct.unpack_from('<4Q', raw, 0x30)
    if getattr(process,'vector_ends',False):size,capacity=(size-members)//8,(capacity-members)//8
    if size > 300 or capacity < size:
        raise ValueError('invalid metadata members')
    result = {'address': hex(address), 'name': process.read(name, length).decode('ascii'),
              'parent': hex(parent), 'members': []}
    for i in range(size):
        m = process.q(members + i*8)
        vt = process.q(m)
        result['members'].append({'address': hex(m), 'name': libcxx_string(process, m+8),
                                  'type': libcxx_string(process, m+32),
                                  'vtable': hex(vt-base),
                                  'functions': [hex(process.q(vt+j)-base) for j in range(0, 48, 8)]})
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hwnd', type=lambda s: int(s, 0), required=True)
    p.add_argument('--module', required=True)
    p.add_argument('--base', type=lambda s: int(s, 0), required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--visible', action='store_true', help='temporarily restore, then restore placement')
    p.add_argument('--profile', help='read-only ABI field descriptor JSON; does not enable injection')
    a = p.parse_args()
    pe, process = PE(a.module), Process(a.hwnd)
    profile=json.load(open(a.profile,encoding='utf-8-sig')) if a.profile else {}
    def field(name,default):return profile.get(name,default)
    process.metadata_offset=field('metadata',0x100)
    process.vector_ends=field('vectorEnds',False)
    saved_placement = C.create_string_buffer(44)
    struct.pack_into('<I', saved_placement, 0, 44)
    process.u.GetWindowPlacement.argtypes = [W.HWND, C.c_void_p]
    process.u.SetWindowPlacement.argtypes = [W.HWND, C.c_void_p]
    process.u.ShowWindow.argtypes = [W.HWND, C.c_int]
    process.u.SetForegroundWindow.argtypes = [W.HWND]
    process.u.GetForegroundWindow.restype = W.HWND
    process.u.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, C.c_size_t, C.c_ssize_t, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    process.u.ClientToScreen.argtypes = [W.HWND, C.c_void_p]
    process.u.SetProcessDpiAwarenessContext.argtypes = [C.c_ssize_t]
    process.u.SetProcessDpiAwarenessContext(-4)
    foreground = process.u.GetForegroundWindow()
    if a.visible:
        if not process.u.GetWindowPlacement(a.hwnd, saved_placement):
            raise C.WinError(C.get_last_error())
        process.u.ShowWindow(a.hwnd, 9)
        import time
        time.sleep(0.8)
    try:
        base = a.base
        # Only the previously disassembled framework chain is read here.
        assert process.q(process.userdata + 0x40) == a.hwnd
        delegate = process.q(process.userdata + field('delegate',0x70))
        widget = process.q(delegate - field('hostAdjust',0x1a8) + field('widget',0x288))
        root = process.q(widget + 0x1a0)
        result = {'pid': process.pid.value, 'thread': process.thread,
                  'identity': pe.identity(), 'userdata': hex(process.userdata),
                  'delegate': hex(delegate), 'widget': hex(widget), 'root': hex(root),
                  'object': [], 'vtables': []}
        result['metadata'] = cached_metadata(process, pe, base, root)
        result['classes'] = {}
        seen = set()
        origin = W.POINT()
        process.u.ClientToScreen(a.hwnd, C.byref(origin))
        dpi = process.u.GetDpiForWindow(a.hwnd)
        def walk(view, depth=0, x=0, y=0, visible=True):
            if view in seen or depth > 24 or len(seen) > 3000:
                return None
            seen.add(view)
            meta = cached_metadata(process, pe, base, view)
            address = int(meta.get('address', '0'), 16)
            while address and hex(address) not in result['classes']:
                description = describe_metadata(process, pe, base, address)
                result['classes'][hex(address)] = description
                address = int(description['parent'], 16)
            children, count, capacity = struct.unpack('<3Q', process.read(view+field('children',0x1a0), 24))
            if field('vectorEnds',False):count,capacity=(count-children)//8,(capacity-children)//8
            if count > 3000 or capacity < count:
                raise ValueError('invalid children vector')
            node = {'address': hex(view), 'vtable': hex(process.q(view)-base),
                    'metadata': meta, 'parent': hex(process.q(view+field('parent',0x198))),
                    'bounds': struct.unpack('<4i', process.read(view+field('bounds',0x1c8), 16)),
                    'visible': bool(process.read(view+field('visible',0x1d8), 1)[0]),
                    'fields': process.read(view+0x1b8, 0x40).hex(), 'children': []}
            background = process.q(view+field('background',0x220))
            if background:
                bgvt = process.q(background)
                node['background'] = {'address': hex(background), 'vtable': hex(bgvt-base),
                                      'paint': hex(process.q(bgvt+0x20)-base)}
            bx, by, width, height = node['bounds']
            x, y = x+bx, y+by
            visible = visible and node['visible']
            node['rootOrigin'] = [x, y]
            if visible and width > 0 and height > 0 and 0 <= y < 65 and width < 160:
                sx, sy = origin.x + round((x+width/2)*dpi/96), origin.y + round((y+height/2)*dpi/96)
                hit = C.c_size_t()
                if process.u.SendMessageTimeoutW(a.hwnd, 0x84, 0, (sx & 65535) | ((sy & 65535) << 16), 2, 500, C.byref(hit)):
                    node['hitTest'] = hit.value
            for i in range(count):
                child = process.q(children+i*8)
                if process.q(child+field('parent',0x198)) != view:
                    raise ValueError('parent mismatch')
                node['children'].append(walk(child, depth+1, x, y, visible))
            return node
        result['tree'] = walk(root)
        for offset in range(0, 0x400, 8):
            value = process.q(root + offset)
            result['object'].append([hex(offset), hex(value)])
            if base < value < base + len(pe.image):
                functions = []
                for i in range(0, 0x400, 8):
                    f = process.q(value + i)
                    if not (base < f < base + len(pe.image)):
                        break
                    functions.append({'slot': hex(i), 'rva': hex(f-base),
                                      'code': pe.disassemble(f-base, 100)})
                result['vtables'].append({'offset': hex(offset), 'rva': hex(value-base), 'functions': functions})
        with open(a.out, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
        print(json.dumps({k:v for k,v in result.items() if k not in ('object', 'vtables', 'tree', 'classes', 'identity')}, indent=2))
        print('View count:', len(seen), 'class metadata count:', len(result['classes']))
        for x in result['classes'].values():
            print(x['name'], [(m['name'], m['type']) for m in x['members']])
        print('Object vtable candidates:', [(x['offset'], x['rva'], len(x['functions'])) for x in result['vtables']])
    finally:
        if a.visible:
            process.u.SetWindowPlacement(a.hwnd, saved_placement)
            process.u.SetForegroundWindow(foreground)
        process.close()
