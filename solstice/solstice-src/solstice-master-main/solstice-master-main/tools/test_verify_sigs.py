"""Small synthetic tests: no game process or large image required."""
import mmap
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

import verify_sigs as vs


def memory_image(payload, sections=None, size_of_image=None):
    """Build an Image-shaped object for verdict tests (RVA 0 stays reserved)."""
    img = vs.Image.__new__(vs.Image)
    img.data = bytes(payload)
    img.size_of_image = size_of_image if size_of_image is not None else len(payload)
    img.sections = sections if sections is not None else [{
        'rva': 1, 'vsize': max(0, len(payload) - 1),
        'raw_size': max(0, len(payload) - 1),
        'characteristics': 0x20000000, 'name': 'text', 'index': 0,
    }]
    return img


def write_synthetic_pe(path, sections, size_of_image, file_size=None):
    """Write a minimal PE32+ virtual image (file offset == RVA)."""
    e_lfanew = 0x80
    opt_size = 0xF0
    sec_off = e_lfanew + 24 + opt_size
    minimum = sec_off + 40 * len(sections)
    if file_size is None:
        file_size = max(minimum, min(size_of_image, 0x20000))
    buf = bytearray(max(minimum, file_size))
    buf[:2] = b'MZ'
    struct.pack_into('<I', buf, 0x3C, e_lfanew)
    buf[e_lfanew:e_lfanew + 4] = b'PE\0\0'
    coff = e_lfanew + 4
    struct.pack_into('<H', buf, coff, 0x8664)
    struct.pack_into('<H', buf, coff + 2, len(sections))
    struct.pack_into('<H', buf, coff + 16, opt_size)
    opt = coff + 20
    struct.pack_into('<H', buf, opt, 0x20B)
    struct.pack_into('<Q', buf, opt + 24, 0x140000000)
    struct.pack_into('<I', buf, opt + 56, size_of_image)
    for i, sec in enumerate(sections):
        off = sec_off + i * 40
        name = sec.get('name', ('s%d' % i)).encode('ascii')[:8]
        buf[off:off + len(name)] = name
        struct.pack_into('<III', buf, off + 8,
                         sec['vsize'], sec['rva'], sec.get('raw_size', sec['vsize']))
        struct.pack_into('<I', buf, off + 36, sec.get('characteristics', 0x20000000))
    path.write_bytes(buf)


def sig_def(pattern, sig_type='Sig', disp_off=0, name='sig'):
    return {'kind': 'sig', 'name': name, 'pattern': pattern,
            'sig_type': sig_type, 'disp_off': disp_off}


