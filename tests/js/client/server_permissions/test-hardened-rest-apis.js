/*jshint globalstrict:false, strict:false */
/* global getOptions, runSetup, assertTrue, assertFalse, assertEqual, assertMatch, fail, arango */

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
    'server.harden': 'true',
    'server.authentication': 'true',
    'server.jwt-secret': 'abc123',
    'runSetup': true
  };
}

if (runSetup === true) {
    let users = require("@arangodb/users");
  
  users.save("test_rw", "testi");
  users.grantDatabase("test_rw", "_system", "rw");
  
  users.save("test_ro", "testi");
  users.grantDatabase("test_ro", "_system", "ro");
  
  return true;
}

var jsunity = require('jsunity');

function testSuite() {
  let endpoint = arango.getEndpoint();
  let db = require("@arangodb").db;
  const isCluster = require("internal").isCluster();

  return {
    setUp: function() {},
    tearDown: function() {},

    testCanAccessVersionRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_api/version");
      assertTrue(result.hasOwnProperty("version"));
      assertTrue(result.hasOwnProperty("license"));
      assertMatch(/^\d+\.\d+/, result.version);
    },

    testCanAccessVersionRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_api/version");
      assertFalse(result.hasOwnProperty("version"));
      assertTrue(result.hasOwnProperty("license"));
    },

    testCanAccessEngineRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_api/engine");
      assertTrue(result.hasOwnProperty("name"));
    },

    testCanAccessEngineRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_api/engine");
      assertTrue(result.hasOwnProperty("name"));
    },

    testCanAccessEngineStatsRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_api/engine/stats");
      assertFalse(result.error);
    },

    testCanAccessEngineStatsRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_api/engine/stats");
      assertTrue(result.error);
      assertEqual(403, result.code);
    },

    testCanAccessAdminStatusRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_admin/status");
      assertTrue(result.hasOwnProperty("version"));
      assertTrue(result.hasOwnProperty("serverInfo"));
      assertTrue(result.hasOwnProperty("server"));
      assertTrue(result.hasOwnProperty("pid"));
      assertTrue(result.hasOwnProperty("foxxApi"));
    },

    testCanAccessAdminStatusRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_admin/status");
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertFalse(result.hasOwnProperty("version"));
      assertFalse(result.hasOwnProperty("serverInfo"));
      assertFalse(result.hasOwnProperty("server"));
      assertFalse(result.hasOwnProperty("pid"));
      assertFalse(result.hasOwnProperty("foxxApi"));
    },

    testCanAccessAdminMetricsRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_admin/metrics");
    },

    testCanAccessAdminMetricsRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_admin/metrics");
      assertTrue(result.error);
      assertEqual(403, result.code);
    },

    testCanAccessAdminSystemReportRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_admin/system-report");
    },

    testCanAccessAdminSystemReportRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_admin/system-report");
      assertTrue(result.error);
      assertEqual(403, result.code);
    },

    testCanAccessAdminLogRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_admin/log");
      assertTrue(result.hasOwnProperty("topic"));
      assertTrue(result.hasOwnProperty("level"));
      assertTrue(result.hasOwnProperty("timestamp"));
      assertTrue(result.hasOwnProperty("text"));
    },

    testCanAccessAdminLogRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_admin/log");
      assertTrue(result.error);
      assertEqual(403, result.code);
      assertFalse(result.hasOwnProperty("topic"));
      assertFalse(result.hasOwnProperty("level"));
      assertFalse(result.hasOwnProperty("timestamp"));
      assertFalse(result.hasOwnProperty("text"));
    },

    testCanAccessAdminLogLevelRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      let result = arango.GET("/_admin/log/level");
      assertTrue(result.hasOwnProperty("agency"));
      assertTrue(result.hasOwnProperty("aql"));
      assertTrue(result.hasOwnProperty("cluster"));
      assertTrue(result.hasOwnProperty("general"));
    },

    testCanAccessAdminLogLevelRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      let result = arango.GET("/_admin/log/level");
      assertTrue(result.error);
      assertEqual(403, result.code);
    },
    
    testCanAccessGetNumberOfServersRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      if (isCluster) {
        let result = arango.GET("/_admin/cluster/numberOfServers");
        assertFalse(result.error);
        assertTrue(result.hasOwnProperty("numberOfDBServers"));
        assertTrue(result.hasOwnProperty("numberOfCoordinators"));
        assertTrue(result.hasOwnProperty("cleanedServers"));
      } else {
        let result = arango.GET("/_admin/cluster/numberOfServers");
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("only allowed on coordinators", result.errorMessage);
      }
    },

    testCanAccessGetNumberOfServersRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      if (isCluster) {
        let result = arango.GET("/_admin/cluster/numberOfServers");
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("forbidden", result.errorMessage);
      } else {
        let result = arango.GET("/_admin/cluster/numberOfServers");
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("only allowed on coordinators", result.errorMessage);
      }
    },

    testCanAccessPutNumberOfServersRw : function() {
      arango.reconnect(endpoint, db._name(), "test_rw", "testi");
      const data = {};
      if (isCluster) {
        let result = arango.PUT("/_admin/cluster/numberOfServers", data);
        assertFalse(result.error);
        assertEqual(200, result.code);
      } else {
        let result = arango.PUT("/_admin/cluster/numberOfServers", data);
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("only allowed on coordinators", result.errorMessage);
      }
    },

    testCanAccessPutNumberOfServersRo : function() {
      arango.reconnect(endpoint, db._name(), "test_ro", "testi");
      const data = {};
      if (isCluster) {
        let result = arango.PUT("/_admin/cluster/numberOfServers", data);
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("forbidden", result.errorMessage);
      } else {
        let result = arango.PUT("/_admin/cluster/numberOfServers", data);
        assertTrue(result.error);
        assertEqual(403, result.code);
        assertEqual("only allowed on coordinators", result.errorMessage);
      }
    },

  };
}
jsunity.run(testSuite);
return jsunity.done();
