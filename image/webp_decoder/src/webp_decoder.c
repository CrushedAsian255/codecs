#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

#define err(x) {puts(x);exit(1);}
#define assert(x,y) {if(!(x))err(y)}

typedef struct {
    uint8_t* data;
    uint64_t current_read;
} BitstreamState;
uint8_t read_bit(BitstreamState* state) {
    uint8_t output = (state->data[state->current_read>>3] >> (state->current_read&0x7))&1;
    state->current_read++;
    return output;
}
uint64_t read_bits(BitstreamState* state, uint8_t bit_count) {
    uint64_t output = 0;
    for(int i = 0; i < bit_count; i++) {
        output |= read_bit(state) << i;
    }
    return output;
}

typedef struct {
    uint32_t* data;
    uint16_t width;
    uint16_t height;
} ImageData;
ImageData malloc_new_image(uint16_t width, uint16_t height) {
    ImageData output = {
        .data = malloc(width*height*4),
        .width = width,
        .height = height
    };
    memset(output.data,0,width*height*4);
    return output;
}
void write_image(const ImageData* image_data, const char* output_file_name) {
    uint8_t has_alpha = 0;
    for(int i = 0; i < image_data->width*image_data->height; i++) {
        if((image_data->data[i]&0xff000000) != (image_data->data[0]&0xff000000)) {
            has_alpha = 1;
            break;
        }
    }
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

typedef enum {
    PREDICTOR_TRANSFORM,
    COLOR_TRANSFORM,
    SUBTRACT_GREEN_TRANSFORM,
    COLOR_INDEXING_TRANSFORM
} WebPTransformType;

typedef struct {
    WebPTransformType transform_type;
    void* transform_data;
} WebPTransform;

const char* transform_names[4] = {
    "Predictor",
    "Colour",
    "Subtract Green",
    "Colour Index"
};

void decode_prefix_codes(BitstreamState* bitstream, uint32_t prefix_code_groups, uint32_t color_cache_size) {
    for(int i = 0; i < prefix_code_groups*5; i++) {
        uint8_t prefix_code_type = read_bit(bitstream);
        if(prefix_code_type) {
            uint8_t num_code_lengths = 4 + read_bits(bitstream,4);
            printf("%d\n",num_code_lengths);
            int kCodeLengthCodeOrder[19] = {
                17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
            };
            int code_length_code_lengths[19] = { 0 };
            for (i = 0; i < num_code_lengths; ++i) {
                code_length_code_lengths[kCodeLengthCodeOrder[i]] = read_bits(bitstream,3);
                printf("%d: %d\n",i,code_length_code_lengths[i]);
            }
            uint32_t max_alphabet_size = (i%5==0)?(256+24+color_cache_size):(i%5==4?40:256);
            uint32_t max_symbol = max_alphabet_size;
            if(read_bit(bitstream)) {
                int length_nbits = 2 + 2 * read_bits(bitstream,3);
                max_symbol = 2 + read_bits(bitstream,length_nbits);
            }
            printf("%d %d\n",max_alphabet_size,max_symbol);
        }
        break;
    }
}

void decode_entropy_coded_image(BitstreamState* bitstream, ImageData* image) {
    uint32_t colour_cache_size = 0;
    if(read_bit(bitstream)) {
        colour_cache_size = 1<<read_bits(bitstream,4);
    }
    printf("Colour cache: %d\n",colour_cache_size);
    decode_prefix_codes(bitstream,1,colour_cache_size);
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
    BitstreamState file = {
        .data = file_data,
        .current_read = 0,
    };
    assert(read_bits(&file,32)==(*(uint32_t*)"RIFF"),"Error: invalid RIFF header"); 
    assert(read_bits(&file,32)==file_length-8, "Error: invalid RIFF header");
    assert(read_bits(&file,32)==(*(uint32_t*)"WEBP"),"Error: invalid WebP header"); 
    assert(read_bits(&file,32)==(*(uint32_t*)"VP8L"),"Error: not a lossless WebP"); 
    assert(read_bits(&file,32)==file_length-20,"Error: invalid WebP header");    
    assert(read_bits(&file,8)==0x2f,"Error: invalid WebP header");  
    uint16_t image_width = read_bits(&file,14);
    uint16_t image_height = read_bits(&file,14);
    uint8_t use_alpha = read_bits(&file,1);
    printf("Image dimensions: %d x %d %s\n",image_width,image_height,use_alpha?"with alpha":"");   
    assert(read_bits(&file,3)==0,"Error: invalid WebP version");
    WebPTransform transforms[4];
    uint8_t transform_count = 0;
    while(read_bit(&file)) {
        assert(transform_count < 4,"Error: too many image transforms");
        WebPTransformType transform_type = read_bits(&file,2);
        printf("Transform %s\n",transform_names[transform_type]);
        void* transform_data;
        switch(transform_type) {
            case SUBTRACT_GREEN_TRANSFORM:
                break;
            case PREDICTOR_TRANSFORM: {
                uint32_t block_size = (1<<(read_bits(&file,3)+2));
                uint32_t subimage_width = (image_width + block_size - 1) / block_size;
                uint32_t subimage_height = (image_height + block_size - 1) / block_size;
                ImageData subimage = malloc_new_image(subimage_width,subimage_height);
                decode_entropy_coded_image(&file,&subimage);
                write_image(&subimage,"predictors");
            }
            default:
                printf("Not implemented transform: %s\n",transform_names[transform_type]);
                exit(1);
        }
        transforms[transform_count].transform_type = transform_type;
        transforms[transform_count].transform_data = transform_data;
        transform_count++;
    }
    ImageData image = malloc_new_image(image_width,image_height);
    for(int i = transform_count-1; i >= 0; i--) {
        switch(transforms[i].transform_type) {
            default:
                printf("Not implemented transform: %s\n",transform_names[transforms[i].transform_type]);
                exit(1);
        }
    }
    write_image(&image,"output");
}