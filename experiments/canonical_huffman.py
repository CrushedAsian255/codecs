def decode_huffman(bit_lengths):
    actual_codes=[]
    largest_bits = 0
    for idx,bits in enumerate(bit_lengths):
        if bits != 0:
            actual_codes.append((idx,bits))
        largest_bits = max(largest_bits,bits)
    actual_codes.sort(key=lambda a: a[1])

    code = -1
    bit = 0

    huffman = [(-1,-1) for _ in range(1<<largest_bits)]
    for symbol,bits in actual_codes:
        code += 1
        code <<= (bits-bit)
        bit = bits
        for x in range(code << (largest_bits-bits), (code+1) << (largest_bits-bits)):
            huffman[x]=(symbol,bits)
    print(huffman)
    pass

print(decode_huffman([2,3,1,3]))