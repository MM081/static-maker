#include <random>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <fstream>
#include <cstdlib>
#include "crc32.h"

using namespace std;

// Returns a vector of a decimal split into 8-bit chunks, length >= minlen; zeroes will be added if need be. I think it returns little-endian...? Not 100% confident I really get the difference
vector<unsigned int> bytesplit(unsigned int dec, unsigned int minlen = 0)
{
    vector<unsigned int> ByteChunks = {dec};
    while (ByteChunks.back() > 255)
    {
        ByteChunks.push_back(ByteChunks.back()/256);
        ByteChunks[ByteChunks.size()-2] %= 256;
    }
    while (ByteChunks.size() < minlen)
    {
        ByteChunks.push_back(0);
    }
    return ByteChunks;
}

// You don't want to know how long I spent not realizing I needed this
namespace adler32
{
    // Calculate adler32 of a char array
	uint32_t checksum(unsigned char prea[])
	{
		uint32_t a = 1;
		uint32_t b = 0;
		for (int i = 0; i < sizeof(prea)/sizeof(prea[0]);i++)
		{
			a += prea[i];
			b += a;
		}
		a, b %= 65536;
		return (b << 16) | a;
	}
	// Update an existing adler32 with a char array
	uint32_t update(uint32_t checksum, unsigned char prea[])
	{
		vector<unsigned int> bytesum = bytesplit(checksum,4);
		uint32_t a = bytesum[0] + bytesum[1]*256;
		uint32_t b = bytesum[2] + bytesum[3]*256;
		for (int i = 0; i < sizeof(prea)/sizeof(prea[0]);i++)
		{
			a += prea[i];
			b += a;
		}
		a, b %= 65536;
		return (b << 16) | a;
	}
}

