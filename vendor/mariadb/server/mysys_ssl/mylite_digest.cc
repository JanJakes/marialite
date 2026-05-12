/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <my_global.h>
#include <mysql/service_md5.h>
#include <mysql/service_sha1.h>

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

struct mylite_md5_context
{
  uint32_t state[4];
  uint64_t bytes;
  uchar block[64];
};

struct mylite_sha1_context
{
  uint32_t state[5];
  uint64_t bytes;
  uchar block[64];
};

static void mylite_md5_init(mylite_md5_context *ctx);
static void mylite_md5_input(mylite_md5_context *ctx, const uchar *buf,
                             size_t len);
static void mylite_md5_result(mylite_md5_context *ctx, uchar *digest);
static void mylite_sha1_init(mylite_sha1_context *ctx);
static void mylite_sha1_input(mylite_sha1_context *ctx, const uchar *buf,
                              size_t len);
static void mylite_sha1_result(mylite_sha1_context *ctx, uchar *digest);

extern "C" {

void my_md5(uchar *digest, const char *buf, size_t len)
{
  mylite_md5_context ctx;

  mylite_md5_init(&ctx);
  mylite_md5_input(&ctx, (const uchar *) buf, len);
  mylite_md5_result(&ctx, digest);
}


void my_md5_multi(uchar *digest, ...)
{
  va_list args;
  mylite_md5_context ctx;
  const uchar *str;

  va_start(args, digest);
  mylite_md5_init(&ctx);
  for (str= va_arg(args, const uchar *); str; str= va_arg(args, const uchar *))
    mylite_md5_input(&ctx, str, va_arg(args, size_t));
  mylite_md5_result(&ctx, digest);
  va_end(args);
}


size_t my_md5_context_size()
{
  return sizeof(mylite_md5_context);
}


void my_md5_init(void *context)
{
  mylite_md5_init((mylite_md5_context *) context);
}


void my_md5_input(void *context, const uchar *buf, size_t len)
{
  mylite_md5_input((mylite_md5_context *) context, buf, len);
}


void my_md5_result(void *context, uchar *digest)
{
  mylite_md5_result((mylite_md5_context *) context, digest);
}


void my_sha1(uchar *digest, const char *buf, size_t len)
{
  mylite_sha1_context ctx;

  mylite_sha1_init(&ctx);
  mylite_sha1_input(&ctx, (const uchar *) buf, len);
  mylite_sha1_result(&ctx, digest);
}


void my_sha1_multi(uchar *digest, ...)
{
  va_list args;
  mylite_sha1_context ctx;
  const uchar *str;

  va_start(args, digest);
  mylite_sha1_init(&ctx);
  for (str= va_arg(args, const uchar *); str; str= va_arg(args, const uchar *))
    mylite_sha1_input(&ctx, str, va_arg(args, size_t));
  mylite_sha1_result(&ctx, digest);
  va_end(args);
}


size_t my_sha1_context_size()
{
  return sizeof(mylite_sha1_context);
}


void my_sha1_init(void *context)
{
  mylite_sha1_init((mylite_sha1_context *) context);
}


void my_sha1_input(void *context, const uchar *buf, size_t len)
{
  mylite_sha1_input((mylite_sha1_context *) context, buf, len);
}


void my_sha1_result(void *context, uchar *digest)
{
  mylite_sha1_result((mylite_sha1_context *) context, digest);
}


int check_openssl_compatibility()
{
  return 0;
}

}

static uint32_t mylite_rotl32(uint32_t value, uint count);
static uint32_t mylite_load_le32(const uchar *src);
static uint32_t mylite_load_be32(const uchar *src);
static void mylite_store_le32(uchar *dst, uint32_t value);
static void mylite_store_be32(uchar *dst, uint32_t value);
static void mylite_store_le64(uchar *dst, uint64_t value);
static void mylite_store_be64(uchar *dst, uint64_t value);
static void mylite_md5_block(mylite_md5_context *ctx, const uchar *block);
static void mylite_sha1_block(mylite_sha1_context *ctx, const uchar *block);


