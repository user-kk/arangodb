/*jshint globalstrict:false, strict:false */
/*global assertEqual, assertNotEqual, assertTrue, getOptions*/

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
/// @author Jan Christoph Uhde
/// @author Copyright 2019, triAGENS GmbH, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

if (getOptions === true) {
  return {
    'rocksdb.exclusive-writes': 'true',
  };
}

var jsunity = require("jsunity");
var tasks = require("@arangodb/tasks");
var arangodb = require("@arangodb");
var db = arangodb.db;

var ERRORS = arangodb.errors;

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function OptionsTestSuite () {
  var cn1 = "UnitTestsExclusiveCollection1"; // used for test data
  var cn2 = "UnitTestsExclusiveCollection2"; // used for communication
  var c1, c2;

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      db._drop(cn1);
      db._drop(cn2);
      c1 = db._create(cn1, { numberOfShards: 2 });
      c2 = db._create(cn2, { numberOfShards: 2 });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      db._drop(cn1);
      db._drop(cn2);
    },

    testExclusiveExpectConflictWithoutOption : function () {
      c1.insert({ "_key" : "XXX" , "name" : "initial" });
      let task = tasks.register({
        command: function() {
          let db = require("internal").db;
          db.UnitTestsExclusiveCollection2.insert({ _key: "runner1", value: false });

          while (!db.UnitTestsExclusiveCollection2.exists("runner2")) {
            require("internal").sleep(0.02);
          }

          db._executeTransaction({
            collections: { write: [ "UnitTestsExclusiveCollection1", "UnitTestsExclusiveCollection2" ] },
            action: function () {
              let db = require("internal").db;
              for (let i = 0; i < 10000; ++i) {
                db.UnitTestsExclusiveCollection1.update("XXX", { name : "runner1" });
              }
              db.UnitTestsExclusiveCollection2.update("runner1", { value: true });
            }
          });
        }
      });

      db.UnitTestsExclusiveCollection2.insert({ _key: "runner2", value: false });
      while (!db.UnitTestsExclusiveCollection2.exists("runner1")) {
        require("internal").sleep(0.02);
      }

      db._executeTransaction({
        collections: { write: [ "UnitTestsExclusiveCollection1", "UnitTestsExclusiveCollection2" ] },
        action: function () {
          let db = require("internal").db;
          for (let i = 0; i < 10000; ++i) {
            db.UnitTestsExclusiveCollection1.update("XXX", { name : "runner2" });
          }
          db.UnitTestsExclusiveCollection2.update("runner2", { value: true });
        }
      });

      while (true) {
        try {
          tasks.get(task);
          require("internal").wait(0.25, false);
        } catch (err) {
          // "task not found" means the task is finished
          break;
        }
      }

      assertEqual(2, c2.count());
      // both transactions should have succeeded
      assertTrue(c2.document("runner1").value);  // runner1 transaction should succeed
      assertTrue(c2.document("runner2").value); // runner2 transaction should succeed
    },

  };
}

jsunity.run(OptionsTestSuite);

return jsunity.done();
