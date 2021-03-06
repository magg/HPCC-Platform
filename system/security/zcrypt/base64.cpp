/*##############################################################################

    Copyright (C) 2011 HPCC Systems.

    All rights reserved. This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
############################################################################## */

#include "base64.ipp"

static const char BASE64_enc[65] =  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789+/";

static const char pad = '=';

ZBuffer& base64_encode(int length, const void *data, ZBuffer& result)
{
    int estlen = (int)(length*4.0/3.0 + (length / 54) + 8);
    char *out  = new char[estlen];

    const unsigned char *in = static_cast<const unsigned char *>(data);

    unsigned char one;
    unsigned char two;
    unsigned char three;

    long i, j;
    for(i = 0, j = 0; i <= length - 3;)
    {
        one = *(in + i++);
        two = *(in + i++);
        three = *(in + i++);

        // 0x30 -> 0011 0000 b
        // 0x3c -> 0011 1100 b
        // 0x3f -> 0011 1111 b
        //
        out[j++] = BASE64_enc[one >> 2];
        out[j++] = BASE64_enc[((one << 4) & 0x30) | (two >> 4)];
        out[j++] = BASE64_enc[((two << 2)  & 0x3c) | (three >> 6)];
        out[j++] = BASE64_enc[three & 0x3f];

        if(i % 54 == 0)
        {
            out[j++] = '\n';
        }
    }

    switch(length - i)
    {
    case 2:
        one = *(in + i++);
        two = *(in + i++);

        out[j++] = BASE64_enc[one >> 2];
        out[j++] = BASE64_enc[((one << 4) & 0x30) | (two >> 4)];
        out[j++] = BASE64_enc[(two << 2)  & 0x3c];
        out[j++] = pad;
        break;

    case 1:
        one = *(in + i++);

        out[j++] = BASE64_enc[one >> 2];
        out[j++] = BASE64_enc[(one << 4) & 0x30];
        out[j++] = pad;
        out[j++] = pad;
        break;
    }

    out[j++] = '\n';
    out[j] = '\0';

    result.setBuffer(j, (unsigned char*)out);

    return result;
}

ZBuffer& base64_decode(int inlen, const char *in, ZBuffer& data)
{
    if(!inlen || !in)
        return data;

    static unsigned char BASE64_dec[256] = {0};
    static bool initialized = false;

    if(!initialized)
    {
        for(int i = 0; i < 64; ++i)
        {
            BASE64_dec[BASE64_enc[i]] = i;
        }

        initialized = true;
    }

    //unsigned char *data = static_cast<unsigned char *>(out);
    unsigned char c1, c2, c3, c4;
    unsigned char d1, d2, d3, d4;

    int bc = 0; 
    int estlen = (int)(inlen/4.0*3.0 + 2);
    unsigned char* buf = new unsigned char[estlen];
    for(int i = 0; i < inlen; )
    {
        if(in[i] == '\n')
        {
            ++i;
            continue;
        }

        c1 = in[i++];
        c2 = in[i++];
        c3 = in[i++];
        c4 = in[i++];
        d1 = BASE64_dec[c1];
        d2 = BASE64_dec[c2];
        d3 = BASE64_dec[c3];
        d4 = BASE64_dec[c4];

        buf[bc++] = (d1 << 2) | (d2 >> 4);

        if(c3 == pad)
            break;

        buf[bc++] = (d2 << 4) | (d3 >> 2);

        if(c4 == pad)
            break;

        buf[bc++] = (d3 << 6) | d4;
    }

    if(bc > 0)
        data.setBuffer(bc, buf);
    else
        delete buf;

    return data;
}
