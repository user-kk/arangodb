/*jshint globalstrict:false, strict:false */
/* global getOptions, assertEqual, assertNotEqual */

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
/// @author Jan Steemann
/// @author Copyright 2019, ArangoDB Inc, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

const jsunity = require('jsunity');
const errors = require('@arangodb').errors;
const cn = "UnitTestsCollection";
const db = require('internal').db;

function testSuite() {
  return {
    testNoOldSystemCollectionsInSystemDB : function() {
      let collections = db._collections().map((c) => c.name());

      assertNotEqual(0, collections.length);
      assertNotEqual(-1, collections.indexOf('_graphs'));
      assertEqual(-1, collections.indexOf('_fishbowl'));
      assertEqual(-1, collections.indexOf('_modules'));
      assertEqual(-1, collections.indexOf('_routing'));
    },
    
    testNoOldSystemCollectionsInUserDB : function() {
      db._useDatabase("_system");
      db._createDatabase("UnitTestsCollection");
      try {
        db._useDatabase("UnitTestsCollection");
        let collections = db._collections().map((c) => c.name());

        assertNotEqual(0, collections.length);
        assertNotEqual(-1, collections.indexOf('_graphs'));
        assertEqual(-1, collections.indexOf('_fishbowl'));
        assertEqual(-1, collections.indexOf('_modules'));
        assertEqual(-1, collections.indexOf('_routing'));
      } finally {
        db._useDatabase("_system");
        db._dropDatabase("UnitTestsCollection");
      }
    },
    
  };
}

jsunity.run(testSuite);
return jsunity.done();
