////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Christoph Uhde
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////
#pragma once
#ifndef ARANGO_CXX_DRIVER_HTTP
#define ARANGO_CXX_DRIVER_HTTP

#include <fuerte/message.h>
#include <fuerte/types.h>

#include <string>
#include <string_view>

namespace arangodb { namespace fuerte { inline namespace v1 { namespace http {

void urlDecode(std::string& out, std::string_view str);

/// url-decodes str and returns it - convenience function
inline std::string urlDecode(std::string_view str) {
  std::string result;
  urlDecode(result, str);
  return result;
}

/// url-encodes str into out - convenience function
void urlEncode(std::string& out, std::string_view str);

/// url-encodes str and returns it - convenience function
inline std::string urlEncode(std::string_view str) {
  std::string result;
  urlEncode(result, str);
  return result;
}

void appendPath(Request const& req, std::string& target);
}}}}  // namespace arangodb::fuerte::v1::http
#endif
