#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

#define err(x) {printf("Error at line %d: %s\n",__LINE__,x);exit(1);}
#define assert(x,y) {if(!(x)){printf("Assertion failure at line %d: %s\n",__LINE__,y);exit(1);}}
#define todo(x) {printf("Not implemented at line %d: %s\n",__LINE__,x);exit(1);}

#define ceil_div(n,d) (((n)+(d)-1)/(d));

typedef uint32_t pixel_t;
typedef uint16_t symbol_t;

struct bitstream {
    uint8_t* data;
    uint64_t current_read;
};
uint8_t read_bit(struct bitstream* state) {
    uint8_t output = (state->data[state->current_read>>3] >> (state->current_read&0x7))&1;
    state->current_read++;
    return output;
}
uint64_t read_bits(struct bitstream* state, uint8_t bit_count) {
    uint64_t output = 0;
    for(int i = 0; i < bit_count; i++) {
        output |= read_bit(state) << i;
    }
    return output;
}

struct image_data {
    pixel_t* data;
    uint16_t width;
    uint16_t height;
};
struct image_data malloc_new_image(uint16_t width, uint16_t height) {
    struct image_data output = {
        .data = malloc(width*height*4),
        .width = width,
        .height = height
    };
    memset(output.data,0,width*height*4);
    return output;
}
void write_image(const struct image_data* image_data, const char* output_file_name) {
    uint8_t has_alpha = 0;
    for(int i = 0; i < image_data->width*image_data->height; i++) {
        if((image_data->data[i]&0xff000000) != (image_data->data[0]&0xff000000)) {
            has_alpha = 1;
            break;
        }
    }
    printf("Image %s: %d x %d%s\n",output_file_name,image_data->width,image_data->height,has_alpha?" with alpha":"");
    char* output_name_buffer = malloc(strlen(output_file_name) + 20);
    
    sprintf(output_name_buffer,"%s.ppm",output_file_name);
    FILE* output_file = fopen(output_name_buffer,"wb");
    fprintf(output_file,"P6\n%d %d\n255\n",image_data->width,image_data->height);
    for(int i = 0; i < image_data->width*image_data->height; i++) {
        uint8_t data;
        data = (image_data->data[i]>>16)&0xff;
        fwrite(&data,1,1,output_file);
        data = (image_data->data[i]>>8)&0xff;
        fwrite(&data,1,1,output_file);
        data = image_data->data[i]&0xff;
        fwrite(&data,1,1,output_file);
    }
    fclose(output_file);
    free(output_name_buffer);
}

enum transform_type {
    PREDICTOR_TRANSFORM,
    COLOUR_TRANSFORM,
    SUBTRACT_GREEN_TRANSFORM,
    COLOUR_INDEXING_TRANSFORM
};
const char* transform_names[4] = {
    "Predictor",
    "Colour",
    "Subtract Green",
    "Colour Index"
};

struct prefix_code_entry {
    symbol_t symbol;
    uint8_t bits;
};
struct prefix_code {
    struct prefix_code_entry *table;
    uint8_t total_bits;
};
void print_prefix_code(const struct prefix_code code) {
    for(int i = 0; i < 1<<code.total_bits; i++) {
        printf("%d: %d\n", code.table[i].symbol, code.table[i].bits);
    }
}
symbol_t read_from_prefix_code(struct bitstream* bitstream, const struct prefix_code code) {
    uint64_t idx = 0;
    for(int i = 0; i < code.total_bits; i++) {
        idx <<= 1;
        idx |= read_bit(bitstream);
    }
    bitstream->current_read -= code.total_bits;
    bitstream->current_read += code.table[idx].bits;
    return code.table[idx].symbol;
}

