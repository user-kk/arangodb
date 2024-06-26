/*jshint globalstrict:false, strict:false */
/* global getOptions, assertTrue, assertFalse, assertEqual, arango */

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
/// @author Wilfried Goesgens
/// @author Copyright 2019, ArangoDB Inc, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

if (getOptions === true) {
  return {
    'javascript.harden': 'true',
    'javascript.allow-external-process-control': 'false'
  };
}
var jsunity = require('jsunity');

function testSuite() {
  let db = require("@arangodb").db;
  const errors = require('@arangodb').errors;

  return {
    setUp: function() {},
    tearDown: function() {},

    testGetPid : function() {
      let data = {
        collections: {},
        action: String(function() { require('internal').getPid(); })
      };

      let result = arango.POST("/_api/transaction", data);
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertEqual(errors.ERROR_FORBIDDEN.code, result.errorNum);
    },

    testClientStatistics : function() {
      let data = {
        collections: {},
        action: String(function() { require('internal').clientStatistics(); })
      };

      let result = arango.POST("/_api/transaction", data);
      assertFalse(result.error);
      assertEqual(200, result.code);
    },

    testHttpStatistics : function() {
      let data = {
        collections: {},
        action: String(function() { require('internal').httpStatistics(); })
      };

      let result = arango.POST("/_api/transaction", data);
      assertFalse(result.error);
      assertEqual(200, result.code);
    },

    testProcessStatistics : function() {
      let data = {
        collections: {},
        action: String(function() { require('internal').processStatistics(); })
      };

      let result = arango.POST("/_api/transaction", data);

      assertFalse(result.error);
      // disabled for oasis
      //assertTrue(result.error);
      //assertEqual(403, result.code);
      //assertEqual(errors.ERROR_FORBIDDEN.code, result.errorNum);
    },

    testExecuteExternal : function() {
      let data = {
        collections: {},
        action: String(function() {
          let command = "/bin/true";
          require('internal').executeExternal(command);
        })
      };

      let result = arango.POST("/_api/transaction", data);
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertEqual(errors.ERROR_FORBIDDEN.code, result.errorNum);
    },

  };
}

jsunity.run(testSuite);
return jsunity.done();
