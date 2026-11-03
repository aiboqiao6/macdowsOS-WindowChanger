"""Read-only provenance of caption click callbacks; never a layout/role selector.

Select sources by GF's actual hit results and canonical identities, not names.
The optional native event-bridge decoder is exact-CodeView guarded; unknown
bridges stay opaque. Host callbacks are reported, never called or installed.
"""
import argparse
import json
import pathlib
import runpy
import struct
import uuid

from pe_inspect import PE

Reader = runpy.run_path(str(pathlib.Path(__file__).with_name('read-framework-objects.py')))['Reader']
BRIDGE = ('048FD36D-5C4B-4D0F-AD4D-3AE453E2798D', 1)


def reachable_code(pe, entry, span=2048, budget=512):
    """Read reachable basic blocks only; never spill past a return into a neighbour."""
    import capstone
    from capstone.x86 import X86_OP_IMM
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    pending, seen, complete = [entry], {}, True
    while pending:
        cursor = pending.pop()
        while cursor not in seen:
            if not entry <= cursor < entry + span or len(seen) >= budget:
                complete = False
                break
            instruction = next(decoder.disasm(pe.image[cursor:cursor + 15], cursor, 1), None)
            if not instruction:
                complete = False
                break
            seen[cursor] = f'{cursor:08x} {instruction.mnemonic:8} {instruction.op_str}'
            if instruction.mnemonic.startswith('ret') or instruction.mnemonic in ('int3', 'ud2'):
                break
            if instruction.mnemonic.startswith('j'):
                if instruction.operands and instruction.operands[0].type == X86_OP_IMM:
                    destination = instruction.operands[0].imm
                    if entry <= destination < entry + span:
                        pending.append(destination)
                    else:
                        complete = False
                else:
                    complete = False
                if instruction.mnemonic == 'jmp':
                    break
            cursor += instruction.size
    return '\n'.join(seen[key] for key in sorted(seen)), complete, len(seen)


