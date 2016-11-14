/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      utf8.cpp
@brief     RE/Flex UCS to UTF8 converters
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2016, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "utf8.h"

namespace reflex {

static const char *hex_r(char *buf, int a, const char *esc)
{
  sprintf(buf, "%sx%02x", esc, a);
  return buf;
}

static const char *hex_r(char *buf, int a, int b, const char *esc)
{
  if (a == b)
    return hex_r(buf, a, esc);
  sprintf(buf, "[%sx%02x-%sx%02x]", esc, a, esc, b);
  return buf;
}

std::string utf8(wchar_t a, wchar_t b, bool strict, const char *esc)
{
  if (esc == NULL || strlen(esc) > 3)
    esc = "\\";
  if (a < 0)
    return std::string(esc) + "x80"; // undefined
  if (a > b)
    b = a;
  static const char *min_utf8_strict[6] = { // strict UTF8 validation
    "\x00",
    "\xc2\x80",
    "\xe0\xa0\x80",
    "\xf0\x90\x80\x80",
    "\xf8\x88\x80\x80\x80",
    "\xfc\x84\x80\x80\x80\x80"
  };
  static const char *min_utf8_lean[6] = { // lean output, no UTF8 form validation
    "\x00",
    "\xc2\x80",
    "\xe0\x80\x80",
    "\xf0\x80\x80\x80",
    "\xf8\x80\x80\x80\x80",
    "\xfc\x80\x80\x80\x80\x80"
  };
  static const char *max_utf8[6] = {
    "\x7f",
    "\xdf\xbf",
    "\xef\xbf\xbf",
    "\xf7\xbf\xbf\xbf",
    "\xfb\xbf\xbf\xbf\xbf",
    "\xfd\xbf\xbf\xbf\xbf\xbf"
  };
  const char **min_utf8 = (strict ? min_utf8_strict : min_utf8_lean);
  char any[16];
  char buf[16];
  char at[6];
  char bt[6];
  size_t n = utf8(a, at);
  size_t m = utf8(b, bt);
  const unsigned char *as = reinterpret_cast<const unsigned char*>(at);
  const unsigned char *bs = NULL;
  std::string regex;
  if (strict)
    hex_r(any, 0x80, 0xbf, esc);
  else
    strcpy(any, ".");
  while (n <= m)
  {
    if (n < m)
      bs = reinterpret_cast<const unsigned char*>(max_utf8[n-1]);
    else
      bs = reinterpret_cast<const unsigned char*>(bt);
    size_t i;
    for (i = 0; i < n && as[i] == bs[i]; ++i)
      regex.append(hex_r(buf, as[i], esc));
    size_t l = 0; // pattern compression: l == 0 -> as[i+1..n-1] == UTF-8 lower bound
    for (size_t k = i+1; k < n && l == 0; ++k)
      if (as[k] != 0x80)
	l = 1;
    size_t h = 0; // pattern compression: h == 0 -> bs[i+1..n-1] == UTF-8 upper bound
    for (size_t k = i+1; k < n && h == 0; ++k)
      if (bs[k] != 0xbf)
	h = 1;
    if (i+1 < n)
    {
      size_t j = i;
      if (i != 0)
	regex.append("(");
      if (l != 0)
      {
	size_t p = 0;
	regex.append(hex_r(buf, as[i], esc));
	++i;
	while (i+1 < n)
	{
	  if (as[i+1] == 0x80) // pattern compression
	  {
	    regex.append(hex_r(buf, as[i], 0xbf, esc));
	    for (++i; i < n && as[i] == 0x80; ++i)
	      regex.append(any);
	  }
	  else
	  {
	    if (as[i] != 0xbf)
	    {
	      ++p;
	      regex.append("(").append(hex_r(buf, as[i]+1, 0xbf, esc));
	      for (size_t k = i+1; k < n; ++k)
		regex.append(any);
	      regex.append("|");
	    }
	    regex.append(hex_r(buf, as[i], esc));
	    ++i;
	  }
	}
	if (i < n)
	  regex.append(hex_r(buf, as[i], 0xbf, esc));
	for (size_t k = 0; k < p; ++k)
	  regex.append(")");
	i = j;
      }
      if (i+1 < n && as[i]+l <= bs[i]-h)
      {
	if (l != 0)
	  regex.append("|");
	regex.append(hex_r(buf, as[i]+l, bs[i]-h, esc));
	for (size_t k = i+1; k < n; ++k)
	  regex.append(any);
      }
      if (h != 0)
      {
	size_t p = 0;
	regex.append("|").append(hex_r(buf, bs[i], esc));
	++i;
	while (i+1 < n)
	{
	  if (bs[i+1] == 0xbf) // pattern compression
	  {
	    regex.append(hex_r(buf, 0x80, bs[i], esc));
	    for (++i; i < n && bs[i] == 0xbf; ++i)
	      regex.append(any);
	  }
	  else
	  {
	    if (bs[i] != 0x80)
	    {
	      ++p;
	      regex.append("(").append(hex_r(buf, 0x80, bs[i]-1, esc));
	      for (size_t k = i+1; k < n; ++k)
		regex.append(any);
	      regex.append("|");
	    }
	    regex.append(hex_r(buf, bs[i], esc));
	    ++i;
	  }
	}
	if (i < n)
	  regex.append(hex_r(buf, 0x80, bs[i], esc));
	for (size_t k = 0; k < p; ++k)
	  regex.append(")");
      }
      if (j != 0)
	regex.append(")");
    }
    else if (i < n)
    {
      regex.append(hex_r(buf, as[i], bs[i], esc));
    }
    if (n < m)
    {
      as = reinterpret_cast<const unsigned char*>(min_utf8[n]);
      regex.append("|");
    }
    ++n;
  }
  return regex;
}

} // namespace reflex