void generate_canonical_code(struct prefix_code* code, const symbol_t* lengths, const symbol_t length_counts) {
    symbol_t max_length = 0;
    symbol_t starting_points[16] = {0};
    for(int i = 0; i < length_counts; i++) {
        if(lengths[i] == 0) continue;
        if(lengths[i] > max_length) {
            max_length = lengths[i];
        }
        assert(max_length < 16,"Invalid canonical Huffman code");
        starting_points[lengths[i]]++;
    }
    for(int i = 1; i <= max_length; i++) {
        starting_points[i] += starting_points[i-1];
    }
    struct prefix_code_entry* sorted_codes = malloc(sizeof(struct prefix_code_entry)*starting_points[max_length]);
    for(symbol_t i = 0; i < length_counts; i++) {
        symbol_t bits = lengths[i];
        if(bits != 0) {
            sorted_codes[starting_points[bits-1]].symbol = i;
            sorted_codes[starting_points[bits-1]].bits = bits;
            starting_points[bits-1]++;
        }
    }
    symbol_t running_code = -1;
    symbol_t prev_bits = 0;
    code->total_bits = max_length;
    code->table = malloc(sizeof(struct prefix_code_entry)<<max_length);
    assert(code->table!=NULL,"Unable to allocate huffman table");
    for(int i = 0; i < starting_points[max_length]; i++) {
        struct prefix_code_entry entry = sorted_codes[i];
        running_code++;
        running_code <<= (entry.bits-prev_bits);
        prev_bits = entry.bits;
        for(int j = running_code<<(max_length-entry.bits); j < (running_code+1)<<(max_length-entry.bits); j++) {
            code->table[j] = entry;
        }
    }
}

// ll prefix codes: low level code-length codes
static const int llcodes = 19;
static const int llcode_orders[llcodes] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
void read_code_complex(struct bitstream* bitstream, struct prefix_code* code, symbol_t alphabet_size) {
    uint8_t llcode_length = read_bits(bitstream,4) + 4;
    symbol_t llcode_lengths[llcodes] = {0};
    for(int i = 0; i < llcode_length; i++) {
        llcode_lengths[llcode_orders[i]] = read_bits(bitstream,3);
    }
    symbol_t max_entry_count = alphabet_size;
    if(read_bit(bitstream)) {
        max_entry_count = read_bits(bitstream,read_bits(bitstream,3)*2 + 2) + 2;
    }
    assert(max_entry_count <= alphabet_size, "Alphabet too big");
    struct prefix_code temp_prefix_code;
    generate_canonical_code(&temp_prefix_code,llcode_lengths,llcodes);
    
    symbol_t* code_lengths = malloc(sizeof(symbol_t)*alphabet_size);
    symbol_t read_count = 0;
    symbol_t prev_read = 8;
    for(int i = 0; i < max_entry_count && read_count < alphabet_size; i++) {
        symbol_t read_symbol = read_from_prefix_code(bitstream,temp_prefix_code);
        switch(read_symbol) {
            default: case 0: {
                code_lengths[read_count++] = 0;
            } break;
            case  1: case  2: case  3: case  4: case 5:
            case  6: case  7: case  8: case  9: case 10:
            case 11: case 12: case 13: case 14: case 15: {
                code_lengths[read_count++] = read_symbol;
                prev_read = read_symbol;
            } break;
            case 16: {
                symbol_t repeat = read_bits(bitstream,2) + 3;
                for(int j = 0; j < repeat; j++) {
                    code_lengths[read_count++] = prev_read;
                }
            } break;
            case 17: {
                symbol_t repeat = read_bits(bitstream,3) + 3;
                for(int j = 0; j < repeat; j++) {
                    code_lengths[read_count++] = 0;
                }
            } break; 
            case 18: {
                symbol_t repeat = read_bits(bitstream,7) + 11;
                for(int j = 0; j < repeat; j++) {
                    code_lengths[read_count++] = 0;
                }
            } break;
        }
    }

    generate_canonical_code(code,code_lengths,read_count);
    free(temp_prefix_code.table);
}
void read_code_simple(struct bitstream* bitstream, struct prefix_code* code, symbol_t alphabet_size) { 
    symbol_t multiple_symbols = read_bit(bitstream);
    code->total_bits = multiple_symbols;
    code->table = malloc((multiple_symbols+1)*sizeof(struct prefix_code));
    code->table[0].bits = multiple_symbols;
    code->table[0].symbol = read_bits(bitstream,read_bit(bitstream)?8:1);
    if(multiple_symbols) {
        code->table[1].bits = multiple_symbols;
        code->table[1].symbol = read_bits(bitstream,8);
    }
}

