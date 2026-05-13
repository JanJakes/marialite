/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "proxy_protocol.h"

bool
has_proxy_protocol_header(NET *)
{
  return false;
}

int
parse_proxy_protocol_header(NET *, proxy_peer_info *)
{
  return 1;
}

bool
is_proxy_protocol_allowed(const sockaddr *)
{
  return false;
}

int
init_proxy_protocol_networks(const char *spec)
{
  return set_proxy_protocol_networks(spec);
}

void
destroy_proxy_protocol_networks()
{
}

int
set_proxy_protocol_networks(const char *spec)
{
  return proxy_protocol_networks_valid(spec) ? 0 : 1;
}

bool
proxy_protocol_networks_valid(const char *spec)
{
  return !spec || !spec[0];
}