def off_def(pattern, read_type='int', offset_type='FieldOffset', disp_off=0,
            name='off'):
    return {'kind': 'offset', 'name': name, 'pattern': pattern,
            'read_type': read_type, 'offset_type': offset_type,
            'disp_off': disp_off}


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
        # -16 is an 8-byte-aligned negative displacement: C++ integer division
        # truncates towards zero, giving -2 rather than -3.
        self.assertEqual(vs.read_offset((-16).to_bytes(4, 'little', signed=True), 0,
                         {'read_type': 'int', 'offset_type': 'Index'}), -2)

    def test_unaligned_vtable_displacement_is_refused(self):
        # The runtime refuses a displacement that is not a multiple of 8; the
        # offline scanner must not turn it into a plausible-looking slot.
        with self.assertRaises(ValueError):
            vs.read_offset((9).to_bytes(4, 'little', signed=True), 0,
                           {'read_type': 'int', 'offset_type': 'Index'})
        with self.assertRaises(ValueError):
            vs.read_offset((-9).to_bytes(4, 'little', signed=True), 0,
                           {'read_type': 'int', 'offset_type': 'Index'})

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


    def test_offset_plausibility_matches_cpp(self):
        # Must agree with OffsetProvider::plausibleOffset() in every case,
        # especially the ones the old abs(val) > 0x20000 rule let through.
        for v in (0, 8, 0x100, 0x7FFF):
            self.assertTrue(vs.cpp_plausible_offset(v, False)[0], v)
        for v in (-8, 1, 7, 0x8000, 0x9000, 0x20000):
            self.assertFalse(vs.cpp_plausible_offset(v, False)[0], v)
        for v in (1, 8, 436, 4095):
            self.assertTrue(vs.cpp_plausible_offset(v, True)[0], v)
        for v in (-1, 0, 4096, 17305961):
            self.assertFalse(vs.cpp_plausible_offset(v, True)[0], v)

    def test_exec_spans_clips_and_sorts_like_cpp(self):
        img = vs.Image.__new__(vs.Image)
        img.data = b'\0' * 0x1000
        img.size_of_image = 0xA00
        img.sections = [
            {'rva': 0x100, 'vsize': 0x900, 'characteristics': 0x20000000, 'name': 'big'},
            {'rva': 0x900, 'vsize': 0x200, 'characteristics': 0x20000000, 'name': 'tail'},
            {'rva': 0x0,   'vsize': 0x100, 'characteristics': 0x20000000, 'name': 'zero'},
            {'rva': 0xB00, 'vsize': 0x100, 'characteristics': 0x20000000, 'name': 'past'},
            {'rva': 0x300, 'vsize': 0x100, 'characteristics': 0, 'name': 'data'},
        ]
        # "big" clips to 0x900 (0xA00-0x100), "tail" clips to 0x100
        # (0xA00-0x900). Sort key must be the CLIPPED size.
        self.assertEqual(img.exec_spans(), [(0x100, 0xA00), (0x900, 0xA00)])
        self.assertEqual(img.code_span(), (0x100, 0xA00, 'big'))

    def test_exec_spans_empty_when_no_executable_range(self):
        img = vs.Image.__new__(vs.Image)
        img.data = b'\0' * 0x100
        img.size_of_image = 0x100
        img.sections = [{'rva': 0x40, 'vsize': 0x10,
                         'characteristics': 0, 'name': 'data'}]
        self.assertEqual(img.exec_spans(), [])
        self.assertEqual(img.code_span(), (0, 0, '<no executable range>'))

    def test_mirrored_scanner_uses_the_same_rules(self):
        # tools/scan_sigs.py is the byte-for-byte mirror of tools/re/scan_sigs.py
        # that ships in the source ZIP. A future edit that changes only one copy
        # must fail here instead of silently producing a different verdict.
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            'scan_sigs_mirror', Path(__file__).with_name('scan_sigs.py'))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for v in (0, 8, 0x100, 0x7FFF, -8, 1, 7, 0x8000, 0x9000):
            self.assertEqual(vs.cpp_plausible_offset(v, False)[0],
                             mod.cpp_plausible_offset(v, False)[0], v)
        for v in (1, 436, 4095, 0, 4096, -1):
            self.assertEqual(vs.cpp_plausible_offset(v, True)[0],
                             mod.cpp_plausible_offset(v, True)[0], v)

        mimg = mod.Image.__new__(mod.Image)
        mimg.buf = b'\0' * 0x1000
        mimg.size_of_image = 0xA00
        mimg.sections = [
            {'rva': 0x100, 'vsize': 0x900, 'exec': True},
            {'rva': 0x900, 'vsize': 0x200, 'exec': True},
            {'rva': 0x0,   'vsize': 0x100, 'exec': True},
            {'rva': 0xB00, 'vsize': 0x100, 'exec': True},
            {'rva': 0x300, 'vsize': 0x100, 'exec': False},
        ]
        self.assertEqual(list(mimg.exec_ranges()),
                         [(0x100, 0xA00), (0x900, 0xA00)])


