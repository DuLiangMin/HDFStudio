"""🧪 独立GRIB2模板5.3/7.3参考解析器，仅供只读业务样本数值核验。

输入：regular_ll、扫描模式0、无bitmap、general group splitting、二阶差分GRIB2。
输出：float64[rows,cols]（内部缺测为NaN）和参考cases；不调用ecCodes。
调用：由26NOV23_v01_grib_image_verify.py按HDFSTUDIO_GRIB2_SAMPLE读取。
公式来源：ECMWF模板5.2/5.3、7.2/7.3；不满足此子集直接断言失败。
https://codes.ecmwf.int/grib/format/grib2/templates/5/2/
https://codes.ecmwf.int/grib/format/grib2/templates/5/3/
https://codes.ecmwf.int/grib/format/grib2/templates/7/2/
https://codes.ecmwf.int/grib/format/grib2/templates/7/3/
"""
import json
import struct

import numpy as np


def signed_magnitude(b_value):
    i_value = int.from_bytes(b_value, 'big')
    i_sign = 1 << (len(b_value) * 8 - 1)
    return -(i_value & (i_sign - 1)) if i_value & i_sign else i_value


class BitReader:
    def __init__(self, b_data):
        self.a_bits = np.unpackbits(np.frombuffer(b_data, dtype=np.uint8))
        self.i_position = 0

    def read(self, i_count, i_width):
        assert 0 <= i_width <= 32 and 0 <= i_count <= 4194304
        i_end = self.i_position + i_count * i_width
        assert i_end <= self.a_bits.size
        if i_width:
            a_bits = self.a_bits[self.i_position:i_end].reshape(i_count, i_width)
            a_result = a_bits.astype(np.int64) @ (1 << np.arange(i_width - 1, -1, -1, dtype=np.int64))
        else:
            a_result = np.zeros(i_count, dtype=np.int64)
        self.i_position = i_end
        return a_result

    def align(self):
        i_aligned = (self.i_position + 7) // 8 * 8
        assert not self.a_bits[self.i_position:i_aligned].any()
        self.i_position = i_aligned


def decode_sections(d_sections):
    b_grid = d_sections[3]
    b_packing = d_sections[5]
    assert len(b_grid) == 72 and int.from_bytes(b_grid[12:14], 'big') == 0
    assert b_grid[71] == 0 and d_sections[6] == b'\x00\x00\x00\x06\x06\xff'
    i_columns = int.from_bytes(b_grid[30:34], 'big')
    i_rows = int.from_bytes(b_grid[34:38], 'big')
    i_count = int.from_bytes(b_packing[5:9], 'big')
    assert 0 < i_rows * i_columns == i_count <= 4194304
    assert int.from_bytes(b_grid[6:10], 'big') == i_count
    assert len(b_packing) == 49 and int.from_bytes(b_packing[9:11], 'big') == 3
    assert b_packing[21] == 1 and b_packing[22] in [0, 1]
    assert b_packing[47] == 2 and 1 <= b_packing[48] <= 4
    f_reference = struct.unpack('>f', b_packing[11:15])[0]
    i_binary_scale = signed_magnitude(b_packing[15:17])
    i_decimal_scale = signed_magnitude(b_packing[17:19])
    i_reference_bits = b_packing[19]
    i_missing_mode = b_packing[22]
    i_groups = int.from_bytes(b_packing[31:35], 'big')
    assert 0 < i_groups <= i_count
    i_extra = b_packing[48]
    b_values = d_sections[7][5:]
    l_first = [int.from_bytes(b_values[i_extra * i_index:i_extra * (i_index + 1)], 'big') for i_index in range(2)]
    i_minimum = signed_magnitude(b_values[i_extra * 2:i_extra * 3])
    o_bits = BitReader(b_values[i_extra * 3:])
    a_references = o_bits.read(i_groups, i_reference_bits)
    o_bits.align()
    a_widths = o_bits.read(i_groups, b_packing[36]) + b_packing[35]
    o_bits.align()
    a_lengths = o_bits.read(i_groups, b_packing[46]) * b_packing[41] + int.from_bytes(b_packing[37:41], 'big')
    o_bits.align()
    a_lengths[-1] = int.from_bytes(b_packing[42:46], 'big')
    assert int(a_lengths.sum()) == i_count and (a_lengths > 0).all()
    a_differences = np.zeros(i_count, dtype=np.int64)
    a_missing = np.zeros(i_count, dtype=bool)
    i_start = 0
    for i_group in range(i_groups):
        i_width = int(a_widths[i_group])
        i_length = int(a_lengths[i_group])
        i_reference = int(a_references[i_group])
        a_values = o_bits.read(i_length, i_width)
        a_group_missing = np.zeros(i_length, dtype=bool)
        if i_missing_mode:
            i_all_ones = (1 << (i_width if i_width else i_reference_bits)) - 1
            if i_width:
                a_group_missing = a_values >= i_all_ones - (i_missing_mode - 1)
            elif i_reference >= i_all_ones - (i_missing_mode - 1):
                a_group_missing[:] = True
        a_differences[i_start:i_start + i_length] = a_values + i_reference
        a_missing[i_start:i_start + i_length] = a_group_missing
        i_start += i_length
    assert o_bits.a_bits.size - o_bits.i_position < 8
    o_bits.align()
    a_valid = a_differences[~a_missing]
    assert a_valid.size >= 2
    # 首两个非缺测packed值是占位符；二阶差分仅沿非缺测序列恢复。
    a_increments = a_valid[2:] + i_minimum
    a_recovered = np.empty(a_valid.size, dtype=np.int64)
    a_recovered[:2] = l_first
    a_recovered[2:] = l_first[1] + np.cumsum(l_first[1] - l_first[0] + np.cumsum(a_increments))
    a_result = np.full(i_count, np.nan, dtype=np.float64)
    a_result[~a_missing] = (f_reference + np.ldexp(a_recovered.astype(np.float64), i_binary_scale)) * (10.0 ** -i_decimal_scale)
    return a_result.reshape(i_rows, i_columns), {'packing_template': 3, 'missing_mode': i_missing_mode, 'missing_count': int(a_missing.sum()), 'order': 2, 'groups': i_groups}


