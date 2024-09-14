#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <stdbool.h>

#define err(x) puts(x);exit(1);

const uint8_t use_wave = true;

const char* channel_descriptions[] = {
    "1 (mono)",
    "2 (left, right)",
    "3 (left, right, center)",
    "4 (front left, front right, back left, back right)",
    "5 (front left, front right, front center, back left, back right)",
    "6 (front left, front right, front center, LFE, back left, back right)",
    "7 (front left, front right, front center, LFE, back center, side left, side right)",
    "8 (front left, front right, front center, LFE, back left, back right, side left, side right)",
    "2 (left/side)",
    "2 (right/side)",
    "2 (mid/side)"
};

const char* picture_types[] = {
    "Other",
    "32x32 icon",
    "General icon",
    "Front cover",
    "Back cover",
    "Linear notes page",
    "Media label",
    "Lead artist / performer",
    "Artist / performer",
    "Conductor",
    "Band / orchestra",
    "Composer",
    "Lyricist",
    "Recording location",
    "During recording",
    "During production",
    "Screen capture",
    "Bright coloured fish",
    "Illustration",
    "Band / artist logotype",
    "Publisher / studio logotype"
};
const int64_t fixed_prediction_data[15] = {
    0,5,6,8,11,
    1,
    2,-1,
    3,-3,1,
    4,-6,4,-1
};


typedef struct {
    uint16_t minimum_block_size;
    uint16_t maximum_block_size;
    uint32_t minimum_frame_size;
    uint32_t maximum_frame_size;
    uint32_t sample_rate;
    uint64_t sample_count;
    uint8_t channel_count;
    uint8_t bit_depth;
    uint8_t read;
} StreamInfo;

int64_t predict(const int64_t* audio, const int64_t* pred, const int8_t order, const int8_t right_shift) {
    int64_t output = 0;
    for(int i = 0; i < order; i++) {
        output += audio[-1-i] * pred[i];
    }
    return output >> right_shift;
}

typedef struct {
    uint8_t* data;
    uint64_t current_read;
} BitstreamState;

uint8_t read_bit(BitstreamState* state) {
    uint8_t output = (state->data[state->current_read>>3] >> (~state->current_read&0x7))&1;
    state->current_read++;
    //printf("Read bit: %d\n",output);
    return output;
}
uint64_t read_bits(BitstreamState* state, uint8_t bit_count) {
    uint64_t output = 0;
    for(int i = 0; i < bit_count; i++) {
        output <<= 1;
        output |= (state->data[state->current_read>>3] >> (~state->current_read&0x7))&1;
        state->current_read++;
    }
    //printf("Read %d bits: %lld\n",bit_count,output);
    return output;
}
int64_t read_bits_signed(BitstreamState* state, uint8_t bit_count) {
    int64_t x = read_bits(state, bit_count);
    if(x >= 1<<(bit_count-1)) {
        return x-(1<<bit_count);
    } else {
        return x;
    }
}
uint64_t read_unary(BitstreamState* state) {
    uint64_t output = 0;
    while(!read_bit(state)) {output++;}
    return output;
}

