////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "EndpointList.h"
#include "Basics/Logger.h"
#include "Basics/StringUtils.h"

using namespace std;
using namespace arangodb::basics;
using namespace arangodb::rest;

static std::vector<std::string> const EmptyMapping;

////////////////////////////////////////////////////////////////////////////////
/// @brief create an endpoint list
////////////////////////////////////////////////////////////////////////////////

EndpointList::EndpointList() : _endpoints() {}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy an endpoint list
////////////////////////////////////////////////////////////////////////////////

EndpointList::~EndpointList() {
  for (auto& it : _endpoints) {
    Endpoint* ep = it.second.first;

    delete ep;
  }

  _endpoints.clear();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add a new endpoint
////////////////////////////////////////////////////////////////////////////////

bool EndpointList::add(std::string const& specification,
                       std::vector<std::string> const& dbNames, int backLogSize,
                       bool reuseAddress, Endpoint** dst) {
  std::string const key = Endpoint::getUnifiedForm(specification);

  if (key.empty()) {
    return false;
  }

  auto it = _endpoints.find(key);

  if (it != _endpoints.end()) {
    // already in list, just update
    (*it).second.second = dbNames;
    if (dst != nullptr) {
      *dst = nullptr;
    }
    return true;
  }

  Endpoint* ep = Endpoint::serverFactory(key, backLogSize, reuseAddress);

  if (ep == nullptr) {
    return false;
  }

  _endpoints[key] = std::pair<Endpoint*, std::vector<std::string>>(ep, dbNames);

  if (dst != nullptr) {
    *dst = ep;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a specific endpoint
////////////////////////////////////////////////////////////////////////////////

bool EndpointList::remove(std::string const& specification, Endpoint** dst) {
  std::string const key = Endpoint::getUnifiedForm(specification);

  if (key.empty()) {
    return false;
  }

  if (_endpoints.size() <= 1) {
    // must not remove last endpoint
    return false;
  }

  auto it = _endpoints.find(key);

  if (it == _endpoints.end()) {
    // not in list
    return false;
  }

  *dst = (*it).second.first;
  _endpoints.erase(key);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all databases for an endpoint
////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> const& EndpointList::getMapping(
    std::string const& endpoint) const {
  auto it = _endpoints.find(endpoint);

  if (it != _endpoints.end()) {
    return (*it).second.second;
  }

  return EmptyMapping;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all endpoints
////////////////////////////////////////////////////////////////////////////////

std::map<std::string, std::vector<std::string>> EndpointList::getAll() const {
  std::map<std::string, std::vector<std::string>> result;

  for (auto& it : _endpoints) {
    result[it.first] = it.second.second;
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all endpoints with a certain prefix
////////////////////////////////////////////////////////////////////////////////

std::map<std::string, Endpoint*> EndpointList::getByPrefix(
    std::string const& prefix) const {
  std::map<std::string, Endpoint*> result;

  for (auto& it : _endpoints) {
    std::string const& key = it.first;

    if (StringUtils::isPrefix(key, prefix)) {
      result[key] = it.second.first;
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all endpoints with a certain encryption type
////////////////////////////////////////////////////////////////////////////////

std::map<std::string, Endpoint*> EndpointList::getByPrefix(
    Endpoint::EncryptionType encryption) const {
  std::map<std::string, Endpoint*> result;

  for (auto& it : _endpoints) {
    std::string const& key = it.first;

    if (encryption == Endpoint::ENCRYPTION_SSL) {
      if (StringUtils::isPrefix(key, "ssl://")) {
        result[key] = it.second.first;
      }
    } else {
      if (StringUtils::isPrefix(key, "tcp://") ||
          StringUtils::isPrefix(key, "unix://")) {
        result[key] = it.second.first;
      }
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return if there is an endpoint with a certain encryption type
////////////////////////////////////////////////////////////////////////////////

bool EndpointList::has(Endpoint::EncryptionType encryption) const {
  for (auto& it : _endpoints) {
    std::string const& key = it.first;

    if (encryption == Endpoint::ENCRYPTION_SSL) {
      if (StringUtils::isPrefix(key, "ssl://")) {
        return true;
      }
    } else {
      if (StringUtils::isPrefix(key, "tcp://") ||
          StringUtils::isPrefix(key, "unix://")) {
        return true;
      }
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump all endpoints used
////////////////////////////////////////////////////////////////////////////////

void EndpointList::dump() const {
  for (auto& it : _endpoints) {
    Endpoint const* ep = it.second.first;

    LOG(INFO) << "using endpoint '" << it.first.c_str() << "' for " << getEncryptionName(ep->getEncryption()).c_str() << " requests";
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return an encryption name
////////////////////////////////////////////////////////////////////////////////

std::string EndpointList::getEncryptionName(
    Endpoint::EncryptionType encryption) {
  switch (encryption) {
    case Endpoint::ENCRYPTION_SSL:
      return "ssl-encrypted";
    case Endpoint::ENCRYPTION_NONE:
    default:
      return "non-encrypted";
  }
}
