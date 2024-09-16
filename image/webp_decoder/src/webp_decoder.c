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
    COLOR_TRANSFORM,
    SUBTRACT_GREEN_TRANSFORM,
    COLOR_INDEXING_TRANSFORM
};
const char* transform_names[4] = {
    "Predictor",
    "Colour",
    "Subtract Green",
    "Colour Index"
};

struct prefix_code_entry {
    symbol_t decoded_symbol;
    uint8_t bits_real;
};
struct prefix_code {
    struct prefix_code_entry *table;
    symbol_t total_bits;
};
void print_prefix_code(const struct prefix_code code) {
    for(int i = 0; i < 1<<code.total_bits; i++) {
        printf("%d %d\n",code.table[i].bits_real, code.table[i].decoded_symbol);
    }
}

// ll prefix codes: low level code-length codes
static const int llcodes = 19;
static const int llcode_orders[llcodes] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
void read_code_complex(struct bitstream* bitstream, struct prefix_code* code, symbol_t alphabet_size) {
    todo("complex prefix code: not fully implemented");
    uint8_t code_lengths = read_bits(bitstream,4) + 4;
    uint8_t llcode_lengths[llcodes] = {0};
    for(int i = 0; i < code_lengths; i++) {
        llcode_lengths[llcode_orders[i]] = read_bits(bitstream,3);
    }
    for(int i = 0; i < llcodes; i++) {
        printf("%d: %d\n",i,llcode_lengths[i]);
    }
    symbol_t alphabet_limit = alphabet_size;
    if(read_bit(bitstream)) {
        alphabet_limit = read_bits(bitstream,read_bits(bitstream,3)*2 + 2);
    }
    printf("%d\n",alphabet_limit);
    assert(alphabet_limit <= alphabet_size, "Alphabet too big");
}
void read_code_simple(struct bitstream* bitstream, struct prefix_code* code, symbol_t alphabet_size) { 
    symbol_t multiple_symbols = read_bit(bitstream);
    code->total_bits = multiple_symbols;
    code->table = malloc((multiple_symbols+1)*sizeof(struct prefix_code));
    code->table[0].bits_real = multiple_symbols;
    code->table[0].decoded_symbol = read_bits(bitstream,read_bit(bitstream)?8:1);
    if(multiple_symbols) {
        code->table[1].bits_real = multiple_symbols;
        code->table[1].decoded_symbol = read_bits(bitstream,8);
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
        printf("Channel %d\n",i);
        print_prefix_code(prefix_group->codes[i]);
    }
}

void decode_image(struct bitstream* bitstream, struct image_data* image, bool is_main_image) {
    symbol_t colour_cache_size = 0;
    if(read_bit(bitstream)) {
        colour_cache_size = 1<<read_bits(bitstream,4);
    }
    pixel_t* colour_cache = malloc(4*colour_cache_size);
    memset(colour_cache,0,colour_cache_size*4);
    assert(colour_cache!=NULL,"Error allocating memory");
    printf("Colour cache size: %d\n",colour_cache_size);

    uint32_t prefix_group_count = 1;

    if(is_main_image && read_bit(bitstream)) {
        todo("Read full bitstream");
    }

    struct prefix_group* groups = malloc(sizeof(struct prefix_group)*prefix_group_count);
    assert(groups!=NULL,"Error allocating memory");

    for(int i = 0; i < prefix_group_count; i++) {
        decode_prefix_group(bitstream,&groups[i],colour_cache_size);
    }

    for(int i = 0; i < prefix_group_count; i++) {
        for(int j = 0; j < 5; j++) {
            free(groups[i].codes[j].table);
        }
    }
    free(colour_cache);
    free(groups);
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
    assert(read_bits(&file,32)==file_length-20,"Error: invalid WebP header");    
    assert(read_bits(&file,8)==0x2f,"Error: invalid WebP header");  
    uint16_t image_width = read_bits(&file,14) + 1;
    uint16_t image_height = read_bits(&file,14) + 1;
    uint8_t use_alpha = read_bits(&file,1);
    printf("Image dimensions: %d x %d %s\n",image_width,image_height,use_alpha?"with alpha":"");   
    assert(read_bits(&file,3)==0,"Error: invalid WebP version");
    enum transform_type transforms[4];
    uint8_t transform_count = 0;

    uint32_t transform_predictor_block_size = 0;
    struct image_data transform_predictor_subimage;

    while(read_bit(&file)) {
        assert(transform_count < 4,"Error: too many image transforms");
        enum transform_type transform_type = read_bits(&file,2);
        printf("Transform %s\n",transform_names[transform_type]);
        switch(transform_type) {
            case SUBTRACT_GREEN_TRANSFORM:
                break;
            case PREDICTOR_TRANSFORM: {
                transform_predictor_block_size = (1<<(read_bits(&file,3)+2));
                uint32_t subimage_width = ceil_div(image_width,transform_predictor_block_size);
                uint32_t subimage_height = ceil_div(image_height,transform_predictor_block_size);
                transform_predictor_subimage = malloc_new_image(subimage_width,subimage_height);
                decode_image(&file,&transform_predictor_subimage,false);
                write_image(&transform_predictor_subimage,"transform_predictor");
            }
            default:
                printf("Not implemented transform: %s\n",transform_names[transform_type]);
                exit(1);
        }
        transforms[transform_count] = transform_type;
        transform_count++;
    }
    struct image_data image = malloc_new_image(image_width,image_height);
    for(int i = transform_count-1; i >= 0; i--) {
        switch(transforms[i]) {
            default:
                printf("Not implemented transform: %s\n",transform_names[transforms[i]]);
                exit(1);
        }
    }
    write_image(&image,"output");
}