/*jshint globalstrict:false, strict:false */
/* global getOptions, runSetup, assertTrue, assertFalse, assertEqual, assertNotEqual, arango */

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
    'server.allow-use-database': 'false',
    'javascript.endpoints-denylist': '^.*$',
    'javascript.files-allowlist': '^$',
    'runSetup': true
  };
}

if (runSetup === true) {
  let users = require("@arangodb/users");

  users.save("test_rw", "testi");
  users.grantDatabase("test_rw", "_system", "rw");

  return true;
}

var jsunity = require('jsunity');

function testSuite() {
  let endpoint = arango.getEndpoint();
  let db = require("@arangodb").db;
  const errors = require('@arangodb').errors;

  return {
    setUp: function() {
      try {
        db._dropDatabase("UnitTestsApiTest");
      } catch (err) {}

      db._createDatabase("UnitTestsApiTest");
      db._useDatabase("UnitTestsApiTest");
      db._create("UnitTestsCollection");
      db._useDatabase("_system");
    },

    tearDown: function() {
      db._useDatabase("_system");
      db._dropDatabase("UnitTestsApiTest");
    },

    testCanSwitchDatabase : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");

      let data = {
        collections: { },
        action: String(function() {
          let db = require("@arangodb").db;
          db._useDatabase("UnitTestsApiTest");
        })
      };

      let result = arango.POST("/_api/transaction", data);
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertEqual(errors.ERROR_FORBIDDEN.code, result.errorNum);
    },

    testReadArbitraryFiles : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");

      let data = {
        collections: { },
        action: String(function() {
          const fs = require('fs');
          const rootDir = fs.join(fs.getTempPath(), '..');
          return fs.listTree(rootDir);
        })
      };

      let result = arango.POST("/_api/transaction", data);
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertEqual(errors.ERROR_FORBIDDEN.code, result.errorNum);
    },

    testDownload : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");

      let data = {
        collections: { },
        action: String(function() {
          require("internal").download("https://www.arangodb.com/");
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
