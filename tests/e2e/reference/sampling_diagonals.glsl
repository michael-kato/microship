// Author decomposition retained only for real-application equivalence diagnostics.
// MIT license in third_party/nile/LICENSE.
uint matrix4(uint offset, uint value)
{
    return ((value >> 3u) & sz_columns[3072u+offset]) ^
           ((value >> 2u) & sz_columns[3072u+offset+1u]) ^
           ((value >> 1u) & sz_columns[3072u+offset+2u]) ^
           (value & sz_columns[3072u+offset+3u]) ^
           ((value << 1u) & sz_columns[3072u+offset+4u]) ^
           ((value << 2u) & sz_columns[3072u+offset+5u]) ^
           ((value << 3u) & sz_columns[3072u+offset+6u]);
}

uint matrix8(uint offset, uint value)
{
    uint result = value & sz_columns[3072u+offset+7u];
    for (uint bit = 1u; bit <= 7u; ++bit)
        result ^= ((value >> bit) & sz_columns[3072u+offset+7u-bit]) ^
                  ((value << bit) & sz_columns[3072u+offset+7u+bit]);
    return result;
}

uint sz_sample(uint index, uint dimension)
{
    if (dimension == 0u) return bitfieldReverse(index);
    if (dimension < 16u)
    {
        uint offset = (dimension-1u)*14u;
        uint value = bitfieldReverse(matrix4(offset,index));
        value ^= value << 16u;
        value ^= (value & 0x00ff00ffu) << 8u;
        value ^= (value & 0x0f0f0f0fu) << 4u;
        return matrix4(offset+7u,value);
    }
    uint offset = 210u+(dimension-16u)*30u;
    uint value = bitfieldReverse(matrix8(offset,index));
    value ^= value << 16u;
    value ^= (value & 0x00ff00ffu) << 8u;
    return matrix8(offset+15u,value);
}