struct prefix_group {
    struct prefix_code codes[5];
};
void decode_prefix_group(struct bitstream* bitstream, struct prefix_group* prefix_group, symbol_t cache_size) {
    for(int i = 0; i < 5; i++) {
        symbol_t alphabet_size = 256;
        if(i == 0) alphabet_size += cache_size + 24;
        if(i == 4) alphabet_size = 40;
        if(read_bit(bitstream)) {
            read_code_simple(bitstream, &(prefix_group->codes[i]),alphabet_size);
        } else {
            read_code_complex(bitstream, &(prefix_group->codes[i]),alphabet_size);
        }
    }
}

static const int8_t lz77_distance_neighbourhood[240] = {
     0, 1,  1, 0,  1, 1, -1, 1,  0, 2,  2, 0,  1, 2,
    -1, 2,  2, 1, -2, 1,  2, 2, -2, 2,  0, 3,  3, 0,
     1, 3, -1, 3,  3, 1, -3, 1,  2, 3, -2, 3,  3, 2,
    -3, 2,  0, 4,  4, 0,  1, 4, -1, 4,  4, 1, -4, 1,
     3, 3, -3, 3,  2, 4, -2, 4,  4, 2, -4, 2,  0, 5,
     3, 4, -3, 4,  4, 3, -4, 3,  5, 0,  1, 5, -1, 5, 
     5, 1, -5, 1,  2, 5, -2, 5,  5, 2, -5, 2,  4, 4,
    -4, 4,  3, 5, -3, 5,  5, 3, -5, 3,  0, 6,  6, 0,
     1, 6, -1, 6,  6, 1, -6, 1,  2, 6, -2, 6,  6, 2,
    -6, 2,  4, 5, -4, 5,  5, 4, -5, 4,  3, 6, -3, 6,
     6, 3, -6, 3,  0, 7,  7, 0,  1, 7, -1, 7,  5, 5,
    -5, 5,  7, 1, -7, 1,  4, 6, -4, 6,  6, 4, -6, 4, 
     2, 7, -2, 7,  7, 2, -7, 2,  3, 7, -3, 7,  7, 3,
    -7, 3,  5, 6, -5, 6,  6, 5, -6, 5,  8, 0,  4, 7,
    -4, 7,  7, 4, -7, 4,  8, 1,  8, 2,  6, 6, -6, 6,
     8, 3,  5, 7, -5, 7,  7, 5, -7, 5,  8, 4,  6, 7,
    -6, 7,  7, 6, -7, 6,  8, 5,  7, 7, -7, 7,  8, 6,
    8, 7
};

uint64_t read_lz77_code(struct bitstream* bitstream, symbol_t prefix_code) {
    if(prefix_code < 4) {
        return prefix_code;
    }
    int extra_bits = (prefix_code - 2) >> 1;
    int offset = (2 + (prefix_code & 1)) << extra_bits;
    return offset + read_bits(bitstream,extra_bits);
}

uint32_t colour_hash(pixel_t pixel, uint8_t colour_cache_size) {
    return ((0x1e35a7bd * pixel) & 0xFFFFFFFF) >> (32 - colour_cache_size);
}

