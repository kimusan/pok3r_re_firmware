#include "pok3r.h"
#include "zlog.h"
#include "zfile.h"
#include "zhash.h"

// XOR encryption/decryption key
// Found at 0x2188 in Pok3r flash
static const zbyte xor_key_bytes[] = {
    0xaa,0x55,0xaa,0x55,
    0x55,0xaa,0x55,0xaa,
    0xff,0x00,0x00,0x00,
    0x00,0xff,0x00,0x00,
    0x00,0x00,0xff,0x00,
    0x00,0x00,0x00,0xff,
    0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,
    0x0f,0x0f,0x0f,0x0f,
    0xf0,0xf0,0xf0,0xf0,
    0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,
    0x00,0x00,0x00,0x00,
};
static const zu32 xor_key_words[] = {
    0x55aa55aa,
    0xaa55aa55,
    0x000000ff,
    0x0000ff00,
    0x00ff0000,
    0xff000000,
    0x00000000,
    0xffffffff,
    0x0f0f0f0f,
    0xf0f0f0f0,
    0xaaaaaaaa,
    0x55555555,
    0x00000000,
};

int readver(){
    LOG("Looking for Vortex Pok3r...");
    Pok3r pok3r;
    if(pok3r.findPok3r()){
        if(pok3r.open()){
            LOG("Found: " << pok3r.getVersion());
            return 0;
        } else {
            LOG("Failed to Open");
            return -2;
        }
    } else {
        LOG("Not Found");
        return -1;
    }
}

int crcflash(){
    LOG("Looking for Vortex Pok3r...");
    Pok3r pok3r;
    if(pok3r.findPok3r()){
        if(pok3r.open()){
            zu32 len = pok3r.crcFlash(0x0, 0x100);
            LOG(len);
            return 0;
        } else {
            LOG("Failed to Open");
            return -2;
        }
    } else {
        LOG("Not Found");
        return -1;
    }
}

int readfw(zu32 start, zu32 len, ZPath out){
    LOG("Looking for Vortex Pok3r...");
    Pok3r pok3r;
    if(pok3r.findPok3r()){
        if(pok3r.open()){
            LOG("Reading...");
            ZBinary data;
            // Flash
//            zu64 start = 0x0;
//            zu64 len =   0x20000;
            // Boot loader
//            zu64 start = 0x1F000000;
//            zu64 len =       0x2000;
            // SRAM
//            zu64 start = 0x20000000;
//            zu64 len =       0x8000;
            // GPIO registers
//            zu64 start = 0x400B0000;
//            zu64 len =       0xA000;
            // VTOR
//            zu64 start = 0xE0000000;
//            zu64 len =     0x100000;
            for(zu64 o = 0; o < len; o += 64){
                if(pok3r.read(start + o, data) != 64){
                    LOG("Failed to read: 0x" << ZString::ItoS(start + o, 16));
                    return -3;
                }
            }
            LOG("Size: " << data.size());
            RLOG(data.dumpBytes(4, 8, start));
            ZFile::writeBinary(out, data);
            return 0;
        } else {
            LOG("Failed to Open");
            return -2;
        }
    } else {
        LOG("Not Found");
        return -1;
    }
}

/*  Decode the encryption scheme used by the updater program.
 *  Produced from IDA disassembly in sub_401000 of v117 updater.
 *  First, swap the 1st and 4th bytes, every 5 bytes
 *  Second, reverse each pair of bytes
 *  Third, shift the bits in each byte, sub 7 from MSBs
 */
void decode_package_scheme(ZBinary &bin){
    // Swap bytes 4 apart, skip 5
    for(zu64 i = 4; i < bin.size(); i+=5){
        zbyte a = bin[i-4];
        zbyte b = bin[i];
        bin[i-4] = b;
        bin[i] = a;
    }

    // Swap bytes in each set of two bytes
    for(zu64 i = 1; i < bin.size(); i+=2){
        zbyte d = bin[i-1];
        zbyte b = bin[i];
        bin[i-1] = b;
        bin[i] = d;
    }

    // y = ((x - 7) << 4) + (x >> 4)
    for(zu64 i = 0; i < bin.size(); ++i){
        bin[i] = ((bin[i] - 7) << 4) + (bin[i] >> 4);
    }
}