static void mylite_md5_init(mylite_md5_context *ctx)
{
  ctx->state[0]= 0x67452301U;
  ctx->state[1]= 0xefcdab89U;
  ctx->state[2]= 0x98badcfeU;
  ctx->state[3]= 0x10325476U;
  ctx->bytes= 0;
}


static void mylite_md5_input(mylite_md5_context *ctx, const uchar *buf,
                             size_t len)
{
  size_t used= (size_t) (ctx->bytes & 63);

  ctx->bytes+= len;
  if (used)
  {
    const size_t free= sizeof(ctx->block) - used;
    if (len < free)
    {
      memcpy(ctx->block + used, buf, len);
      return;
    }
    memcpy(ctx->block + used, buf, free);
    mylite_md5_block(ctx, ctx->block);
    buf+= free;
    len-= free;
  }

  while (len >= sizeof(ctx->block))
  {
    mylite_md5_block(ctx, buf);
    buf+= sizeof(ctx->block);
    len-= sizeof(ctx->block);
  }

  if (len)
    memcpy(ctx->block, buf, len);
}


static void mylite_md5_result(mylite_md5_context *ctx, uchar *digest)
{
  const uint64_t bit_count= ctx->bytes * 8;
  const size_t used= (size_t) (ctx->bytes & 63);
  const size_t pad_len= (used < 56) ? (56 - used) : (120 - used);
  uchar pad[64]= {0x80};
  uchar length[8];

  mylite_store_le64(length, bit_count);
  mylite_md5_input(ctx, pad, pad_len);
  mylite_md5_input(ctx, length, sizeof(length));

  mylite_store_le32(digest, ctx->state[0]);
  mylite_store_le32(digest + 4, ctx->state[1]);
  mylite_store_le32(digest + 8, ctx->state[2]);
  mylite_store_le32(digest + 12, ctx->state[3]);
}


static void mylite_sha1_init(mylite_sha1_context *ctx)
{
  ctx->state[0]= 0x67452301U;
  ctx->state[1]= 0xefcdab89U;
  ctx->state[2]= 0x98badcfeU;
  ctx->state[3]= 0x10325476U;
  ctx->state[4]= 0xc3d2e1f0U;
  ctx->bytes= 0;
}


static void mylite_sha1_input(mylite_sha1_context *ctx, const uchar *buf,
                              size_t len)
{
  size_t used= (size_t) (ctx->bytes & 63);

  ctx->bytes+= len;
  if (used)
  {
    const size_t free= sizeof(ctx->block) - used;
    if (len < free)
    {
      memcpy(ctx->block + used, buf, len);
      return;
    }
    memcpy(ctx->block + used, buf, free);
    mylite_sha1_block(ctx, ctx->block);
    buf+= free;
    len-= free;
  }

  while (len >= sizeof(ctx->block))
  {
    mylite_sha1_block(ctx, buf);
    buf+= sizeof(ctx->block);
    len-= sizeof(ctx->block);
  }

  if (len)
    memcpy(ctx->block, buf, len);
}


static void mylite_sha1_result(mylite_sha1_context *ctx, uchar *digest)
{
  const uint64_t bit_count= ctx->bytes * 8;
  const size_t used= (size_t) (ctx->bytes & 63);
  const size_t pad_len= (used < 56) ? (56 - used) : (120 - used);
  uchar pad[64]= {0x80};
  uchar length[8];

  mylite_store_be64(length, bit_count);
  mylite_sha1_input(ctx, pad, pad_len);
  mylite_sha1_input(ctx, length, sizeof(length));

  for (uint i= 0; i < 5; i++)
    mylite_store_be32(digest + i * 4, ctx->state[i]);
}


static uint32_t mylite_rotl32(uint32_t value, uint count)
{
  return (value << count) | (value >> (32 - count));
}


static uint32_t mylite_load_le32(const uchar *src)
{
  return ((uint32_t) src[0]) |
         ((uint32_t) src[1] << 8) |
         ((uint32_t) src[2] << 16) |
         ((uint32_t) src[3] << 24);
}


static uint32_t mylite_load_be32(const uchar *src)
{
  return ((uint32_t) src[0] << 24) |
         ((uint32_t) src[1] << 16) |
         ((uint32_t) src[2] << 8) |
         ((uint32_t) src[3]);
}


