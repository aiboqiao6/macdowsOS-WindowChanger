"""Read-only PE identity/disassembly helper for validating framework ABIs."""
import argparse
import json
import pathlib
import struct
import uuid


class PE:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.raw = self.path.read_bytes()
        nt = struct.unpack_from('<I', self.raw, 0x3c)[0]
        if self.raw[:2] != b'MZ' or self.raw[nt:nt+4] != b'PE\0\0':
            raise ValueError('Not a PE image')
        count, optional = struct.unpack_from('<H', self.raw, nt+6)[0], struct.unpack_from('<H', self.raw, nt+20)[0]
        opt = nt+24
        self.is64 = struct.unpack_from('<H', self.raw, opt)[0] == 0x20b
        self.image = bytearray(struct.unpack_from('<I', self.raw, opt+56)[0])
        self.image[:4096] = self.raw[:4096]
        self.sections = []
        for i in range(count):
            s = opt+optional+i*40
            name = self.raw[s:s+8].rstrip(b'\0').decode('ascii')
            vs, rva, size, ptr = struct.unpack_from('<4I', self.raw, s+8)
            self.image[rva:rva+size] = self.raw[ptr:ptr+size]
            self.sections.append((name, rva, vs, size, ptr))
        self.directories = [struct.unpack_from('<II', self.raw, opt+(112 if self.is64 else 96)+8*i) for i in range(16)]

    def identity(self):
        addr, size = self.directories[6]
        result = dict(path=str(self.path), sections=self.sections)
        for i in range(addr, addr+size, 28):
            _, _, _, _, kind, length, rva, _ = struct.unpack_from('<IIHHIIII', self.image, i)
            cv = self.image[rva:rva+length]
            if kind == 2 and cv[:4] == b'RSDS':
                guid = str(uuid.UUID(bytes_le=bytes(cv[4:20]))).upper()
                age = struct.unpack_from('<I', cv, 20)[0]
                result.update(guid=guid, age=age, key=guid.replace('-', '')+f'{age:X}', pdb=cv[24:].split(b'\0')[0].decode())
        return result

    def disassemble(self, rva, size):
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if self.is64 else capstone.CS_MODE_32)
        return '\n'.join(f'{x.address:08x} {x.mnemonic:8} {x.op_str}' for x in md.disasm(self.image[rva:rva+size], rva))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('path')
    p.add_argument('--disasm', nargs=2, metavar=('RVA', 'SIZE'))
    a = p.parse_args()
    pe = PE(a.path)
    print(pe.disassemble(*[int(x, 0) for x in a.disasm]) if a.disasm else json.dumps(pe.identity(), indent=2))