// Encrypt using the encryption scheme used by the updater program
void encode_package_scheme(ZBinary &bin){
    // x = (y >> 4 + 7 & 0xF) | (x << 4)
    for(zu64 i = 0; i < bin.size(); ++i){
        bin[i] = (((bin[i] >> 4) + 7) & 0xF) | (bin[i] << 4);
    }

    // Swap bytes in each set of two bytes
    for(zu64 i = 1; i < bin.size(); i+=2){
        zbyte d = bin[i-1];
        zbyte b = bin[i];
        bin[i-1] = b;
        bin[i] = d;
    }

    // Swap bytes 4 apart, skip 5
    for(zu64 i = 4; i < bin.size(); i+=5){
        zbyte a = bin[i-4];
        zbyte b = bin[i];
        bin[i-4] = b;
        bin[i] = a;
    }
}

const char frizz[] = {
    0,1,2,3,
    1,2,3,0,
    2,1,3,0,
    3,2,1,0,
    3,1,0,2,
    1,2,0,3,
    2,3,1,0,
    0,2,1,3,
};

void decode_firmware_packet(zbyte *data, zu32 num){
    zu32 *words = (zu32*)data;

    for(int i = 0; i < 13; ++i){
        // XOR with key
        words[i] = words[i] ^ xor_key_words[i];
    }

    zbyte buff[52];
    zbyte *o = buff;
    const char *f = frizz + ((num & 7) << 2);

    for(int i = 0; i < 13; ++i){
        zbyte *p = data + (i << 2);
        const char *g = f;

        *o++ = p[*g++];
        *o++ = p[*g++];
        *o++ = p[*g++];
        *o++ = p[*g++];

        continue;


        zu32 a = words[i] &       0xff; // lsb
        zu32 b = words[i] &     0xff00;
        zu32 c = words[i] &   0xff0000;
        zu32 d = words[i] & 0xff000000; // msb

        // Swap bytes on counter
        switch(num & 7){
            case 1:
                a <<= 24;
                b >>= 8;
                c >>= 8;
                d >>= 8;
                o[0] = p[1];
                o[1] = p[2];
                o[2] = p[3];
                o[3] = p[0];
                break;
            case 2:
                a <<= 24;
                b = b;
                c >>= 16;
                d >>= 8;
                o[0] = p[2];
                o[1] = p[1];
                o[2] = p[3];
                o[3] = p[0];
                break;
            case 3:
                a <<= 16;
                b <<= 8;
                c >>= 8;
                d >>= 24;
                break;
            case 4:
                a <<= 16;
                b = b;
                c <<= 8;
                d >>= 24;
                break;
            case 5:
                a <<= 16;
                b >>= 8;
                c >>= 8;
                d = d;
                break;
            case 6:
                a <<= 24;
                b <<= 8;
                c >>= 16;
                d >>= 16;
                break;
            case 7:
                a = a;
                b <<= 8;
                c >>= 8;
                d = d;
                break;
            default:
                break;
        }

        words[i] = a | b | c | d;
    }

    memcpy(data, o, 64);
}

// Decode the encryption scheme used by the firmware
void decode_firmware_scheme(ZBinary &bin){
    zu32 count = 0;
    zs32 swap = 1;
    for(zu32 offset = 0; offset < bin.size(); offset += 52){
        if(count >= 10 && count <= 100){
            decode_firmware_packet(bin.raw() + offset, (zu32)(++swap));
        }
        count++;
    }
}

void encode_firmware_scheme(ZBinary &bin){

}

