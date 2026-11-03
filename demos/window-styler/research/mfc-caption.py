"""Read caption command fields and framework virtual methods, without execution."""
import argparse
import ctypes as C
import json
import pathlib
import runpy
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / 'out' / 'research-deps'))
import capstone

mfc = runpy.run_path(str(ROOT / 'mfc-objects.py'))
Reader, Region, describe = mfc['Reader'], mfc['MemoryRegion'], mfc['describe']
identity = runpy.run_path(str(ROOT / 'office-metadata.py'))['identity']
COMMANDS = {0xF020: 'SC_MINIMIZE', 0xF030: 'SC_MAXIMIZE', 0xF060: 'SC_CLOSE', 0xF120: 'SC_RESTORE'}


def inspect(hwnd, previous):
    reader = Reader(hwnd)
    try:
        if reader.pid != previous['pid']:
            raise ValueError('The previous discovery belongs to another process')
        records, methods = [], {}
        kernel = C.WinDLL('kernel32')
        kernel.VirtualQueryEx.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(Region), C.c_size_t]
        kernel.VirtualQueryEx.restype = C.c_size_t
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        decoder.detail = True
        for original in previous['frameworkObjects']:
            if original['classes'][0]['name'] != 'CBCGPRibbonCaptionButton':
                continue
            pointer = int(original['address'], 16)
            record = describe(reader, pointer, required=None)
            if not record or record['classes'][0]['name'] != 'CBCGPRibbonCaptionButton':
                continue
            module = next(m for m in reader.modules if m['name'] == record['module'])
            if identity(reader, module) != ('73D590B1-7FA3-4F9D-9CE2-E2A6D801C11C', 1):
                raise ValueError('Unknown BCG static-library image identity')
            size = min(record['classes'][0]['objectSize'], 4096)
            raw = reader.read(pointer, size)
            record['systemCommands'] = []
            record['smallRectCandidates'] = []
            for offset in range(0, size - 3, 4):
                value = struct.unpack_from('<I', raw, offset)[0]
                if value in COMMANDS:
                    record['systemCommands'].append({'field': hex(offset), 'command': COMMANDS[value], 'value': value})
                if offset + 16 <= size:
                    left, top, right, bottom = struct.unpack_from('<4i', raw, offset)
                    if 0 <= left < right <= 5000 and 0 <= top < bottom <= 150 and \
                            8 <= right - left <= 150 and 8 <= bottom - top <= 100:
                        record['smallRectCandidates'].append({'field': hex(offset), 'rect': [left, top, right, bottom]})
            records.append(record)
            table = reader.integer(pointer)
            for slot in range(160):
                function = reader.integer(table + slot * 8)
                region = Region()
                if not kernel.VirtualQueryEx(reader.h, function, C.byref(region), C.sizeof(region)) or \
                        not region.protect & 0xf0:
                    break
                if function in methods:
                    continue
                code = reader.read(function, min(768, region.base + region.size - function))
                instructions = list(decoder.disasm(code, function))
                # Capture potential WM_SYSCOMMAND / WM_CLOSE / WM_NCHITTEST
                # dispatch sites. A numeric match alone does not authorize use.
                messages = []
                for ins in instructions:
                    for operand in ins.operands:
                        if operand.type == capstone.x86.X86_OP_IMM and operand.imm in (0x112, 0x10, 0x84):
                            messages.append({'instructionRva': hex(ins.address - module['base']),
                                             'value': operand.imm})
                if messages:
                    methods[function] = {'slot': slot, 'functionRva': hex(function - module['base']),
                                         'messages': messages, 'entry': code[:32].hex(),
                                         'assembly': ['%08x %-8s %s' % (i.address - module['base'], i.mnemonic, i.op_str)
                                                      for i in instructions]}
        elements = []
        for original in previous['frameworkObjects']:
            if not any(c['name'] in ('CBCGPBaseRibbonElement', 'CBCGPRibbonCategory') for c in original['classes']):
                continue
            pointer = int(original['address'], 16)
            current = describe(reader, pointer, required=None)
            if not current:
                continue
            current['rectAtEc'] = list(struct.unpack('<4i', reader.read(pointer + 0xec, 16)))
            current['frameworkReferences'] = original['references']
            elements.append(current)
        return {'pid': reader.pid, 'targetCodeExecuted': False, 'layoutModified': False,
                'captionObjects': records, 'ribbonElements': elements, 'candidateMethods': list(methods.values())}
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hwnd', type=lambda text: int(text, 0), required=True)
    parser.add_argument('--previous', type=pathlib.Path, required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    data = inspect(args.hwnd, json.loads(args.previous.read_text(encoding='utf-8-sig')))
    args.out.write_text(json.dumps(data, indent=2), encoding='utf-8')
    print(json.dumps({'captionObjects': len(data['captionObjects']), 'candidateMethods': len(data['candidateMethods'])}))
