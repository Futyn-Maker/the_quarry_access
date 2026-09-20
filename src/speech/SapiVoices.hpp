#pragma once
// The languages each installed SAPI voice speaks, read from the voice token's own attributes.
//
// The speech library reports every SAPI voice as en-us: it asks the token for a "Language"
// value, and SAPI keeps that value one key deeper, under the token's Attributes, where a voice
// also lists every language it speaks ("409;809"). So the languages are read here and
// speech/Outputs matches them to the library's voice list by name, which both sides take from
// the same place.

#include <string>
#include <unordered_map>
#include <vector>

namespace qa::sapivoices
{
    // Voice name to the language identifiers that voice lists. Empty when SAPI is not there.
    std::unordered_map<std::wstring, std::vector<unsigned>> Languages();
}