void fwDecode(ZBinary &bin){
    decode_package_scheme(bin);
//    decode_firmware_scheme(bin);
}

void fwEncode(ZBinary &bin){
    decode_package_scheme(bin);
}

// POK3R
#define V113_HASH       0x62FCF913A689C9AE
#define V114_HASH       0xFE37430DB1FFCF5F
#define V115_HASH       0x8986F7893143E9F7
#define V116_HASH       0xA28E5EFB3F796181
#define V117_HASH       0xEA55CB190C35505F

// POK3R RGB
#define V124_HASH       0x882cb0e4ece25454
#define V130_HASH       0x6CFF0BB4F4086C2F

#define FWU_START       0x1A3800
#define STRINGS_LEN     0x4B8

int decfw(ZPath exe, ZPath out){
    LOG("Extract from " << exe);
    ZFile file;
    if(!file.open(exe, ZFile::READ)){
        ELOG("Failed to open file");
        return -1;
    }

    zu64 exelen = file.fileSize();
    zu64 exehash = ZFile::fileHash(exe);

    char type = 0;

    zu64 strings_len;
    zu64 strings_start;
    zu64 fw_len;

    zu64 offset_company;
    zu64 offset_product;
    zu64 offset_desc;
    zu64 offset_version;
    zu64 offset_sig;


    switch(exehash){
        case V113_HASH:
        case V114_HASH:
        case V115_HASH:
        case V116_HASH:
        case V117_HASH:
            type = 1;
            strings_len = 0x4B8;

            offset_company = 0x10;
            offset_product = 0x218;
            offset_desc    = 0x424;
            offset_version = 0x460;
            offset_sig     = 0x4AE;
            break;

        case V124_HASH:
        case V130_HASH:
            type = 2;
            strings_len = 0xB24; // from IDA disassembly in sub_403830 of v130 updater

            offset_company = 0x22E;
            offset_product = 0x436;
            offset_desc    = 0;
            offset_version = 0;
            offset_sig     = 0xB19;
            break;

        default:
            ELOG("Unknown updater executable: " << ZString::ItoS(exehash, 16));
            return -2;
    }

    strings_start = exelen - strings_len;

    // Read strings
    ZBinary strs;
    if(file.seek(strings_start) != strings_start){
        LOG("File too short - seek");
        return -4;
    }
    if(file.read(strs, strings_len) != strings_len){
        LOG("File too short - read");
        return -5;
    }
    // Decrypt strings
    decode_package_scheme(strs);

    ZString company;
    ZString product;
    ZString description;
    ZString version;

    // Company name
    company.parseUTF16((const zu16 *)(strs.raw() + offset_company), 0x200);
    // Product name
    product.parseUTF16((const zu16 *)(strs.raw() + offset_product), 0x200);
    // Description
    description.parseUTF16((const zu16 *)(strs.raw() + offset_desc), 0x20);
    // Version
    version = ZString(strs.raw() + offset_version, 12);

    LOG("Company:     " << company);
    LOG("Product:     " << product);
    LOG("Description: " << description);
    LOG("Version:     " << version);

    LOG("Signature:   " << ZString(strs.raw() + offset_sig, strings_len - offset_sig));

//    LOG("String Dump:");
//    RLOG(strs.dumpBytes(4, 8));

    // Decode other encrypted sections

    zu64 total = strings_len;
    ZArray<zu64> sections;

    switch(type){
        case 1:
            fw_len = ZBinary::decle32(strs.raw() + 0x420); // Firmware location
            total += fw_len;
                sections.push(fw_len);
            break;

        case 2: {
            zu64 start = 0xAC8 - (0x50 * 8);
            for(zu8 i = 0; i < 8; ++i){
                zu32 fwl = ZBinary::decle32(strs.raw() + start);
                zu32 strl = ZBinary::decle32(strs.raw() + start + 4);
                total += fwl;
                total += strl;
                sections.push(fwl);
                sections.push(strl);
                start += 0x50;
            }
            break;
        }

        default:
            return -4;
            break;
    }

    zu64 sec_start = exelen - total;
    LOG("Section Count: " << sections.size());

    for(zu64 i = 0; i < sections.size(); ++i){
        zu64 sec_len = sections[i];
        if(sec_len == 0)
            continue;

        LOG("Section: " << i);
        LOG("  Section Length: 0x" << ZString::ItoS(sec_len, 16));

        // Read section
        ZBinary sec;
        if(file.seek(sec_start) != sec_start){
            LOG("File too short - seek");
            return -2;
        }
        if(file.read(sec, sec_len) != sec_len){
            LOG("File too short - read");
            return -3;
        }

        sec_start += sec_len;

        switch(type){
            case 1:
                // Decrypt firmware
                fwDecode(sec);
                break;

            case 2:
                // Decode section
                decode_package_scheme(sec);
                break;

            default:
                break;
        }

//        LOG("Section Dump:");
//        RLOG(sec.dumpBytes(4, 8, 0));

        ZPath secout = out;
        if(type == 2)
            secout.last() = out.getName() + "-" + i + out.getExtension();
        LOG("  Section Output: " << secout);

        // Write firmware
        ZFile fwout(secout, ZFile::WRITE);
        fwout.write(sec);
    }

    return 0;
}