class Inspector:
    def __init__(self, reader):
        self.reader = reader
        self.images = {}

    def u32(self, address):
        return struct.unpack('<I', self.reader.read(address, 4))[0]

    def image(self, address):
        module = self.reader.module(address)
        if not module:
            raise ValueError('Callback address is outside loaded images')
        key = module['base']
        if key not in self.images:
            pe = PE(module['path'])
            identity = pe.identity()
            # Validate the live PE32 CodeView against the immutable disk image.
            nt = self.u32(key + 0x3c)
            header = self.reader.read(key + nt, 248)
            if header[:4] != b'PE\0\0' or struct.unpack_from('<H', header, 24)[0] != 0x10b:
                raise ValueError('The callback profile requires PE32')
            rva, size = struct.unpack_from('<II', header, 24 + 96 + 6 * 8)
            if size > 4096 or rva + size > module['size']:
                raise ValueError('Invalid CodeView directory')
            loaded = None
            for offset in range(0, size - 27, 28):
                entry = self.reader.read(key + rva + offset, 28)
                kind, length, value = struct.unpack_from('<III', entry, 12)
                if kind == 2 and length >= 24 and value + 24 <= module['size']:
                    cv = self.reader.read(key + value, 24)
                    if cv[:4] == b'RSDS':
                        loaded = str(uuid.UUID(bytes_le=cv[4:20])).upper(), struct.unpack_from('<I', cv, 20)[0]
            if loaded != (identity.get('guid'), identity.get('age')):
                raise ValueError('Live/disk callback CodeView mismatch')
            self.images[key] = module, pe, identity
        return self.images[key]

    def describe(self, address, code_size=0):
        module, pe, identity = self.image(address)
        result = dict(address=hex(address), module=module['name'], rva=hex(address - module['base']),
                      codeView=identity['key'])
        if code_size:
            code, complete, count = reachable_code(pe, address - module['base'])
            result.update(disassembly=code, boundedTraversalComplete=complete, reachableInstructions=count)
            nt = struct.unpack_from('<I', pe.raw, 0x3c)[0]
            preferred = struct.unpack_from('<I', pe.raw, nt + 24 + 28)[0]
            imports = []
            directory, size = pe.directories[1]
            for descriptor in range(directory, directory + min(size, 65536), 20):
                original, _, _, name, iat = struct.unpack_from('<5I', pe.image, descriptor)
                if not name:
                    break
                dll = pe.image[name:name + 256].split(b'\0')[0].decode('ascii', errors='replace')
                for index in range(16384):
                    value = struct.unpack_from('<I', pe.image, (original or iat) + index * 4)[0]
                    if not value:
                        break
                    if f'[0x{preferred + iat + index * 4:x}]' in result['disassembly']:
                        symbol = (f'ordinal:{value & 0xffff}' if value & 0x80000000 else
                                  pe.image[value + 2:value + 258].split(b'\0')[0].decode('ascii', errors='replace'))
                        imports.append(dll + '!' + symbol)
            result['referencedImports'] = imports
        return result

    def handlers(self, context, offset):
        head, count = self.u32(context + offset), self.u32(context + offset + 4)
        if count > 128 or self.reader.read(head + 13, 1) != b'\x01':
            raise ValueError('Unverified callback-map bound/sentinel')
        seen, result = set(), []

        def walk(node, parent, depth=0):
            if node == head:
                return
            if not node or depth > 32 or len(seen) >= count or node in seen:
                raise ValueError('Callback-map cycle or budget exceeded')
            seen.add(node)
            left, backlink, right = struct.unpack('<3I', self.reader.read(node, 12))
            if backlink != parent or self.reader.read(node + 13, 1) != b'\0':
                raise ValueError('Callback-map backlink/sentinel mismatch')
            walk(left, node, depth + 1)
            source, event, callback = struct.unpack('<3I', self.reader.read(node + 16, 12))
            result.append((source, event, callback))
            walk(right, node, depth + 1)

        walk(self.u32(head + 4), head)
        if len(seen) != count or [(e, s) for s, e, _ in result] != sorted((e, s) for s, e, _ in result):
            raise ValueError('Callback-map size/order mismatch')
        return result

    def trace(self, source, binding):
        sink, callback = binding['sink'], binding['numericCallback']
        if self.u32(sink) != binding['vtable'] or self.u32(binding['vtable'] + 28) != callback:
            raise ValueError('The original click subscription is no longer current')
        result = dict(source=source, eventId=binding['id'], sink=hex(sink), dispatcher=self.describe(callback),
                      bridgeDecoded=False, roleAssigned=False)
        module, pe, identity = self.image(callback)
        if (identity['guid'], identity['age']) != BRIDGE or callback - module['base'] != 0x19289:
            return result
        base = module['base']
        if self.reader.read(callback, 7) != bytes.fromhex('558bec518b4508'):
            raise ValueError('Event-bridge entry differs from the verified implementation')
        delegate = self.u32(sink + 20)
        method = self.u32(self.u32(delegate) + 4)
        if method != base + 0x8f80 or self.reader.read(method, 12) != bytes.fromhex('558bec8bc18b48045dff6008'):
            raise ValueError('Delegate is not the verified native member-call thunk')
        context, forward = self.u32(delegate + 4), self.u32(delegate + 8)
        if forward != base + 0x15cc6 or self.reader.read(forward, 4) != bytes.fromhex('8b01ff20'):
            raise ValueError('Delegate forwarding ABI differs')
        handler = self.u32(self.u32(context))
        if handler != base + 0x57d5:
            raise ValueError('Unknown event-router implementation')
        # These two maps are keyed by original source interface identity +
        # numeric event. They are NOT business-control names or inferred roles.
        for offset in (0x1c, 0x14):
            matches = [fn for src, event, fn in self.handlers(context, offset)
                       if src == source and event == binding['id']]
            if matches:
                if len(matches) != 1:
                    raise ValueError('Ambiguous original click handler')
                result.update(bridgeDecoded=True, router=self.describe(handler),
                              destination=hex(self.u32(context + 0x2c)), handler=self.describe(matches[0], 0x130))
                break
        return result


def inspect(hwnd, report):
    reader = Reader(hwnd)
    try:
        if reader.ptr != 4 or reader.pid != report['pid'] or report.get('layoutModified') is not False:
            raise ValueError('Report does not belong to this x86 read-only target')
        nodes = {}

        def walk(node):
            if node:
                nodes[node['identity']] = node
                for child in node.get('children', []):
                    walk(child)

        walk(report['tree'])
        sources = list(dict.fromkeys(s['identity'] for s in report['captionHitSamples'][:7]
                                     if s.get('frameworkType') == 'IGFButton'))
        if len(sources) != 3:
            raise ValueError('Expected three distinct original top-row hit identities')
        inspector, results = Inspector(reader), []
        for source in sources:
            events = nodes[source]['eventBindings']
            if not (events.get('profileVerified') and events.get('complete') and events.get('stableRead')):
                raise ValueError('Original event-container validation is incomplete')
            clicks = [b for b in events['numeric'] if b['id'] == 0x1003]
            if len(clicks) != 1:
                raise ValueError('Caption source has ambiguous original click subscriptions')
            results.append(inspector.trace(source, clicks[0]))
        return dict(pid=reader.pid, targetCodeExecuted=False, layoutModified=False,
                    roleSelectionImplemented=False, callbacks=results)
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hwnd', type=lambda v: int(v, 0), required=True)
    parser.add_argument('--report', type=pathlib.Path, required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = inspect(args.hwnd, json.loads(args.report.read_text(encoding='utf-8-sig')))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(callbacks=len(result['callbacks']), decoded=sum(c['bridgeDecoded'] for c in result['callbacks']),
                          targetCodeExecuted=False, roleSelectionImplemented=False)))