void predict_subframe(BitstreamState* state, int64_t* channel_data, uint32_t block_size, uint8_t order, const int64_t* qlp_coeffs, uint8_t qlp_rightshift) {
    if(read_bit(state)) {err("Error: invalid bitstream");}
    uint8_t rice_parameter_length = read_bit(state)+4;
    uint8_t partition_order = read_bits(state,4);
    if((block_size&((1<<partition_order)-1)) || (block_size >> partition_order <= order)) {err("Error: impossible partition order");}
    int bucket_remaining = -1;
    uint8_t rice_parameter;
    bool rice_escaped;
    for(int i = order; i < block_size; i++) {
        if(bucket_remaining <= 0) {
            rice_parameter = read_bits(state,rice_parameter_length);
            rice_escaped = rice_parameter == (1<<rice_parameter_length)-1;
            if(rice_escaped) {
                rice_parameter = read_bits(state,5);
            }
            if(bucket_remaining == -1) {
                bucket_remaining = (block_size >> partition_order) - order;
            } else {
                bucket_remaining = block_size >> partition_order;
            }
        }
        int64_t residual;
        if(rice_escaped) {
            residual=read_bits_signed(state,rice_parameter);
        } else {
            uint64_t quotient = read_unary(state);
            uint64_t remainder = read_bits(state,rice_parameter);
            residual = quotient << rice_parameter | remainder;
            if(residual & 1) {
                residual = -((residual>>1) + 1);
            } else {
                residual >>= 1;
            }
        }
        channel_data[i] = residual + predict(channel_data+i, qlp_coeffs, order, qlp_rightshift);
        bucket_remaining--;
    }
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        exit(1);
    }
    FILE* file = fopen(argv[1],"rb");
    if(file == NULL) {err("Error: no such file or directory");}
    fseek(file,0,SEEK_END);
    long file_length = ftell(file);
    fseek(file,0,SEEK_SET);
    uint8_t* file_data = malloc(file_length);
    if(file_data == NULL) {err("Error: unable to allocate memory");}
    if(fread(file_data,1,file_length,file) != file_length) {err("Error: unable to read full file");}
    fclose(file);
    if(memcmp(file_data,"fLaC",4)!=0) {err("Error: Not a FLAC file!");}
    StreamInfo stream_info;
    long file_offset = 4;
    stream_info.read = 0;
    while(true) {
        uint8_t block_type = file_data[file_offset]&0x7f;
        bool is_last_block = file_data[file_offset]>=0x80;
        uint32_t block_size = 0;
        block_size |= file_data[file_offset+1];
        block_size <<= 8;
        block_size |= file_data[file_offset+2];
        block_size <<= 8;
        block_size |= file_data[file_offset+3];        
        uint8_t* block_data = file_data + file_offset + 4;
        if(file_offset == 4) {
            if(block_type != 0) {err("Error: missing or incorrectly placed Streaminfo block");}
            stream_info.read = 1;
            stream_info.minimum_block_size = block_data[0]<<8 | block_data[1];
            stream_info.maximum_block_size = block_data[2]<<8 | block_data[3];
            stream_info.minimum_frame_size = block_data[4]<<16 | block_data[5]<<8 | block_data[6];
            stream_info.maximum_frame_size = block_data[7]<<16 | block_data[8]<<8 | block_data[9];
            stream_info.sample_rate = block_data[10]<<12 | block_data[11]<<4 | (block_data[12]>>4);
            stream_info.channel_count = 1 + (block_data[12]>>1) & 0x7;
            stream_info.bit_depth = 1 + (((block_data[12]&1)<<4) | block_data[13]>>4);
            stream_info.sample_count = ((uint64_t)block_data[13]&0xf)<<32 | block_data[14]<<24 | block_data[15]<<16 | block_data[16]<<8 | block_data[16];
            printf("File info:\n");
            printf("Sample rate: %d\n",stream_info.sample_rate);
            printf("Length: %llu samples (%f seconds)\n",stream_info.sample_count,(stream_info.sample_count/(double)stream_info.sample_rate));
            printf("Channels: %d\n",stream_info.channel_count);
            printf("Bit depth: %d\n",stream_info.bit_depth);
            printf("Block sizes: [%hd, %hd]\n",stream_info.minimum_block_size, stream_info.maximum_block_size);
            printf("Frame sizes: [%d, %d]\n",stream_info.minimum_frame_size, stream_info.maximum_frame_size);
        } else {
            if(block_type == 0) {
                err("Error: misordered stream info block");
            } else if(block_type == 4) {
                uint32_t vendor_length = block_data[0] | block_data[1]<<8 | block_data[2]<<16 | block_data[3]<<24;
                uint32_t comment_data_pointer = 4;
                printf("Vendor: ");
                fwrite(block_data+comment_data_pointer,vendor_length,1,stdout);
                printf("\n");
                comment_data_pointer += vendor_length;
                uint32_t comment_count = block_data[comment_data_pointer] | block_data[comment_data_pointer+1]<<8 | block_data[comment_data_pointer+2]<<16 | block_data[comment_data_pointer+3]<<24;
                comment_data_pointer += 4;
                for(int i = 0; i < comment_count; i++) {
                    uint32_t comment_lengh = block_data[comment_data_pointer] | block_data[comment_data_pointer+1]<<8 | block_data[comment_data_pointer+2]<<16 | block_data[comment_data_pointer+3]<<24;
                    comment_data_pointer += 4;
                    fwrite(block_data+comment_data_pointer,comment_lengh,1,stdout);
                    comment_data_pointer += comment_lengh;
                    printf("\n");
                }
            } else if(block_type == 6) {
                printf("Attached picture: ");
                uint32_t image_type = block_data[0]<<24 | block_data[1]<<16 | block_data[2]<<8 | block_data[3];
                if(image_type <= 20) {
                    printf("%s",picture_types[image_type]);
                } else {
                    printf("Unknown with id %d",image_type);
                }
                uint32_t image_type_length = block_data[4]<<24 | block_data[5]<<16 | block_data[6]<<8 | block_data[7];
                printf(" (");
                fwrite(block_data+8,image_type_length,1,stdout);
                printf(")");
                uint32_t image_block_index = 8 + image_type_length;
                uint32_t description_length = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                image_block_index += 4;
                uint32_t image_width = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                image_block_index += 4;
                uint32_t image_height = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                image_block_index += 4;
                uint32_t colour_depth = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                image_block_index += 4;
                uint32_t palette_size = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                image_block_index += 4;
                uint32_t image_filesize = block_data[image_block_index]<<24 | block_data[image_block_index+1]<<16 | block_data[image_block_index+2]<<8 | block_data[image_block_index+3];
                if(image_width != 0 && image_height != 0) {
                    printf(" [%dx%d]",image_width, image_height);
                }
                printf(", %d bytes\n",image_filesize);
            } else {}
        }

        file_offset += block_size + 4;
        if(is_last_block) break;
    }
    if(stream_info.read == 0) {err("Error: no streaminfo found!");}

    char* filename = malloc(strlen(argv[1] + 10));
    if(use_wave) {
        sprintf(filename,"%s.wav",argv[1]);
    } else {
        sprintf(filename,"%s.dat",argv[1]);
    }
    FILE* output_file = fopen(filename, "wb");
    free(filename);
    if(use_wave) {
        fseek(output_file,44,SEEK_SET);
    }

    uint8_t blocking_strat = 2;
    uint32_t wave_data_length = 0;

    while(file_offset < file_length) {
        if(file_data[file_offset++]!=0xff) continue;
        switch(blocking_strat) {
            case 0:
            case 1:
                if(file_data[file_offset++]!=(0b11111000|blocking_strat)) continue;
                break;
            case 2:
                blocking_strat = file_data[file_offset++] & 0x1;
                break;
        }
        uint64_t frame_start = file_offset-2;
        
        uint8_t block_size_signal = file_data[file_offset] >> 4;
        uint8_t sample_rate_signal = file_data[file_offset] & 0xf;
        file_offset++;

        uint8_t channel_layout_signal = file_data[file_offset] >> 4;
        uint8_t bit_depth_signal = (file_data[file_offset] & 0xf) >> 1;
        file_offset++;

        uint64_t block_id = file_data[file_offset++];
        uint8_t more_bytes = 0;
        if(block_id < 0b10000000) {
            more_bytes=0;
        } else if(block_id < 0b11000000) {
            err("Error: cannot read coded number!");
        } else if(block_id < 0b11100000) {
            more_bytes=1;
            block_id &= 0b00011111;
        } else if(block_id < 0b11110000) {
            more_bytes=2;
            block_id &= 0b00001111;
        } else if(block_id < 0b11111000) {
            more_bytes=3;
            block_id &= 0b00000111;
        } else if(block_id < 0b11111100) {
            more_bytes=4;
            block_id &= 0b00000011;
        } else if(block_id < 0b11111110) {
            more_bytes=5;
            block_id &= 0b00000001;
        } else if(block_id == 0b11111110) {
            more_bytes=6;
            block_id = 0;
        } else {
            err("Error: cannot read coded number!");
        }
        for(int i = 0; i < more_bytes; i++) {
            block_id <<= 6;
            block_id |= file_data[file_offset++]&0x3f;
        }

        uint32_t block_size = 0;
        switch(block_size_signal) {
            case 0: err("Error: invalid block size!");
            case 1: block_size=192;break;
            case 2: block_size=576;break;
            case 3: block_size=1152;break;
            case 4: block_size=2304;break;
            case 5: block_size=4608;break;
            case 6: block_size=file_data[file_offset++];break;
            case 7: block_size=file_data[file_offset]<<8|file_data[file_offset+1];file_offset+=2;break;
            default: block_size=(1<<block_size_signal);
        }
        
        uint32_t sample_rate = 0;
        switch(sample_rate_signal) {
            case 0: sample_rate=stream_info.sample_rate;break;
            case 1: sample_rate=88200;break;
            case 2: sample_rate=176400;break;
            case 3: sample_rate=192000;break;
            case 4: sample_rate=8000;break;
            case 5: sample_rate=16000;break;
            case 6: sample_rate=22050;break;
            case 7: sample_rate=24000;break;
            case 8: sample_rate=32000;break;
            case 9: sample_rate=44100;break;
            case 10: sample_rate=48000;break;
            case 11: sample_rate=96000;break;
            case 12: sample_rate=1000*file_data[file_offset++];break;
            case 13: sample_rate=file_data[file_offset]<<8|file_data[file_offset+1];file_offset+=2;break;
            case 14: sample_rate=10*(file_data[file_offset]<<8|file_data[file_offset+1]);file_offset+=2;break;
            case 15: err("Error: forbidden sample rate!"); 
        }
        
        uint8_t bit_depth = 0;
        switch(bit_depth_signal) {
            case 0: bit_depth=stream_info.bit_depth;break;
            case 1: bit_depth=8;break;
            case 2: bit_depth=12;break;
            case 3: err("Error: forbidden bit depth!"); 
            case 4: bit_depth=16;break;
            case 5: bit_depth=20;break;
            case 6: bit_depth=24;break;
            case 7: bit_depth=32;break;
        }
        
        uint8_t channel_count = channel_layout_signal + 1;
        if(channel_layout_signal >= 8) {
            if(channel_layout_signal > 10) {
                err("Error: invalid channel layout!");
            }
            channel_count = 2;
        }

        uint8_t block_crc=file_data[file_offset];
        file_data[file_offset]=0;
        uint8_t calculated_crf = 0;
        for(int i = frame_start; i < file_offset; i++) {
            for(int bit = 0; bit < 8; bit++) {
                if((file_data[i] & (0x80>>bit)) == 0) {
                    continue;
                }
                file_data[i] ^= (0x107>>(bit+1))&0xff;
                file_data[i+1] ^= (0x107<<(7-bit))&0xff;
            }
        }
        calculated_crf = file_data[file_offset];
        
        if(calculated_crf != block_crc) {err("Error: invalid CRC");}
        if(bit_depth!=stream_info.bit_depth) {err("Error: stream info bit depth mismatch!");}
        if(block_size > stream_info.maximum_block_size) {err("Error: stream info invalid block size!");}
        if(sample_rate != stream_info.sample_rate) {err("Error: stream info sample rate mismatch!");}
        if(channel_count != stream_info.channel_count) {err("Error: stream info channel count mismatch!");}
        file_offset++;
    
        uint64_t seconds = 0;
        if(blocking_strat) {
            seconds = block_id / stream_info.sample_rate;
        } else {
            seconds = block_id * block_size / stream_info.sample_rate;
        }
        printf("Processing %llu seconds (%llu)\n",seconds, block_id);
        fflush(stdout);
        

        BitstreamState state = {
            .data = (file_data + file_offset),
            .current_read = 0
        };
        int64_t qlp_coeffs[32];
        int64_t* audio_data = malloc(8*block_size*channel_count);
        for(int i = 0; i < channel_count; i++) {
            if(read_bit(&state)) {
                err("Error: lost subframe sync!");
            }
            uint8_t prediction_mode = read_bits(&state,6);
            if(read_bit(&state)) {
                err("Error: wasted bits are not yet supported");
            }
            int64_t* channel_data = audio_data + (block_size * i);
            uint8_t sample_bits = bit_depth;
            printf("Prediction mode: %d\n",prediction_mode);
            if(channel_layout_signal == 8 && i == 1) sample_bits += 1;
            if(channel_layout_signal == 9 && i == 0) sample_bits += 1;
            if(channel_layout_signal == 10 && i == 1) sample_bits += 1;
            if(prediction_mode == 0) {
                int64_t data = read_bits(&state,sample_bits);
                for(int i = 0; i < block_size; i++) {
                    channel_data[i] = data;
                }
            } else if(prediction_mode >= 8 && prediction_mode <= 12) {
                uint8_t order = prediction_mode - 8;
                for(int i = 0; i < order; i++) {
                    channel_data[i] = read_bits_signed(&state,sample_bits);
                }
                predict_subframe(&state, channel_data, block_size, order, fixed_prediction_data+fixed_prediction_data[order], 0);
            } else if(prediction_mode >= 32) {
                uint8_t order = prediction_mode - 31;
                for(int i = 0; i < order; i++) {
                    channel_data[i] = read_bits_signed(&state,sample_bits);
                    // printf("warmup[%d]: %lld\n",i,channel_data[i]);
                }
                uint8_t qlp_precision = read_bits(&state,4)+1;
                uint8_t qlp_rightshift = read_bits(&state,5);
                for(int i = 0; i < order; i++) {
                    qlp_coeffs[i] = read_bits_signed(&state,qlp_precision);
                   // printf("qlp_coeff[%d]: %lld\n",i,qlp_coeffs[i]);
                }
                predict_subframe(&state, channel_data, block_size, order, qlp_coeffs, qlp_rightshift);
            } else {
                printf("Unsupported prediction mode: %d\n",prediction_mode);
            }
            uint8_t sample_depth = 0;
        }
        file_offset += ((state.current_read+7)>>3) + 2;
        if(channel_layout_signal == 8) {
            for(int i = 0; i < block_size; i++) {
                audio_data[block_size+i] = audio_data[i] - audio_data[block_size+i];
            }
        }
        if(channel_layout_signal == 9) {
            for(int i = 0; i < block_size; i++) {
                audio_data[i] += audio_data[block_size+i];
            }
        }
        if(channel_layout_signal == 10) {
            for(int i = 0; i < block_size; i++) {
                int64_t side = audio_data[block_size+i];
                int64_t mid = audio_data[i] << 1;
                mid |= (side & 1);
                audio_data[i]=(mid+side)>>1;
                audio_data[block_size+i]=(mid-side)>>1;
            }
        }
        for(int i = 0; i < block_size; i++) {
            for(int j = 0; j < channel_count; j++) {
                audio_data[j*block_size+i] <<= (((bit_depth+7)>>3)<<3)-bit_depth;
                fwrite(&audio_data[j*block_size+i],(bit_depth+7)>>3,1,output_file);
            }
            wave_data_length += ((bit_depth+7)>>3) * channel_count;
        }
        fflush(output_file);
        free(audio_data);
    }
    printf("\n");
    if(use_wave) {
        fseek(output_file,0,SEEK_SET);
        uint32_t data[11] = {
            0x46464952, // "RIFF"
            wave_data_length+36, // RIFF size
            0x45564157, // "WAVE"
            0x20746D66, // "fmt "
            16,  // fmt size
            stream_info.channel_count << 16 | 1, // Linear PCM, N ch
            stream_info.sample_rate,
            stream_info.sample_rate*stream_info.channel_count*((stream_info.bit_depth+7)>>3),
            stream_info.bit_depth << 16 | stream_info.channel_count*((stream_info.bit_depth+7)>>3),
            0x61746164, // "data"
            wave_data_length
        };
        fwrite(data,11,4,output_file);
    }
    fclose(output_file);
}