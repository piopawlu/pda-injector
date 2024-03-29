#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

#define POLY 0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256];

void init_crc64_table(void) {
    for (int i = 0; i < 256; i++) {
        uint64_t crc = i;
        crc <<= 56;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000000000000000ULL) {
                crc = (crc << 1) ^ POLY;
            } else {
                crc <<= 1;
            }
        }
        crc64_table[i] = crc;
    }
}

uint64_t crc64(uint64_t crc, const unsigned char *s, size_t l) {
    crc = ~crc;
    while (l--) {
        uint8_t byte = *s++;
        crc = crc64_table[(crc >> 56) ^ byte] ^ (crc << 8);
    }
    return ~crc;
}


int make_patch(const std::string original, const std::string modified)
{
    FILE* fa = fopen(original.c_str(), "rb");
    FILE* fb = fopen(modified.c_str(), "rb");

    if (!fa || !fb) {
        printf("Could not open file A:%p or B:%p\n", fa, fb);
        return -2;
    }

    fseek(fa, 0, SEEK_END);
    fseek(fb, 0, SEEK_END);

    const auto size_a = ftell(fa);
    const auto size_b = ftell(fb);

    const auto max_size = std::max(size_a, size_b);

    std::vector<uint8_t> buf_a(max_size);
    std::vector<uint8_t> buf_b(max_size);

    std::fill(buf_a.begin() ,buf_a.end(), 0);
    std::fill(buf_b.begin(), buf_b.end(), 0);

    fseek(fa, 0, SEEK_SET);
    fread(buf_a.data(), size_a, 1, fa);
    fclose(fa);

    fseek(fb, 0, SEEK_SET);
    fread(buf_b.data(), size_b, 1, fb);
    fclose(fb);

    const auto crc_a = crc64(0, buf_a.data(), max_size);
    const auto crc_b = crc64(0, buf_b.data(), max_size);

    printf("$SIZE1:%d\n", size_a);
    printf("$SIZE2:%d\n", max_size);
    printf("$CRC1:%016I64x\n", crc_a);
    printf("$CRC2:%016I64x\n", crc_b);
    printf("$BEGIN\n");

    bool writing = false;

    for(auto i=0; i<max_size; i++) {
        if (buf_a[i] == buf_b[i]) {
            if(writing) {
                writing = false;
                putchar('\n');
            }
            continue;
        }

        if (!writing) {
            printf("#%x:", i);
            writing = true;
        }
        printf("%02x", buf_b[i]);
        buf_a[i] = buf_b[i];
    }

    if (writing) {
        putchar('\n');
    }

    printf("$END\n");

    const auto crc_c = crc64(0, buf_a.data(), max_size);
    if (crc_c != crc_b) {
        printf("ERROR - Patch generation failed!\n");
        return -1;
    }

    return 0;
}

uint8_t from_hex(uint8_t H, uint8_t L) {
    return  (H >= 'a' ? ((H - 'a' + 10) << 4) : ((H - '0') << 4)) |
           (L >= 'a' ? ((L - 'a' + 10)) : (L - '0'));
}