void decode_image(struct bitstream* bitstream, struct image_data* image, bool is_main_image) {
    symbol_t colour_cache_size = 0;
    symbol_t colour_cache_bits = 0;
    if(read_bit(bitstream)) {
        colour_cache_bits = read_bits(bitstream,4);
        colour_cache_size = 1<<colour_cache_bits;
    }
    pixel_t* colour_cache = malloc(4*colour_cache_size);
    memset(colour_cache,0,colour_cache_size*4);
    assert(colour_cache!=NULL,"Error allocating memory");
    printf("Colour cache size: %d\n",colour_cache_size);

    uint32_t prefix_group_count = 1;
    uint8_t meta_prefix_bits = 0;
    struct image_data meta_prefix_image;

    if(is_main_image && read_bit(bitstream)) {
        meta_prefix_bits = read_bits(bitstream,3)+2;
        uint32_t meta_prefix_image_width = ceil_div(image->width,1<<meta_prefix_bits);
        uint32_t meta_prefix_image_height = ceil_div(image->height,1<<meta_prefix_bits);
        meta_prefix_image = malloc_new_image(meta_prefix_image_width,meta_prefix_image_height);
        printf("Decoding meta-prefix subimage of size %d x %d\n",meta_prefix_image_width,meta_prefix_image_height);
        decode_image(bitstream,&meta_prefix_image,0);
        write_image(&meta_prefix_image,"meta-prefix");
        for(int i = 0; i < meta_prefix_image_width*meta_prefix_image_height; i++) {
            symbol_t meta_prefix_group_id = (meta_prefix_image.data[i]>>8)&0xffff;
            if(meta_prefix_group_id >= prefix_group_count) prefix_group_count = meta_prefix_group_id+1;
        }
        printf("Total meta-prefix groups: %d\n",prefix_group_count);
    }

    struct prefix_group* groups = malloc(sizeof(struct prefix_group)*prefix_group_count);
    assert(groups!=NULL,"Error allocating memory");

    for(int i = 0; i < prefix_group_count; i++) {
        decode_prefix_group(bitstream,&groups[i],colour_cache_size);
    }

    for(int pixel = 0; pixel < image->height * image->width;) {
        int group_num = 0;
        if(prefix_group_count > 1) {
            uint32_t x = (pixel % image->width) >> meta_prefix_bits;
            uint32_t y = (pixel / image->width) >> meta_prefix_bits;
            pixel_t entropy_pixel = meta_prefix_image.data[meta_prefix_image.width * y + x];
            group_num = (entropy_pixel >> 8) & 0xffff;
        }
        const struct prefix_group group = groups[group_num];
        symbol_t g = read_from_prefix_code(bitstream,group.codes[0]);
        if(g < 256) {
            symbol_t r = read_from_prefix_code(bitstream,group.codes[1]);
            symbol_t b = read_from_prefix_code(bitstream,group.codes[2]);
            symbol_t a = read_from_prefix_code(bitstream,group.codes[3]);
            image->data[pixel++]=a<<24 | r<<16 | g<<8 | b;
            if(colour_cache_bits) {
                colour_cache[colour_hash(a<<24 | r<<16 | g<<8 | b,colour_cache_bits)] = a<<24 | r<<16 | g<<8 | b;
            }
        } else if (g < 256+24) {
            uint64_t length = read_lz77_code(bitstream,g-256);
            symbol_t distance_prefix = read_from_prefix_code(bitstream,group.codes[4]);
            uint64_t distance_code = read_lz77_code(bitstream,distance_prefix);
            int64_t distance = distance_code - 119;
            if(distance_code < 120) {
                int8_t x_off = lz77_distance_neighbourhood[(distance_code<<1)];
                int8_t y_off = lz77_distance_neighbourhood[(distance_code<<1)+1];
                distance = x_off + y_off*image->width;
            }
            if(distance < 1) {distance = 1;}
            for(int64_t i = 0; i <= length; i++) {
                if(colour_cache_bits) {
                    colour_cache[colour_hash(image->data[pixel-distance+i],colour_cache_bits)] = image->data[pixel-distance+i];
                }
                image->data[pixel+i] = image->data[pixel-distance+i];
            }
            pixel += length+1;
        } else {
            image->data[pixel++] = colour_cache[g-(256+24)];
        }
    }

    for(int i = 0; i < prefix_group_count; i++) {
        for(int j = 0; j < 5; j++) {
            free(groups[i].codes[j].table);
        }
    }
    free(colour_cache);
    free(groups);
}

int32_t ALPHA(pixel_t x) {return x>>24;}
int32_t RED(pixel_t x) {return (x>>16)&0xff;}
int32_t GREEN(pixel_t x) {return (x>>8)&0xff;}
int32_t BLUE(pixel_t x) {return x&0xff;}

pixel_t add_pixels(pixel_t a, pixel_t b) {
    pixel_t o = 0;
    o |= (ALPHA(a) + ALPHA(b))&0xff;
    o <<= 8;
    o |= (RED(a) + RED(b))&0xff;
    o <<= 8;
    o |= (GREEN(a) + GREEN(b))&0xff;
    o <<= 8;
    o |= (BLUE(a) + BLUE(b))&0xff;
    return o;
}

