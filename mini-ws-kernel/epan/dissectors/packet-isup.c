#ifdef HAVE_CONFIG_H
# include "config.h"
#endif


#include <epan/packet.h>
#include <epan/exceptions.h>
#include <epan/asn1.h>
#include <wsutil/str_util.h>
#include "packet-isup.h"





static const value_string x213_afi_value[] = {
  { 0x34, "IANA ICP"},
  { 0x35, "IANA ICP"},
  { 0x36, "X.121"},
  { 0x37, "X.121"},
  { 0x38, "ISO DCC"},
  { 0x39, "ISO DCC"},
  { 0x40, "F.69"},
  { 0x41, "F.69"},
  { 0x42, "E.163"},
  { 0x43, "E.163"},
  { 0x44, "E.164"},
  { 0x45, "E.164"},
  { 0x46, "ISO 6523-ICD"},
  { 0x47, "ISO 6523-ICD"},
  { 0x48, "Local"},
  { 0x49, "Local"},
  { 0x50, "Local ISO/IEC 646 character "},
  { 0x51, "Local (National character)"},
  { 0x52, "X.121"},
  { 0x53, "X.121"},
  { 0x54, "F.69"},
  { 0x55, "F.69"},
  { 0x56, "E.163"},
  { 0x57, "E.163"},
  { 0x58, "E.164"},
  { 0x59, "E.164"},

  { 0x76, "ITU-T IND"},
  { 0x77, "ITU-T IND"},

  { 0xb8, "IANA ICP Group no"},
  { 0xb9, "IANA ICP Group no"},
  { 0xba, "X.121 Group no"},
  { 0xbb, "X.121 Group no"},
  { 0xbc, "ISO DCC Group no"},
  { 0xbd, "ISO DCC Group no"},
  { 0xbe, "F.69 Group no"},
  { 0xbf, "F.69 Group no"},
  { 0xc0, "E.163 Group no"},
  { 0xc1, "E.163 Group no"},
  { 0xc2, "E.164 Group no"},
  { 0xc3, "E.164 Group no"},
  { 0xc4, "ISO 6523-ICD Group no"},
  { 0xc5, "ISO 6523-ICD Group no"},
  { 0xc6, "Local Group no"},
  { 0xc7, "Local Group no"},
  { 0xc8, "Local ISO/IEC 646 character Group no"},
  { 0xc9, "Local (National character) Group no"},
  { 0xca, "X.121 Group no"},
  { 0xcb, "X.121 Group no"},
  { 0xcd, "F.69 Group no"},
  { 0xce, "F.69 Group no"},
  { 0xcf, "E.163 Group no"},
  { 0xd0, "E.164 Group no"},
  { 0xd1, "E.164 Group no"},
  { 0xde, "E.163 Group no"},

  { 0xe2, "ITU-T IND Group no"},
  { 0xe3, "ITU-T IND Group no"},
  { 0,  NULL }
};
value_string_ext x213_afi_value_ext = VALUE_STRING_EXT_INIT(x213_afi_value);


































