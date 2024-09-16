def decode_huffman(bit_lengths):
    largest_bits = 0
    for bits in bit_lengths:
        largest_bits = max(largest_bits,bits)
    starting_points = [0]*(largest_bits+1)
    for bits in bit_lengths:
        starting_points[bits] += 1
    for i in range(1,largest_bits+1):
        starting_points[i] += starting_points[i-1]
    actual_codes = [None]*starting_points[largest_bits]
    for idx,bits in enumerate(bit_lengths):
        if bits != 0:
            actual_codes[starting_points[bits-1]] = (idx,bits)
            starting_points[bits-1] += 1
    actual_codes.sort(key=lambda a: a[1])

    code = -1
    bit = 0

    huffman = [None]*(1<<largest_bits)
    for symbol,bits in actual_codes:
        code += 1
        code <<= (bits-bit)
        bit = bits
        for x in range(code << (largest_bits-bits), (code+1) << (largest_bits-bits)):
            huffman[x]=(symbol,bits)
    return huffman

print(decode_huffman([2,3,1,3]))