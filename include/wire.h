#pragma once

static unsigned read_u32_le_at(const unsigned char *data, int off)
{
    return (unsigned)data[off]
         | ((unsigned)data[off + 1] << 8)
         | ((unsigned)data[off + 2] << 16)
         | ((unsigned)data[off + 3] << 24);
}

static void write_u32_le(unsigned char *buf, int *pos, unsigned value)
{
    for (int i = 0; i < 4; i++)
        buf[(*pos)++] = (unsigned char)(value >> (8 * i));
}

static void write_u32_le_at(unsigned char *buf, int off, unsigned value)
{
    for (int i = 0; i < 4; i++)
        buf[off + i] = (unsigned char)(value >> (8 * i));
}

static void write_u64_le(unsigned char *buf, int *pos,
                         unsigned long long value)
{
    for (int i = 0; i < 8; i++)
        buf[(*pos)++] = (unsigned char)(value >> (8 * i));
}

static void write_ascii_field(unsigned char *buf, int *pos,
                              const char *s, int width)
{
    if (width <= 0)
        return;

    int start = *pos;
    int i = 0;
    while (s[i] != '\0' && i < width - 1) {
        buf[start + i] = (unsigned char)s[i];
        i++;
    }
    buf[start + i] = '\0';
    *pos = start + width;
}