static void mylite_store_le32(uchar *dst, uint32_t value)
{
  dst[0]= (uchar) value;
  dst[1]= (uchar) (value >> 8);
  dst[2]= (uchar) (value >> 16);
  dst[3]= (uchar) (value >> 24);
}


static void mylite_store_be32(uchar *dst, uint32_t value)
{
  dst[0]= (uchar) (value >> 24);
  dst[1]= (uchar) (value >> 16);
  dst[2]= (uchar) (value >> 8);
  dst[3]= (uchar) value;
}


static void mylite_store_le64(uchar *dst, uint64_t value)
{
  for (uint i= 0; i < 8; i++)
    dst[i]= (uchar) (value >> (i * 8));
}


static void mylite_store_be64(uchar *dst, uint64_t value)
{
  for (uint i= 0; i < 8; i++)
    dst[i]= (uchar) (value >> ((7 - i) * 8));
}


static void mylite_md5_block(mylite_md5_context *ctx, const uchar *block)
{
  static const uint32_t md5_constants[64]=
  {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
    0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
    0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
    0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
    0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
    0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
    0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
    0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
    0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
    0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
    0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
  };
  static const uchar md5_shifts[64]=
  {
    7, 12, 17, 22, 7, 12, 17, 22,
    7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20,
    5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23,
    4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21,
    6, 10, 15, 21, 6, 10, 15, 21
  };
  uint32_t words[16];
  uint32_t a= ctx->state[0];
  uint32_t b= ctx->state[1];
  uint32_t c= ctx->state[2];
  uint32_t d= ctx->state[3];

  for (uint i= 0; i < 16; i++)
    words[i]= mylite_load_le32(block + i * 4);

  for (uint i= 0; i < 64; i++)
  {
    uint32_t f;
    uint g;

    if (i < 16)
    {
      f= (b & c) | (~b & d);
      g= i;
    }
    else if (i < 32)
    {
      f= (d & b) | (~d & c);
      g= (5 * i + 1) & 15;
    }
    else if (i < 48)
    {
      f= b ^ c ^ d;
      g= (3 * i + 5) & 15;
    }
    else
    {
      f= c ^ (b | ~d);
      g= (7 * i) & 15;
    }

    const uint32_t next_d= d;
    d= c;
    c= b;
    b+= mylite_rotl32(a + f + md5_constants[i] + words[g], md5_shifts[i]);
    a= next_d;
  }

  ctx->state[0]+= a;
  ctx->state[1]+= b;
  ctx->state[2]+= c;
  ctx->state[3]+= d;
}


static void mylite_sha1_block(mylite_sha1_context *ctx, const uchar *block)
{
  uint32_t words[80];
  uint32_t a= ctx->state[0];
  uint32_t b= ctx->state[1];
  uint32_t c= ctx->state[2];
  uint32_t d= ctx->state[3];
  uint32_t e= ctx->state[4];

  for (uint i= 0; i < 16; i++)
    words[i]= mylite_load_be32(block + i * 4);
  for (uint i= 16; i < 80; i++)
    words[i]= mylite_rotl32(words[i - 3] ^ words[i - 8] ^
                            words[i - 14] ^ words[i - 16], 1);

  for (uint i= 0; i < 80; i++)
  {
    uint32_t f;
    uint32_t k;

    if (i < 20)
    {
      f= (b & c) | (~b & d);
      k= 0x5a827999U;
    }
    else if (i < 40)
    {
      f= b ^ c ^ d;
      k= 0x6ed9eba1U;
    }
    else if (i < 60)
    {
      f= (b & c) | (b & d) | (c & d);
      k= 0x8f1bbcdcU;
    }
    else
    {
      f= b ^ c ^ d;
      k= 0xca62c1d6U;
    }

    const uint32_t temp= mylite_rotl32(a, 5) + f + e + k + words[i];
    e= d;
    d= c;
    c= mylite_rotl32(b, 30);
    b= a;
    a= temp;
  }

  ctx->state[0]+= a;
  ctx->state[1]+= b;
  ctx->state[2]+= c;
  ctx->state[3]+= d;
  ctx->state[4]+= e;
}
