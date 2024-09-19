#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

#define err(x) {printf("Error at line %d: %s\n",__LINE__,x);exit(1);}
#define assert(x,y) {if(!(x)){printf("Assertion failure at line %d: %s\n",__LINE__,y);exit(1);}}
#define todo(x) {printf("Not implemented at line %d: %s\n",__LINE__,x);exit(1);}

typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t a;
} pixel_t;

typedef struct {
    pixel_t* data;
    uint16_t width;
    uint16_t height;
    uint8_t bit_depth;
} image_t;
image_t malloc_new_image(uint16_t width, uint16_t height, uint8_t bit_depth) {
    image_t output = {
        .data = malloc(width*height*sizeof(pixel_t)),
        .width = width,
        .height = height,
        .bit_depth = bit_depth
    };
    memset(output.data,0,width*height*sizeof(pixel_t));
    return output;
}

typedef struct {
    uint8_t creator[4];
    uint16_t width;
    uint16_t height;
    uint8_t is_444;
    uint8_t colour_primaries;
    uint8_t transfer_function;
    uint8_t colour_space;
    uint8_t qmat_luma[64];
    uint8_t qmat_chroma[64];
} frame_header;
void write_image(image_t image_data, const char* output_file_name) {
    uint8_t has_alpha = 0;
    for(int i = 0; i < image_data.width*image_data.height; i++) {
        if(image_data.data[i].a != image_data.data[0].a) {
            has_alpha = 1;
            break;
        }
        if(image_data.data[i].a >= 1<<image_data.bit_depth) {
            printf("Clipping @ %d.a value %d!\n",i,image_data.data[i].a);
        }
        if(image_data.data[i].r >= 1<<image_data.bit_depth) {
            printf("Clipping @ %d.r value %d!\n",i,image_data.data[i].r);
        }
        if(image_data.data[i].g >= 1<<image_data.bit_depth) {
            printf("Clipping @ %d.g value %d!\n",i,image_data.data[i].g);
        }
        if(image_data.data[i].b >= 1<<image_data.bit_depth) {
            printf("Clipping @ %d.b value %d!\n",i,image_data.data[i].b);
        }
    }
    printf("Image %s: %d x %d%s\n",output_file_name,image_data.width,image_data.height,has_alpha?" with alpha":"");
    char* output_name_buffer = malloc(strlen(output_file_name) + 20);
    
    sprintf(output_name_buffer,"%s.ppm",output_file_name);
    FILE* output_file = fopen(output_name_buffer,"wb");
    fprintf(output_file,"P6\n%d %d\n%d\n",image_data.width,image_data.height,(1<<image_data.bit_depth)-1);
    for(int i = 0; i < image_data.width*image_data.height; i++) {
        uint8_t data;
        fwrite(&image_data.data[i],2,3,output_file);
    }
    fclose(output_file);
    free(output_name_buffer);
}

const char* colour_primaries[23] = {
    "Unknown",
    "BT.709",
    "Unspecified",
    "BT.420 M",
    "BT.420 BG",
    "SMPTE 170 M",
    "SMPTE 240 M",
    "Film",
    "BT.2020",
    "SMPTE 428-1",
    "SMPTE 431-1",
    "SMPTE 422-1",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "JEDEC P22",
};
const char* transfer_function[19] = {
    "Unknown",
    "BT.709",
    "Unspecified",
    "Unknown",
    "BT.420 M",
    "BT.420 BG",
    "SMPTE 170 M",
    "SMPTE 240 M",
    "Linear",
    "Log",
    "Log sqrt",
    "IEC 61966-2-4",
    "BT.1361",
    "IEC 61966-2-1",
    "BT.2020 10-bit",
    "BT.2020 12-bit",
    "SMPTE 2084",
    "SMPTE 428-1",
    "ARIB STD-B67",
};
const char* colour_space[18] = {
    "RGB",
    "BT.709",
    "Unspecified",
    "Unknown",
    "FCC",
    "BT.470 GB",
    "SMPTE 170 M",
    "SMPTE 240 M",
    "YCgCo",
    "BT.2020 NCL",
    "BT.2020 CL",
    "SMPTE 2085",
    "Chroma-derived NCL",
    "Chroma-derived CL",
    "ICtCp",
    "IPT-C2",
    "YCgCo (even add)",
    "YCgCo (odd add)",
};

frame_header read_frame_header(uint8_t *data, uint16_t* length) {
    frame_header hdr;

    uint16_t header_size = data[0]<<8|data[1];
    *length = header_size;
    
    uint16_t version = data[2]<<8|data[3];
    assert(version<=1,"Invalid version");
    memcpy(hdr.creator,data+4,4);

    hdr.width = data[8]<<8|data[9];
    hdr.height = data[10]<<8|data[11];
    hdr.is_444 = data[12]&0xc0;
    hdr.colour_primaries = data[14];
    hdr.transfer_function = data[15];
    hdr.colour_space = data[16];
    assert(data[17]==0,"");

    uint8_t flags = data[19];
    data += 20;

    if(flags & 1) {
        memcpy(hdr.qmat_luma,data,64);
        data += 64;
    } else {
        memset(hdr.qmat_luma,4,64);
    }

    if(flags & 2) {
        memcpy(hdr.qmat_chroma,data,64);
        data += 64;
    } else {
        memset(hdr.qmat_chroma,4,64);
    }
    return hdr;
}

int main(int argc, char* argv[]) {
    assert(argc >= 2, "No input file!");
    FILE* input_file = fopen(argv[1],"rb");
    assert(input_file != NULL, "Error: no such file or directory");
    fseek(input_file,0,SEEK_END);
    long file_length = ftell(input_file);
    fseek(input_file,0,SEEK_SET);
    uint8_t* _file_data = malloc(file_length);
    uint8_t* file_data = _file_data;
    assert(_file_data != NULL, "Error: unable to allocate memory");
    size_t read_data_count = fread(file_data,1,file_length,input_file);
    assert(read_data_count == file_length, "Error: unable to read complete file");
    fclose(input_file);
    uint32_t atom_size = file_data[0]<<24|file_data[1]<<16|file_data[2]<<8|file_data[3];
    printf("%ld\n",file_length);
    assert(atom_size<=file_length,"Atom not large enough");
    assert(memcmp(file_data+4,"icpf",4)==0,"Invalid header");
    file_data+=8;
    
    uint16_t header_size;
    frame_header frame_hdr = read_frame_header(file_data, &header_size);
    file_data += header_size;

    printf("Created by: %.4s\n",frame_hdr.creator);
    printf("Image dimensions: %dx%d\n",frame_hdr.width,frame_hdr.height);
    printf(
        "Colour primaries: %d | %s\n",
        frame_hdr.colour_primaries,
        frame_hdr.colour_primaries<=22?colour_primaries[frame_hdr.colour_primaries]:"Unknown"
    );
    printf(
        "Transfer function: %d | %s\n",
        frame_hdr.transfer_function,
        frame_hdr.transfer_function<=18?transfer_function[frame_hdr.transfer_function]:"Unknown"
    );
    printf(
        "Colour space: %d | %s\n",
        frame_hdr.colour_space,
        frame_hdr.colour_space<=17?colour_space[frame_hdr.colour_space]:"Unknown"
    );
    free(_file_data);
}