class FinalVerdictMatrixTests(unittest.TestCase):
    def test_signature_zero_one_many_and_reasons(self):
        missing = vs.evaluate_definition(memory_image(b'\0\x10\x20\x30'),
                                         sig_def('AA BB'))
        self.assertEqual((missing['status'], missing['matches']), ('MISSING', 0))
        self.assertIn('did not match', missing['rejectionReason'])

        unique = vs.evaluate_definition(memory_image(b'\0\xAA\xBB\x30'),
                                        sig_def('AA BB'))
        self.assertEqual((unique['status'], unique['matches']), ('OK', 1))
        self.assertEqual(unique['rejectionReason'], '')

        ambiguous = vs.evaluate_definition(memory_image(b'\0\xAA\xBB\xAA\xBB'),
                                           sig_def('AA BB'))
        self.assertEqual((ambiguous['status'], ambiguous['matches']), ('AMBIGUOUS', 2))
        self.assertIn('2 executable locations', ambiguous['rejectionReason'])
        self.assertNotIn('resolved_rva', ambiguous)

    def test_overlap_with_wildcard_matches_libhat_x1(self):
        # This takes the regex fallback (only one consecutive concrete byte).
        pat = vs.parse_pattern('41 ?')
        self.assertEqual(vs.scan(b'AAAA', 0, 4, pat), [0, 1, 2])

    def test_match_crosses_64k_scan_boundary(self):
        data = bytearray(0x10010)
        data[0xFFFF:0x10003] = b'ABCD'
        self.assertEqual(vs.scan(data, 1, len(data), vs.parse_pattern('41 42 43 44')),
                         [0xFFFF])

    def test_hits_in_distinct_executable_sections_are_ambiguous(self):
        data = bytearray(0x80)
        data[0x10:0x12] = b'AB'
        data[0x50:0x52] = b'AB'
        sections = [
            {'rva': 0x10, 'vsize': 0x10, 'raw_size': 0x10,
             'characteristics': 0x20000000, 'name': 'a', 'index': 0},
            {'rva': 0x50, 'vsize': 0x10, 'raw_size': 0x10,
             'characteristics': 0x20000000, 'name': 'b', 'index': 1},
        ]
        result = vs.evaluate_definition(memory_image(data, sections), sig_def('41 42'))
        self.assertEqual((result['status'], result['matches']), ('AMBIGUOUS', 2))
        self.assertEqual(result['match_rvas'], ['0x10', '0x50'])

    def test_pattern_split_across_sections_does_not_bridge_ranges(self):
        img = memory_image(b'\0\xAA\xBB\0', sections=[
            {'rva': 1, 'vsize': 1, 'raw_size': 1,
             'characteristics': 0x20000000, 'name': 'a', 'index': 0},
            {'rva': 2, 'vsize': 1, 'raw_size': 1,
             'characteristics': 0x20000000, 'name': 'b', 'index': 1},
        ])
        result = vs.evaluate_definition(img, sig_def('AA BB'))
        self.assertEqual((result['status'], result['matches']), ('MISSING', 0))
        self.assertIn('did not match', result['rejectionReason'])

    def test_empty_executable_range_is_missing(self):
        img = memory_image(b'\0AB', sections=[])
        result = vs.evaluate_definition(img, sig_def('41 42'))
        self.assertEqual((result['status'], result['matches']), ('MISSING', 0))
        self.assertIn('executable range', result['rejectionReason'])

    def test_ambiguous_offset_is_rejected_before_operand_read(self):
        # disp_off is deliberately outside the image. If the scanner decodes the
        # arbitrary first hit, AMBIGUOUS is overwritten by OUT_OF_RANGE.
        img = memory_image(b'\0\xAA\xAA')
        result = vs.evaluate_definition(img, off_def('AA', disp_off=99))
        self.assertEqual((result['status'], result['matches']), ('AMBIGUOUS', 2))
        self.assertNotIn('field_offset', result)

    def test_typed_reads_all_widths_and_signs(self):
        cases = [
            ('int8_t', 127, b'\x7f'), ('int8_t', -2, b'\xfe'),
            ('int32_t', 0x1234, (0x1234).to_bytes(4, 'little', signed=True)),
            ('int32_t', -17, (-17).to_bytes(4, 'little', signed=True)),
            ('int64_t', 0x2345, (0x2345).to_bytes(8, 'little', signed=True)),
            ('int64_t', -0x123, (-0x123).to_bytes(8, 'little', signed=True)),
        ]
        for ctype, expected, raw in cases:
            with self.subTest(ctype=ctype, expected=expected):
                result = vs.evaluate_definition(
                    memory_image(b'\0\xAA' + raw),
                    off_def('AA', read_type=ctype, disp_off=1))
                expected_status = 'OK' if expected >= 8 else 'IMPLAUSIBLE'
                self.assertEqual((result['status'], result['matches']),
                                 (expected_status, 1))
                self.assertEqual(result['field_offset'], expected)
                self.assertEqual(bool(result['rejectionReason']), expected_status != 'OK')

    def test_out_of_range_and_unknown_typed_read_reasons(self):
        with self.assertRaisesRegex(ValueError, 'outside image'):
            vs.read_offset(b'\x01\x02\x03', 0,
                           {'read_type': 'int32_t', 'offset_type': 'FieldOffset'})
        with self.assertRaisesRegex(ValueError, 'unsupported typed offset'):
            vs.read_offset(b'\0' * 8, 0,
                           {'read_type': 'float', 'offset_type': 'FieldOffset'})

        unknown = vs.evaluate_definition(memory_image(b'\0\xAA\0\0\0\0'),
                                         off_def('AA', read_type='float', disp_off=1))
        self.assertEqual((unknown['status'], unknown['matches']), ('INCONSISTENT', 1))
        self.assertIn('unsupported typed offset', unknown['rejectionReason'])

        truncated = vs.evaluate_definition(memory_image(b'\0\xAA\x01'),
                                           off_def('AA', read_type='int32_t', disp_off=1))
        self.assertEqual((truncated['status'], truncated['matches']), ('OUT_OF_RANGE', 1))
        self.assertIn('outside image', truncated['rejectionReason'])

    def test_incomplete_rel32_is_not_false_ok(self):
        img = memory_image(b'\0\0\0\xE8')
        result = vs.evaluate_definition(img, sig_def('E8', 'RefSig', 1))
        self.assertEqual((result['status'], result['matches']), ('OUT_OF_RANGE', 1))
        self.assertIn('rel32 operand', result['rejectionReason'])

    def test_vtable_alignment_index_zero_and_vtable_field_guard(self):
        unaligned = vs.evaluate_definition(
            memory_image(b'\0\xAA\x09\x00\x00\x00'),
            off_def('AA', 'int32_t', 'Index', 1))
        self.assertEqual(unaligned['status'], 'IMPLAUSIBLE')
        self.assertIn('not 8-byte aligned', unaligned['rejectionReason'])

        zero = vs.evaluate_definition(
            memory_image(b'\0\xAA\x00\x00\x00\x00'),
            off_def('AA', 'int32_t', 'Index', 1))
        self.assertEqual(zero['status'], 'IMPLAUSIBLE')
        self.assertIn('index 0', zero['rejectionReason'])

        for value in (1, 7):
            bad = vs.evaluate_definition(
                memory_image(b'\0\xAA' + value.to_bytes(4, 'little')),
                off_def('AA', 'int32_t', 'FieldOffset', 1))
            self.assertEqual(bad['status'], 'IMPLAUSIBLE')
            self.assertIn('vtable pointer', bad['rejectionReason'])
        good = vs.evaluate_definition(
            memory_image(b'\0\xAA\x08\x00\x00\x00'),
            off_def('AA', 'int32_t', 'FieldOffset', 1))
        self.assertEqual(good['status'], 'OK')