int encfw(ZPath exein, ZPath fwin, ZPath exeout){
    LOG("Updater: " << exein);
    LOG("Decoded Firmware: " << fwin);

    // Read updater
    ZBinary exebin;
    if(!ZFile::readBinary(exein, exebin)){
        ELOG("Failed to read file");
        return -1;
    }

    // Read firmware
    ZBinary fwbin;
    if(!ZFile::readBinary(fwin, fwbin)){
        LOG("Failed to read file");
        return -2;
    }

    // Encode firmware
    fwEncode(fwbin);

    // Write encoded firmware onto exe
    exebin.seek(FWU_START);
    exebin.write(fwbin);

    ZFile exefile;
    if(!exefile.open(exeout, ZFile::WRITE)){
        ELOG("Failed to open file");
        return -3;
    }
    // Write updated exe
    if(!exefile.write(exebin)){
        LOG("Write error");
        return -4;
    }

    LOG("Updated Updater: " << exeout);

    return 0;
}

int main(int argc, char **argv){
    ZLog::logLevelStdOut(ZLog::INFO, "%time% %thread% N %log%");
    ZLog::logLevelStdErr(ZLog::ERRORS, "\x1b[31m%time% %thread% E [%file%:%line%] %log%\x1b[m");

    if(argc > 1){
        ZString cmd = argv[1];
        if(cmd == "version"){
            // Read version from Pok3r
            return readver();
        } else if(cmd == "read"){
            // Read bytes from Pok3r
            if(argc > 4){
                zu64 start = ZString(argv[2]).toUint(16);
                zu64 len = ZString(argv[3]).toUint(16);
                return readfw(start, len, ZString(argv[4]));
            } else {
                LOG("Usage: pok3rtest read <start address> <length> <output.bin>");
                return 2;
            }
        } else if(cmd == "decode"){
            // Decode firmware from updater executable
            if(argc > 3){
                return decfw(ZString(argv[2]), ZString(argv[3]));
            } else {
                LOG("Usage: pok3rtest decode <path to updater> <output>");
                return 2;
            }
        } else if(cmd == "encode"){
            // Encode firmware into updater executable
            if(argc > 4){
                return encfw(ZString(argv[2]), ZString(argv[3]), ZString(argv[4]));
            } else {
                LOG("Usage: pok3rtest encode <path to updater> <path to firmware> <output updater>");
                return 2;
            }
        } else if(cmd == "crc"){
            // CRC
            return crcflash();
        } else {
            LOG("Unknown Command \"" << cmd << "\"");
            return 1;
        }
    } else {
        LOG("No Command");
        return 1;
    }
}