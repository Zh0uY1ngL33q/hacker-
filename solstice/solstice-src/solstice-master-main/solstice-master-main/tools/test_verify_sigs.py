"""Small synthetic tests: no game process or large image required."""
import mmap
import tempfile
import unittest
from pathlib import Path

import verify_sigs as vs


class SignatureVerifierTests(unittest.TestCase):
    def test_scan_respects_absolute_span_and_overlapping_hits(self):
        self.assertEqual(vs.scan(b'\xaa\xbb\xaa\xbb\xaa\xbb', 2, 6,
                                 vs.parse_pattern('AA BB')), [2, 4])
        self.assertEqual(vs.scan(b'AAAA', 0, 4, vs.parse_pattern('41 41')), [0, 1, 2])

    def test_mmap_scan_and_regex_fallback(self):
        with tempfile.TemporaryFile() as f:
            f.write(b'\x10\x11\x22\x10\x33\x22')
            f.flush()
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
                self.assertEqual(vs.scan(data, 0, 6, vs.parse_pattern('10 ? 22')), [0, 3])

    def test_global_match_limit(self):
        self.assertEqual(vs.scan_spans(b'ABABABAB', [(0, 4), (4, 8)],
                                      vs.parse_pattern('41 42'), limit=3), [0, 2, 4])

    def test_typed_reads_do_not_consume_following_opcodes(self):
        d = {'read_type': 'uint8_t', 'offset_type': 'FieldOffset'}
        self.assertEqual(vs.read_offset(b'\x08\x48\x85\xc9', 0, d), 8)
        d['offset_type'] = 'Index'
        self.assertEqual(vs.read_offset(b'\x30\x12\x00\x00', 0, d), 6)
        d['read_type'] = 'int'
        self.assertEqual(vs.read_offset(b'\x30\x12\x00\x00', 0, d), 582)

    def test_signed_index_division_matches_cpp(self):
        self.assertEqual(vs.read_offset((-9).to_bytes(4, 'little', signed=True), 0,
                         {'read_type': 'int', 'offset_type': 'Index'}), -1)

    def test_comments_and_typed_definition_parsing(self):
        with tempfile.TemporaryDirectory() as temp:
            sdk = Path(temp) / 'src' / 'SDK'
            sdk.mkdir(parents=True)
            (sdk / 'OffsetProvider.hpp').write_text('''
// DEFINE_INDEX_FIELD(Disabled, "AA BB", 0, OffsetType::Index);
DEFINE_INDEX_FIELD_TYPED(uint8_t, ByteField, "48 8B 4B ?", 3, OffsetType::FieldOffset);
/* DEFINE_INDEX_FIELD(AlsoDisabled, "CC DD", 0, OffsetType::Index); */
DEFINE_INDEX_FIELD(FullField, "48 8B 80 ? ? ? ?", 3, OffsetType::Index);
''')
            (sdk / 'SigManager.hpp').write_text('''
// DEFINE_SIG(DisabledSig, "AA BB", SigType::Sig, 0);
DEFINE_SIG(ActiveSig, "E8 ? ? ? ?", SigType::RefSig, 1);
''')
            defs, _ = vs.load_definitions(temp)
            self.assertEqual({d['name'] for d in defs}, {'ByteField', 'FullField', 'ActiveSig'})
            self.assertEqual(next(d['read_type'] for d in defs if d['name'] == 'ByteField'), 'uint8_t')

    def test_capstone_operand_audit(self):
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        from audit_operand_reads import audit
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        md.detail = True
        field = {'kind': 'offset', 'name': 'field', 'offset_type': 'FieldOffset',
                 'read_type': 'uint8_t', 'disp_off': 3, 'match_rvas': ['0x0']}
        self.assertIsNone(audit(bytes.fromhex('48 8B 4B 08 48 85 C9'), field, md))
        field['read_type'] = 'int'
        self.assertIn('width', audit(bytes.fromhex('48 8B 4B 08 48 85 C9'), field, md)['issues'][0]['issue'])
        ref = {'kind': 'sig', 'name': 'ref', 'sig_type': 'RefSig',
               'disp_off': 1, 'match_rvas': ['0x0']}
        self.assertIsNone(audit(bytes.fromhex('E8 10 00 00 00'), ref, md))
        self.assertIsNotNone(audit(bytes.fromhex('48 8B C4 48 89 58 08'), ref, md))


if __name__ == '__main__':
    unittest.main()