class PeRangeMatrixTests(unittest.TestCase):
    def _open(self, sections, size_of_image=0x6000, file_size=None):
        td = tempfile.TemporaryDirectory()
        path = Path(td.name) / 'test.image'
        write_synthetic_pe(path, sections, size_of_image, file_size)
        image = vs.Image(str(path))
        self.addCleanup(td.cleanup)
        # addCleanup() is LIFO: close the mmap before removing its file on Windows.
        self.addCleanup(image.data.close)
        return image

    def test_virtual_size_smaller_and_larger_than_raw_size(self):
        img = self._open([
            {'name': 'small', 'rva': 0x1000, 'vsize': 0x100, 'raw_size': 0x900},
            {'name': 'large', 'rva': 0x2000, 'vsize': 0x800, 'raw_size': 0x20},
        ])
        self.assertEqual(img.exec_spans(), [(0x2000, 0x2800), (0x1000, 0x1100)])

    def test_zero_and_past_size_of_image_rvas_are_rejected(self):
        img = self._open([
            {'name': 'zero', 'rva': 0, 'vsize': 0x400},
            {'name': 'past', 'rva': 0x7000, 'vsize': 0x100},
            {'name': 'ok', 'rva': 0x1000, 'vsize': 0x200},
        ], size_of_image=0x6000, file_size=0x2000)
        self.assertEqual(img.exec_spans(), [(0x1000, 0x1200)])

    def test_truncated_file_does_not_change_runtime_section_sort(self):
        img = self._open([
            # Runtime size 0x3000, only 0x1000 bytes present in the dump.
            {'name': 'runtime_big', 'rva': 0x1000, 'vsize': 0x3000},
            # Runtime size 0x1000 and all bytes present.
            {'name': 'runtime_small', 'rva': 0x800, 'vsize': 0x1000},
        ], size_of_image=0x5000, file_size=0x2000)
        self.assertEqual(img.exec_spans(), [(0x1000, 0x2000), (0x800, 0x1800)])

    def test_equal_sizes_keep_pe_order(self):
        img = self._open([
            {'name': 'first', 'rva': 0x2000, 'vsize': 0x400},
            {'name': 'second', 'rva': 0x1000, 'vsize': 0x400},
        ], file_size=0x3000)
        self.assertEqual(img.exec_spans(), [(0x2000, 0x2400), (0x1000, 0x1400)])

    def test_no_executable_section_never_falls_back_to_whole_image(self):
        img = self._open([
            {'name': 'data', 'rva': 0x1000, 'vsize': 0x400,
             'characteristics': 0x40000040},
        ], file_size=0x2000)
        self.assertEqual(img.exec_spans(), [])
        result = vs.evaluate_definition(img, sig_def('4D 5A'))
        self.assertEqual(result['status'], 'MISSING')

    def test_truncated_section_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'bad.image'
            write_synthetic_pe(path, [{'rva': 0x1000, 'vsize': 0x100}], 0x2000)
            raw = bytearray(path.read_bytes())
            # Claim more section headers than the file contains.
            struct.pack_into('<H', raw, 0x80 + 6, 0xFFFF)
            path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, 'section table'):
                vs.Image(str(path))


class ScannerConsistencyMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            'scan_sigs_matrix', Path(__file__).with_name('scan_sigs.py'))
        cls.ss = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.ss)

    def _scan_image(self, data):
        img = self.ss.Image.__new__(self.ss.Image)
        img.buf = bytes(data)
        img.size_of_image = len(data)
        img.sections = [{'rva': 1, 'vsize': len(data) - 1, 'rawsize': len(data) - 1,
                         'exec': True, 'index': 0, 'name': 'text', 'chars': 0x20000000}]
        return img

    def test_overlaps_and_final_verdicts_match(self):
        data = b'\0AAAA'
        self.assertEqual(vs.scan(data, 1, len(data), vs.parse_pattern('41 ?')),
                         self.ss.find_all(self._scan_image(data),
                                          *self.ss.compile_pattern('41 ?'), 64))

        cases = [
            (b'\0\x10\x20', sig_def('AA BB')),
            (b'\0\xAA\xBB', sig_def('AA BB')),
            (b'\0\xAA\xBB\xAA\xBB', sig_def('AA BB')),
            (b'\0\xAA\x09\0\0\0', off_def('AA', 'int32_t', 'Index', 1)),
        ]
        map_status = {'MISSING': 'MISSING', 'OK': 'OK', 'AMBIGUOUS': 'AMBIGUOUS',
                      'IMPLAUSIBLE': 'IMPLAUSIBLE', 'OUT_OF_RANGE': 'OUT_OF_RANGE',
                      'INCONSISTENT': 'INCONSISTENT'}
        for data, definition in cases:
            with self.subTest(definition=definition, data=data):
                vr = vs.evaluate_definition(memory_image(data), definition)
                si = self._scan_image(data)
                if definition['kind'] == 'sig':
                    sr = self.ss.eval_sig(si, {
                        'name': definition['name'], 'pattern': definition['pattern'],
                        'type': definition['sig_type'], 'offset': str(definition['disp_off'])})
                else:
                    sr = self.ss.eval_field(si, {
                        'name': definition['name'], 'pattern': definition['pattern'],
                        'ctype': definition['read_type'], 'kind': definition['offset_type'],
                        'idx': str(definition['disp_off'])})
                self.assertEqual(sr['resolveStatus'], map_status[vr['status']])
                self.assertEqual(sr['matches'], vr['matches'])
                self.assertEqual(bool(sr['rejectionReason']), bool(vr['rejectionReason']))

    def test_scanner_copies_are_byte_identical(self):
        root_copy = Path(__file__).parents[5] / 'tools' / 're' / 'scan_sigs.py'
        self.assertEqual(root_copy.read_bytes(), Path(__file__).with_name('scan_sigs.py').read_bytes())


class CppVerdictContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.project = Path(__file__).resolve().parents[1]

    def read(self, relative):
        return (self.project / relative).read_text('utf-8', errors='replace')

    def test_offset_operands_are_not_raw_dereferences(self):
        header = self.read('src/SDK/OffsetProvider.hpp')
        source = self.read('src/SDK/OffsetProvider.cpp')
        self.assertIn('readOffsetOperand(', header)
        self.assertIn('MemUtils::isReadableRange(operand, width)', source)
        self.assertNotIn('*reinterpret_cast<type*>(', header)

    def test_rel32_is_decoded_once_through_guarded_path(self):
        header = self.read('src/SDK/SigManager.hpp')
        source = self.read('src/SDK/SigManager.cpp')
        self.assertNotIn('result.rel(offset)', header)
        self.assertIn('SigType type, int offset', header)
        self.assertIn('type == SigType::RefSig', source)
        self.assertIn('resolveRel32(', source)
        self.assertIn('guardedReadInt32(', source)

    def test_fuzz_report_uses_defined_status_counts(self):
        source = self.read('src/SDK/SigFuzz.cpp')
        self.assertNotIn('offsetsFailed', source)
        self.assertNotIn('signaturesFailed', source)
        self.assertIn('offsets["total"]', source)

    def test_find_and_count_share_code_section_ranges(self):
        source = self.read('src/SDK/CodeSection.cpp')
        find_body = source[source.index('hat::scan_result CodeSection::find'):]
        count_body = source[source.index('int CodeSection::countMatches'):]
        self.assertIn('ranges()', find_body[:3000])
        self.assertIn('ranges()', count_body[:3000])
        self.assertIn('csReadableChunks', find_body[:3000])
        self.assertIn('csReadableChunks', count_body[:3000])

    def test_decrypt_timeout_and_mapping_failure_have_default_stop_paths(self):
        source = self.read('src/Solstice.cpp')
        self.assertIn('CodeSection::waitUntilDecrypted', source)
        self.assertIn('SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS', source)
        self.assertGreaterEqual(source.count('ExceptionHandler::shutdown();'), 3)

    def test_mapping_audit_recognizes_all_explicit_hardcoded_forms(self):
        spec = importlib.util.spec_from_file_location(
            'mapping_audit_contract', Path(__file__).with_name('mapping_audit.py'))
        audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(audit)
        self.assertEqual(audit.DERIVED_RE.search(
            'OffsetProvider::Actor_mSwinging + 0x4').groups(),
            ('Actor_mSwinging', '+', '0x4'))
        self.assertEqual(audit.OFFSET_TABLE_RE.search(
            'const size_t valueOffsets[1] = { 0x28 };').groups(),
            ('valueOffsets', '0x28'))
        self.assertEqual(audit.OFFSETOF_ASSERT_RE.search(
            'static_assert(offsetof(Packet, mSlots) == 0xA0);').groups(),
            ('Packet', 'mSlots', '0xA0'))
        self.assertIsNotNone(audit.LAYOUT_COMMENT_RE.match(
            '  int mTick; // this+0x18'))


if __name__ == '__main__':
    unittest.main()