uint8_t clamp(int32_t x) {
    if(x<0) return 0;
    if(x>255) return 255;
    return x;
}

void apply_predictors(struct image_data* image, const struct image_data* predictor_map, uint8_t block_scale) {
    for(int y = 0; y < image->height; y++) {
        for(int x = 0; x < image->width; x++) {
            pixel_t predictor_pixel = predictor_map->data[predictor_map->width * (y >> block_scale) + (x >> block_scale)];
            int8_t predictor = (predictor_pixel >> 8) & 0xff;
            pixel_t predicted_pixel = 0x00000000;
            if(x == 0 && y == 0) {
                predicted_pixel = 0xff000000;
            } else if(x == 0) {
                predicted_pixel = image->data[(y-1)*image->width];
            } else if(y == 0) {
                predicted_pixel = image->data[x-1];
            } else {
                pixel_t L  = image->data[(y*image->width)+x-1];
                pixel_t T  = image->data[((y-1)*image->width)+x];
                pixel_t TL = image->data[((y-1)*image->width)+(x-1)];
                pixel_t TR = image->data[((y-1)*image->width)+(x+1)];
                
                switch(predictor) {
                    case 0:
                        predicted_pixel = 0xff000000;
                        break;
                    case 1:
                        predicted_pixel = L;
                        break;
                    case 2:
                        predicted_pixel = T;
                        break;
                    case 3:
                        predicted_pixel = TR;
                        break;
                    case 4:
                        predicted_pixel = TL;
                        break;
                    case 5: {
                        uint8_t a = (((ALPHA(L) + ALPHA(TR)) / 2) + ALPHA(T))/2;
                        uint8_t r = (((RED(L) + RED(TR)) / 2) + RED(T))/2;
                        uint8_t g = (((GREEN(L) + GREEN(TR)) / 2) + GREEN(T))/2;
                        uint8_t b = (((BLUE(L) + BLUE(TR)) / 2) + BLUE(T))/2;
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 6: {
                        uint8_t a = ((ALPHA(L) + ALPHA(TL)) / 2);
                        uint8_t r = ((RED(L) + RED(TL)) / 2);
                        uint8_t g = ((GREEN(L) + GREEN(TL)) / 2);
                        uint8_t b = ((BLUE(L) + BLUE(TL)) / 2);
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 7: {
                        uint8_t a = ((ALPHA(L) + ALPHA(T)) / 2);
                        uint8_t r = ((RED(L) + RED(T)) / 2);
                        uint8_t g = ((GREEN(L) + GREEN(T)) / 2);
                        uint8_t b = ((BLUE(L) + BLUE(T)) / 2);
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 8: {
                        uint8_t a = ((ALPHA(TL) + ALPHA(T)) / 2);
                        uint8_t r = ((RED(TL) + RED(T)) / 2);
                        uint8_t g = ((GREEN(TL) + GREEN(T)) / 2);
                        uint8_t b = ((BLUE(TL) + BLUE(T)) / 2);
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 9: {
                        uint8_t a = ((ALPHA(T) + ALPHA(TR)) / 2);
                        uint8_t r = ((RED(T) + RED(TR)) / 2);
                        uint8_t g = ((GREEN(T) + GREEN(TR)) / 2);
                        uint8_t b = ((BLUE(T) + BLUE(TR)) / 2);
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 10: {
                        uint8_t a = (((ALPHA(L) + ALPHA(TL)) / 2) + ((ALPHA(T) + ALPHA(TR)) / 2))/2;
                        uint8_t r = (((RED(L) + RED(TL)) / 2) + ((RED(T) + RED(TR)) / 2))/2;
                        uint8_t g = (((GREEN(L) + GREEN(TL)) / 2) + ((GREEN(T) + GREEN(TR)) / 2))/2;
                        uint8_t b = (((BLUE(L) + BLUE(TL)) / 2) + ((BLUE(T) + BLUE(TR)) / 2))/2;
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 11: {
                        int pAlpha = ALPHA(L) + ALPHA(T) - ALPHA(TL);
                        int pRed = RED(L) + RED(T) - RED(TL);
                        int pGreen = GREEN(L) + GREEN(T) - GREEN(TL);
                        int pBlue = BLUE(L) + BLUE(T) - BLUE(TL);
                        int pL = abs(pAlpha - ALPHA(L)) + abs(pRed - RED(L)) + abs(pGreen - GREEN(L)) + abs(pBlue - BLUE(L));
                        int pT = abs(pAlpha - ALPHA(T)) + abs(pRed - RED(T)) + abs(pGreen - GREEN(T)) + abs(pBlue - BLUE(T));
                        predicted_pixel = (pL<pT)?L:T;
                    } break;
                    case 12: {
                        uint8_t a = clamp(ALPHA(L) + ALPHA(T) - ALPHA(TL));
                        uint8_t r = clamp(RED(L) + RED(T) - RED(TL));
                        uint8_t g = clamp(GREEN(L) + GREEN(T) - GREEN(TL));
                        uint8_t b = clamp(BLUE(L) + BLUE(T) - BLUE(TL));
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    case 13: {
                        uint8_t a_ = (ALPHA(L) + ALPHA(T))/2;
                        uint8_t a = clamp(a_ + (a_ - ALPHA(TL)) / 2);
                        uint8_t r_ = (RED(L) + RED(T))/2;
                        uint8_t r = clamp(r_ + (r_ - RED(TL)) / 2);
                        uint8_t g_ = (GREEN(L) + GREEN(T))/2;
                        uint8_t g = clamp(g_ + (g_ - GREEN(TL)) / 2);
                        uint8_t b_ = (BLUE(L) + BLUE(T))/2;
                        uint8_t b = clamp(b_ + (b_ - BLUE(TL)) / 2);
                        predicted_pixel = a<<24 | r<<16 | g<<8 | b;
                    } break;
                    default: 
                        printf("%d\n",predictor);
                        todo("all the predictors");
                }
            }
            image->data[y*image->width+x] = add_pixels(predicted_pixel,image->data[y*image->width+x]);
        }
    }
}

void apply_subtract_green(struct image_data* image) {
    for(int i = 0; i < image->height*image->width; i++) {
        pixel_t output_pixel = image->data[i]&0xff00ff00;
        output_pixel |= (BLUE(image->data[i])+GREEN(image->data[i])) & 0xff;
        output_pixel |= ((RED(image->data[i])+GREEN(image->data[i])) & 0xff)<<16;
        image->data[i]=output_pixel;
    }
}

int8_t colour_transform_delta(uint8_t t_, uint8_t c_) {
    int32_t t = t_ >= 128 ? -256+t_ : t_;
    int32_t c = c_ >= 128 ? -256+c_ : c_;
    return ((t*c) >> 5)&0xff;
}

void apply_colour_transform(struct image_data* image, const struct image_data* colour_info_map, uint8_t block_scale) {
    for(int y = 0; y < image->height; y++) {
        for(int x = 0; x < image->width; x++) {
            pixel_t pixel = image->data[y*image->width+x];
            pixel_t ctx_pixel = colour_info_map->data[colour_info_map->width * (y >> block_scale) + (x >> block_scale)];
            uint8_t red_to_blue = (uint8_t)RED(ctx_pixel);
            uint8_t green_to_blue = (uint8_t)GREEN(ctx_pixel);
            uint8_t green_to_red = (uint8_t)BLUE(ctx_pixel);
            uint8_t temp_red = RED(pixel);
            uint8_t temp_blue = BLUE(pixel);
            temp_red += colour_transform_delta(green_to_red,GREEN(pixel));
            temp_blue += colour_transform_delta(green_to_blue,GREEN(pixel));
            temp_blue += colour_transform_delta(red_to_blue,temp_red&0xff);
            image->data[y*image->width+x] = pixel&0xff00ff00 | (temp_red&0xff) << 16 | (temp_blue&0xff);
        }
    }
}

int main(int argc, char* argv[]) {
    assert(argc >= 2, "No input file!");
    FILE* input_file = fopen(argv[1],"rb");
    assert(input_file != NULL, "Error: no such file or directory");
    fseek(input_file,0,SEEK_END);
    long file_length = ftell(input_file);
    fseek(input_file,0,SEEK_SET);
    uint8_t* file_data = malloc(file_length);
    assert(file_data != NULL, "Error: unable to allocate memory");
    size_t read_data_count = fread(file_data,1,file_length,input_file);
    assert(read_data_count == file_length, "Error: unable to read complete file");
    fclose(input_file);
    struct bitstream file = {
        .data = file_data,
        .current_read = 0,
    };
    assert(read_bits(&file,32)==(*(uint32_t*)"RIFF"),"Error: invalid RIFF header"); 
    assert(read_bits(&file,32)==file_length-8, "Error: invalid RIFF header");
    assert(read_bits(&file,32)==(*(uint32_t*)"WEBP"),"Error: invalid WebP header"); 
    assert(read_bits(&file,32)==(*(uint32_t*)"VP8L"),"Error: not a lossless WebP"); 
    uint32_t a = read_bits(&file,32);
    uint32_t b = file_length-20-(a&1);
    assert(a==b,"Error: invalid WebP header");    
    assert(read_bits(&file,8)==0x2f,"Error: invalid WebP header");  
    uint16_t image_width = read_bits(&file,14) + 1;
    uint16_t image_height = read_bits(&file,14) + 1;
    uint8_t use_alpha = read_bits(&file,1);
    printf("Image dimensions: %d x %d %s\n",image_width,image_height,use_alpha?"with alpha":"");   
    assert(read_bits(&file,3)==0,"Error: invalid WebP version");
    enum transform_type transforms[4];
    uint8_t transform_count = 0;

    uint32_t transform_predictor_block_scale = 0;
    struct image_data transform_predictor_subimage;

    uint32_t transform_colour_block_scale = 0;
    struct image_data transform_colour_subimage;

    while(read_bit(&file)) {
        assert(transform_count < 4,"Error: too many image transforms");
        enum transform_type transform_type = read_bits(&file,2);
        printf("Transform %s\n",transform_names[transform_type]);
        switch(transform_type) {
            case SUBTRACT_GREEN_TRANSFORM:
                break;
            case PREDICTOR_TRANSFORM: {
                transform_predictor_block_scale = read_bits(&file,3)+2;
                uint32_t subimage_width = ceil_div(image_width,1<<transform_predictor_block_scale);
                uint32_t subimage_height = ceil_div(image_height,1<<transform_predictor_block_scale);
                transform_predictor_subimage = malloc_new_image(subimage_width,subimage_height);
                printf("Decoding predictor subimage\n");
                decode_image(&file,&transform_predictor_subimage,false);
                write_image(&transform_predictor_subimage,"transform_predictor");
            }; break;
            case COLOUR_TRANSFORM: {
                transform_colour_block_scale = read_bits(&file,3)+2;
                uint32_t subimage_width = ceil_div(image_width,1<<transform_colour_block_scale);
                uint32_t subimage_height = ceil_div(image_height,1<<transform_colour_block_scale);
                transform_colour_subimage = malloc_new_image(subimage_width, subimage_height);
                printf("Decoding colour subimage\n");
                decode_image(&file, &transform_colour_subimage, false);
                write_image(&transform_colour_subimage, "transform_colour");
            }; break;
            default:
                printf("Not implemented transform: %s\n",transform_names[transform_type]);
                exit(1);
        }
        transforms[transform_count] = transform_type;
        transform_count++;
    }
    struct image_data image = malloc_new_image(image_width,image_height);
    printf("Decoding main image\n");
    decode_image(&file,&image,true);
    write_image(&image,"image_pretransform");
    for(int i = transform_count-1; i >= 0; i--) {
        switch(transforms[i]) {
            case PREDICTOR_TRANSFORM:
                apply_predictors(&image,&transform_predictor_subimage,transform_predictor_block_scale);
                break;
            case COLOUR_TRANSFORM:
                apply_colour_transform(&image,&transform_colour_subimage,transform_colour_block_scale);
                break;
            case SUBTRACT_GREEN_TRANSFORM:
                apply_subtract_green(&image);
                break;
            default:
                printf("Not implemented transform: %s\n",transform_names[transforms[i]]);
                exit(1);
        }
    }
    write_image(&image,"output");
}