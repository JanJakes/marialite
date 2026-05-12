/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <mysql/service_md5.h>
#include <mysql/service_sha1.h>

#include <cstring>
#include <iostream>
#include <string>

extern "C" int check_openssl_compatibility();

static bool run_digest_smoke();
static bool expect_digest(const char *label, const unsigned char *digest,
                          size_t length, const char *expected);
static std::string hex_digest(const unsigned char *digest, size_t length);

int main()
{
  return run_digest_smoke() ? 0 : 1;
}


static bool run_digest_smoke()
{
  unsigned char digest[MY_SHA1_HASH_SIZE];
  bool ok= true;

  my_md5(digest, "", 0);
  ok&= expect_digest("md5-empty", digest, MY_MD5_HASH_SIZE,
                     "d41d8cd98f00b204e9800998ecf8427e");

  my_md5(digest, "abc", 3);
  ok&= expect_digest("md5-abc", digest, MY_MD5_HASH_SIZE,
                     "900150983cd24fb0d6963f7d28e17f72");

  my_md5(digest,
         "12345678901234567890123456789012345678901234567890"
         "123456789012345678901234567890",
         80);
  ok&= expect_digest("md5-80", digest, MY_MD5_HASH_SIZE,
                     "57edf4a22be3c955ac49da2e2107b67a");

  my_md5_multi(digest, "a", (size_t) 1, "bc", (size_t) 2, nullptr);
  ok&= expect_digest("md5-multi-abc", digest, MY_MD5_HASH_SIZE,
                     "900150983cd24fb0d6963f7d28e17f72");

  alignas(16) unsigned char md5_context[128];
  if (my_md5_context_size() > sizeof(md5_context))
  {
    std::cerr << "md5-context-size: too large\n";
    ok= false;
  }
  else
  {
    my_md5_init(md5_context);
    my_md5_input(md5_context, (const unsigned char *) "a", 1);
    my_md5_input(md5_context, (const unsigned char *) "bc", 2);
    my_md5_result(md5_context, digest);
    ok&= expect_digest("md5-context-abc", digest, MY_MD5_HASH_SIZE,
                       "900150983cd24fb0d6963f7d28e17f72");
  }

  my_sha1(digest, "", 0);
  ok&= expect_digest("sha1-empty", digest, MY_SHA1_HASH_SIZE,
                     "da39a3ee5e6b4b0d3255bfef95601890afd80709");

  my_sha1(digest, "abc", 3);
  ok&= expect_digest("sha1-abc", digest, MY_SHA1_HASH_SIZE,
                     "a9993e364706816aba3e25717850c26c9cd0d89d");

  my_sha1(digest,
          "12345678901234567890123456789012345678901234567890"
          "123456789012345678901234567890",
          80);
  ok&= expect_digest("sha1-80", digest, MY_SHA1_HASH_SIZE,
                     "50abf5706a150990a08b2c5ea40fa0e585554732");

  my_sha1_multi(digest, "a", (size_t) 1, "bc", (size_t) 2, nullptr);
  ok&= expect_digest("sha1-multi-abc", digest, MY_SHA1_HASH_SIZE,
                     "a9993e364706816aba3e25717850c26c9cd0d89d");

  alignas(16) unsigned char sha1_context[128];
  if (my_sha1_context_size() > sizeof(sha1_context))
  {
    std::cerr << "sha1-context-size: too large\n";
    ok= false;
  }
  else
  {
    my_sha1_init(sha1_context);
    my_sha1_input(sha1_context, (const unsigned char *) "a", 1);
    my_sha1_input(sha1_context, (const unsigned char *) "bc", 2);
    my_sha1_result(sha1_context, digest);
    ok&= expect_digest("sha1-context-abc", digest, MY_SHA1_HASH_SIZE,
                       "a9993e364706816aba3e25717850c26c9cd0d89d");
  }

  if (check_openssl_compatibility() != 0)
  {
    std::cerr << "openssl-compatibility-check: expected 0\n";
    ok= false;
  }

  if (ok)
    std::cout << "digest smoke: ok\n";
  return ok;
}


static bool expect_digest(const char *label, const unsigned char *digest,
                          size_t length, const char *expected)
{
  const std::string actual= hex_digest(digest, length);

  if (actual == expected)
    return true;

  std::cerr << label << ": expected " << expected << ", got " << actual
            << "\n";
  return false;
}


static std::string hex_digest(const unsigned char *digest, size_t length)
{
  static const char hex[]= "0123456789abcdef";
  std::string result;

  result.reserve(length * 2);
  for (size_t i= 0; i < length; i++)
  {
    result.push_back(hex[digest[i] >> 4]);
    result.push_back(hex[digest[i] & 0x0f]);
  }
  return result;
}