def make_cases(p_source, p_output):
    b_source = p_source.read_bytes()
    i_offset = 0
    l_fields = []
    d_arrays = {}
    p_output.mkdir(parents=True, exist_ok=True)
    while i_offset < len(b_source):
        assert b_source[i_offset:i_offset + 4] == b'GRIB' and b_source[i_offset + 7] == 2
        i_length = int.from_bytes(b_source[i_offset + 8:i_offset + 16], 'big')
        b_message = b_source[i_offset:i_offset + i_length]
        assert len(b_message) == i_length and b_message[-4:] == b'7777'
        d_sections = {}
        i_position = 16
        while i_position < i_length - 4:
            i_size = int.from_bytes(b_message[i_position:i_position + 4], 'big')
            i_section = b_message[i_position + 4]
            assert i_size >= 5 and i_position + i_size <= i_length - 4 and i_section not in d_sections
            d_sections[i_section] = b_message[i_position:i_position + i_size]
            i_position += i_size
        assert i_position == i_length - 4 and set(d_sections) == {1, 3, 4, 5, 6, 7}
        a_values, d_packing = decode_sections(d_sections)
        i_index = len(l_fields)
        s_tag = 'field-' + str(i_index)
        a_values.astype('<f8').tofile(p_output / (s_tag + '.f64'))
        a_flat = a_values.ravel()
        l_positions = sorted(set([0, a_flat.size // 2, a_flat.size - 1, int(np.flatnonzero(np.isfinite(a_flat))[0])]))
        l_probes = [{'position': i_position, 'value': float(a_flat[i_position]) if np.isfinite(a_flat[i_position]) else None} for i_position in l_positions]
        l_fields.append({'target': '/field/' + str(i_index), 'tag': s_tag, 'offset': str(i_offset), 'length': str(i_length), 'rows': a_values.shape[0], 'cols': a_values.shape[1], 'probes': l_probes, **d_packing})
        d_arrays[s_tag] = a_values
        i_offset += i_length
    assert i_offset == len(b_source) and l_fields
    d_cases = {'edition': 2, 'field_count': len(l_fields), 'reference': 'Independent NumPy GRIB2 5.3/7.3 bitstream decoder; no ecCodes', 'reference_directory': str(p_output), 'fields': l_fields, 'png_fields': [0, 1, 4, len(l_fields) // 2, 37, 62, len(l_fields) - 1]}
    p_cases = p_output / 'external-grib2-cases.json'
    p_cases.write_text(json.dumps(d_cases, indent=2, allow_nan=False), encoding='utf-8')
    return d_cases, d_arrays