void generate_image(int imgnum, uint16_t width, uint16_t height, uint16_t c1[3], uint16_t c2[3])
{
    // Setting up random
    random_device dev;
    mt19937 rng(dev());
    uniform_int_distribution<mt19937::result_type> dist2(0,1);

    uint32_t table[256];
    crc32::generate_table(table);

    // Write png signature and size of IHDR chunk
    ofstream image;
    string imgtitle = "static.png";
    imgtitle.insert(6,to_string(imgnum));
    image.open(imgtitle);
    {
        unsigned char head[12] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D};
        for (int i = 0; i < 12; i++)
        {
            image<<head[i];
        }
    }

    // Write IHDR chunk; bit depth: 8, color type: 2, no interlace
    {
        unsigned char IHDR[21] = "IHDR";
        vector<unsigned int> widthsplit = bytesplit(width,4);
        vector<unsigned int> heightsplit = bytesplit(height,4);
        for (int i = 0; i < 4; i++)
        {
            IHDR[4+i] = widthsplit[3-i];
            IHDR[8+i] = heightsplit[3-i];
        }
        IHDR[12] = 0x08;
        IHDR[13] = 0x02;
        IHDR[14],IHDR[15],IHDR[16] = 0x00;
        unsigned char *ptr = (unsigned char*)&IHDR;
        uint32_t CRC = 0;
        for (int i = 0; i < 17; i++)
        {
            image<<IHDR[i];
            CRC = crc32::update(table,CRC,ptr,1);
            ptr++;
        }
        for (int i = 0; i < 4; i++)
        {
            unsigned char crcsplit = bytesplit(CRC,4)[3-i];
            image<<crcsplit;
        }
    }

    // Write IDAT chunks

    uint64_t imglen = (3*width*height) + height; // Total number of bytes of raw image data
    uint16_t scanlines = 65535/(3*width+1); // Number of scanlines in each IDAT chunk
    uint16_t IDATlen = scanlines*(3*width+1); // Number of bytes of raw image data in each IDAT chunk
    uint8_t firstchunk = 2; // 2 if it's the first IDAT chunk, otherwise 0
    uint32_t ADLER;
    while (imglen > 65535)
    {
        // Write chunk size directly to file, and prepare name and DEFLATE header
        unsigned char chunksize[4];
        for (int i = 0; i < 4; i++)
        {
            chunksize[i] = bytesplit(5+firstchunk+IDATlen, 4)[3-i];
            image<<chunksize[i];
        }
        unsigned char chunkheader[9+firstchunk] = "IDAT";

        if (firstchunk == 2)
        {
            // Genuinely, what even are these 2 numbers? Pulling them from another image seems to work fine, but I haven't the slightest clue how you're *supposed* to determine them
            chunkheader[4] = 0x78;
            chunkheader[5] = 0x01;
        }

        // I think I should use a lambda or something here but idc
        // ...
        // Actually there's probably an even better way of doing this but again, idc
        chunkheader[4+firstchunk] = 0x00;
        unsigned int LEN[2];
        LEN[0] = bytesplit(IDATlen,2)[0];
        LEN[1] = bytesplit(IDATlen,2)[1];
        chunkheader[5+firstchunk] = LEN[0];
        chunkheader[6+firstchunk] = LEN[1];
        chunkheader[7+firstchunk] = ~LEN[0];
        chunkheader[8+firstchunk] = ~LEN[1];

        // Calculate CRC32 of chunk name and header, and write both to file
        for (int i=0;i<9+firstchunk;i++)
        {
            image<<chunkheader[i];
        }
        unsigned char* ptr = (unsigned char*)&chunkheader;
        uint32_t CRC = 0;
        for (int i = 0; i < 9+firstchunk; i++)
        {
            CRC = crc32::update(table,CRC,ptr,1);
            ptr++;
        }
        firstchunk = 0;

        for (int i = 0; i < scanlines; i++)
        {
            // Prepare one scanline
            unsigned char scanline[width*3+1];
            scanline[0] = 0x00;
            for (uint16_t j = 0; j < width*3; j += 3)
            {
                if (dist2(rng) == 1)
                {
                    scanline[j+1] = c1[0];
                    scanline[j+2] = c1[1];
                    scanline[j+3] = c1[2];
                } else {
                    scanline[j+1] = c2[0];
                    scanline[j+2] = c2[1];
                    scanline[j+3] = c2[2];
                }
            }

            // Calculate/update adler32 of scanline
            if (i == 0 && firstchunk == 2)
            {
                ADLER = adler32::checksum(scanline);
            } else {
                ADLER = adler32::update(ADLER, scanline);
            }

            // Update CRC32 with scanline and write it to file
            ptr = (unsigned char*)&scanline;
            for (int j = 0; j < width*3+1; j++)
            {
                CRC = crc32::update(table,CRC,ptr,1);
                ptr++;
            }
            for (int j = 0; j < width*3+1; j++)
            {
                image<<scanline[j];
            }
        }

        // Write CRC32 to file
        unsigned char crchar[4];
        for (int i = 0; i < 4; i++)
        {
            crchar[i] = bytesplit(CRC,4)[3-i];
            image<<crchar[i];
        }
        imglen -= IDATlen;
    }

    // Write last IDAT chunk
    {
        // Write chunk size directly to file, and prepare name and DEFLATE header
        unsigned char chunksize[4];
        for (int i = 0; i < 4; i++)
        {
            chunksize[i] = bytesplit(11+firstchunk+imglen, 4)[3-i];
            image<<chunksize[i];
        }
        unsigned char chunkheader[9+firstchunk] = "IDAT";

        if (firstchunk == 2)
        {
            chunkheader[4] = 0x78;
            chunkheader[5] = 0x01;
        }
        chunkheader[4+firstchunk] = 0x01;
        uint8_t LEN[2];
        LEN[0] = bytesplit(imglen+4,2)[0];
        LEN[1] = bytesplit(imglen+4,2)[1];
        chunkheader[5+firstchunk] = LEN[0];
        chunkheader[6+firstchunk] = LEN[1];
        chunkheader[7+firstchunk] = ~LEN[0];
        chunkheader[8+firstchunk] = ~LEN[1];

        // Calculate CRC32 of chunk name and header, and write both to file
        for (int i=0;i<9+firstchunk;i++)
        {
            image<<chunkheader[i];
        }
        unsigned char* ptr = (unsigned char*)&chunkheader;
        uint32_t CRC = 0;
        for (int i = 0; i < 9+firstchunk; i++)
        {
            CRC = crc32::update(table,CRC,ptr,1);
            ptr++;
        }
        firstchunk = 0;

        for (int i = 0; i < imglen/(3*width+1); i++)
        {
            // Prepare one scanline
            unsigned char scanline[width*3+1];
            scanline[0] = 0x00;
            for (uint16_t j = 0; j < width*3; j += 3)
            {
                if (dist2(rng) == 1)
                {
                    scanline[j+1] = c1[0];
                    scanline[j+2] = c1[1];
                    scanline[j+3] = c1[2];
                } else {
                    scanline[j+1] = c2[0];
                    scanline[j+2] = c2[1];
                    scanline[j+3] = c2[2];
                }
            }

            // Calculate/update adler32 of scanline
            if (i == 0 && firstchunk == 2)
            {
                ADLER = adler32::checksum(scanline);
            } else {
                ADLER = adler32::update(ADLER, scanline);
            }

            // Update CRC32 with scanline and write it to file
            ptr = (unsigned char*)&scanline;
            for (int j = 0; j < width*3+1; j++)
            {
                CRC = crc32::update(table,CRC,ptr,1);
                ptr++;
            }
            for (int j = 0; j < width*3+1; j++)
            {
                image<<scanline[j];
            }
        }

        // Write CRC32 and adler32 to file
        unsigned char adchar[4];
        unsigned char crchar[4];
        for (int i = 0; i < 4; i++)
        {
            adchar[i] = bytesplit(ADLER,4)[3-i];
            image<<adchar[i];
        }
        ptr = (unsigned char*)&adchar;
        for (int i = 0; i < 4; i++)
        {
            crc32::update(table,CRC,ptr,1);
            ptr++;
        }
        for (int i = 0; i < 4; i++)
        {
            crchar[i] = bytesplit(CRC,4)[3-i];
            image<<crchar[i];
        }
    }

    // Write IEND chunk
    {
        unsigned char IEND[12] = {0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};
        for (int i = 0; i < 12; i++)
        {
            image<<IEND[i];
        }
    }

    image.close();
}
int main()
{
    // Take user input on number of files to create
    int numoffiles;
    cout<<"Number of images to create\n";
    cin>>numoffiles;

    // Take user input on width and height
    uint16_t width, height;
    cout<<"Width (Max 21844):\n";
    cin >> width;
    cout<<"Height:\n";
    cin >> height;

    // Take user input on colors
    uint16_t c1[3], c2[3];
    cout<<"Color input format:\n[Red,0-255]\n[Green,0-255]\n[Blue,0-255]\n";
    cout<<"Color 1:\n";
    cin >> c1[0];
    cin >> c1[1];
    cin >> c1[2];
    cout<<"Color 2:\n";
    cin >> c2[0];
    cin >> c2[1];
    cin >> c2[2];

    for (int i = 0; i < numoffiles; i++)
    {
        generate_image(i,width,height,c1,c2);
    }
    return 0;
}
