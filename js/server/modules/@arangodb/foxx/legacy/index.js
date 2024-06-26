'use strict';

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
// / Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
// /
// / Licensed under the Business Source License 1.1 (the "License");
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     https://github.com/arangodb/arangodb/blob/devel/LICENSE
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is ArangoDB GmbH, Cologne, Germany
// /
// / @author Lucas Dohmen
// / @author Alan Plum
// //////////////////////////////////////////////////////////////////////////////

exports.Controller = require('@arangodb/foxx/legacy/controller').Controller;
exports.Model = require('@arangodb/foxx/legacy/model').Model;
exports.Repository = require('@arangodb/foxx/legacy/repository').Repository;
exports.createQuery = require('@arangodb/foxx/legacy/query').createQuery;
exports.toJSONSchema = require('@arangodb/foxx/legacy/schema').toJSONSchema;
exports.queues = require('@arangodb/foxx/queues');

const manager = require('@arangodb/foxx/manager');

exports.getExports = function (path) {
  return manager.requireService('/' + path.replace(/(^\/+|\/+$)/, ''));
};

exports.requireApp = function (path) {
  return exports.getExports(path);
};