int apply_patch(const std::string target_file, const std::string patch_file)
{
    FILE* fdst = fopen(target_file.c_str(), "rb");
    FILE* fpatch = fopen(patch_file.c_str(), "rb");

    if (!fdst || !fpatch) {
        printf("Could not open target file A:%p or patch B:%p\n", fdst, fpatch);
        return -1;
    }

    long size_original = 0;
    long size_patched = 0;

    if(!fscanf(fpatch, " $SIZE1:%d", &size_original)) {
        printf("Failed to read $SIZE1 from patch file!\n");
        return -1;
    }

    fseek(fdst, 0, SEEK_END);
    const auto size_in = ftell(fdst);
    if (size_in != size_original) {
        printf("Destination file size does not match the patch definition (%d != %d)!\n", size_in, size_original);
        return -1;
    }

    if(!fscanf(fpatch, " $SIZE2:%d", &size_patched)) {
        printf("Failed to read $SIZE2 from patch file!\n");
        return -1;
    }

    if (size_patched < size_original) {
        printf("Invalid $SIZE2 in patch file, %d must be greater or equal to %d!\n", size_patched, size_original);
    }

    uint64_t crc1 = 0;
    uint64_t crc2 = 0;

    if(!fscanf(fpatch, " $CRC1:%I64x", &crc1) || crc1 == 0) {
        printf("Failed to read $CRC1 from patch file!\n");
        return -1;
    }

    if(!fscanf(fpatch, " $CRC2:%I64x", &crc2) || crc1 == 0) {
        printf("Failed to read $CRC1 from patch file!\n");
        return -1;
    }

    printf("Reading input file\n");

    std::vector<uint8_t> buf_dst(size_patched);

    std::fill(buf_dst.begin() ,buf_dst.end(), 0);

    fseek(fdst, 0, SEEK_SET);
    fread(buf_dst.data(), size_original, 1, fdst);
    fclose(fdst);
    

    printf("Verifying input CRC\n");

    const uint64_t crc_in = crc64(0, buf_dst.data(), size_patched);

    if (crc_in != crc1) {
        printf("Destination file CRC does not match %I64x != %I64x!\n", crc_in, crc1);
        return -1;
    }

    std::string line;
    line.reserve(2048);

    printf("Patching %s\n", target_file.c_str());

    bool started = false;
    bool finished = false;

    do {
        const auto c = fgetc(fpatch);
        if (feof(fpatch)) {
            printf("Patch definition ended without an $END mark!");
            return -1;
        }

        if(c == '\n' || c == '\r') {
            if( line[0] == '#') {
                if (!started) {
                    printf("Patch bytes started without $BEGIN tag!\n");
                    return -1;
                }

                char * colon = nullptr;
                long addr = strtol(line.c_str() + 1, &colon, 16);

                if(colon == nullptr || colon[0] != ':') {
                    printf("Invalid end mark (%c)!\n", colon[0]);
                    return -1;
                }

                const auto * bytes = colon + 1;
                if(strlen(bytes) % 2) {
                    printf("Invalid number of hex digits in: %s!\n", bytes);
                }

                printf("Patching at %x: ", addr);

                for(const char * b = bytes; *b; b+=2, ++addr) {
                    if (addr > size_patched) {
                        printf("Invalid address, outside of binary %x!\n", addr);
                        return -1;
                    }

                    buf_dst[addr] = from_hex(b[0], b[1]);
                    putchar('.');
                }
                putchar('\n');


            } else if (line == "$BEGIN") {
                started = true;
            }  else if (line == "$END") {
                finished = true;
            } else if(line.size() > 0) {
                printf("UNKNOWN COMMAND: %s\n", line.c_str());
                return -1;
            }

            line.clear();
        } else {
            line.push_back(static_cast<char>(c));
        }
    } while(finished == false);

    fclose(fpatch);

    printf("Checking resulting file CRC\n");

    const auto crc_out = crc64(0, buf_dst.data(), size_patched);
    if (crc_out != crc2) {
        printf("Patched file CRC does not match %I64x != %I64x!\n", crc_out, crc2);
        return -12;
    }

    std::string patched_file_name = target_file;

    if(const auto ext_pos = patched_file_name.find_last_of('.'); ext_pos != std::string::npos) {
        patched_file_name.insert(ext_pos, ".patched");
    } else {
        patched_file_name.append(".patched");
    }

    printf("Writing to %s resulting file CRC\n", patched_file_name.c_str());

    FILE* f_out = fopen(patched_file_name.c_str(), "wb");
    if (f_out == nullptr) {
        printf("Failed to open destination file for writing!\n");
        return -13;
    }

    if (fwrite(buf_dst.data(), buf_dst.size(), 1, f_out) != 1) {
        printf("Failed to write to destination file!\n");
        return -14;
    }
    fclose(f_out);
    printf("Patching complete!");

    return 0;
}

int main(int argc, char** argv)
{
    if(argc != 4) {
        printf("cpatch diff original.exe modified.exe\n");
        printf("cpatch patch original.exe patch.txt\n");
        return -1;
    }

    init_crc64_table();

    if(argv[1] == std::string("diff")) {
        return make_patch(argv[2], argv[3]);
    } else if(argv[1] == std::string("patch")) {
        return apply_patch(argv[2], argv[3]);
    } else {
        printf("Unknown operation %s\n", argv[2]);
        return -1;
    }
}