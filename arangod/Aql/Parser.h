////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
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

#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Aql/Ast.h"
#include "Aql/AstNode.h"
#include "Aql/Collection.h"
#include "Aql/QueryContext.h"
#include "Aql/Variable.h"
#include "Assertions/Assert.h"

namespace arangodb {
namespace aql {
struct AstNode;
class QueryContext;
struct QueryResult;
class QueryString;

/// @brief the parser
class Parser {
  Parser(Parser const&) = delete;

 public:
  /// @brief create the parser
  explicit Parser(QueryContext&, Ast&, QueryString&);

  /// @brief destroy the parser
  ~Parser();

 public:
  /// @brief return the ast during parsing
  Ast* ast() { return &_ast; }

  /// @brief return the query during parsing
  QueryContext& query() { return _query; }

  /// @brief return the scanner
  void* scanner() const { return _scanner; }

  /// @brief a pointer to the start of the query string
  char const* queryStringStart() const { return _queryStringStart; }

  /// @brief return the remaining length of the query string to process
  size_t remainingLength() const { return _remainingLength; }

  /// @brief return the current marker position
  char const* marker() const { return _marker; }

  /// @brief set the current marker position
  void marker(char const* marker) { _marker = marker; }

  /// @brief return the current parse position
  size_t offset() const { return _offset; }

  /// @brief adjust the current parse position
  void increaseOffset(int offset) { _offset += static_cast<size_t>(offset); }

  /// @brief adjust the current parse position
  void increaseOffset(size_t offset) { _offset += offset; }

  void decreaseOffset(int offset) { _offset -= static_cast<size_t>(offset); }

  /// @brief adjust the current parse position
  void decreaseOffset(size_t offset) { _offset -= offset; }

  /// @brief fill the output buffer with a fragment of the query
  void fillBuffer(char* result, size_t length) {
    memcpy(result, _buffer, length);
    _buffer += length;
    _remainingLength -= length;
  }

  /// @brief set data for write queries
  bool configureWriteQuery(AstNode const*, AstNode* optionNode);

  /// @brief parse the query
  void parse();

  /// @brief parse the query and return parse details
  QueryResult parseWithDetails();

  /// @brief register a parse error, position is specified as line / column
  [[noreturn]] void registerParseError(ErrorCode errorCode, char const* format,
                                       std::string_view data, int line,
                                       int column);

  /// @brief register a parse error, position is specified as line / column
  [[noreturn]] void registerParseError(ErrorCode errorCode,
                                       std::string_view data, int line,
                                       int column);

  /// @brief register a warning
  void registerWarning(ErrorCode errorCode, std::string_view data, int line,
                       int column);

  /// @brief push an AstNode array element on top of the stack
  /// the array must be removed from the stack via popArray
  void pushArray(AstNode* array);

  void pushObject();

  /// @brief pop an array value from the parser's stack
  /// the array must have been added to the stack via pushArray
  AstNode* popArray();

  /// @brief push an AstNode into the array element on top of the stack
  void pushArrayElement(AstNode*);

  /// @brief push an AstNode into the object element on top of the stack
  void pushObjectElement(char const*, size_t, AstNode*);

  /// @brief push an AstNode into the object element on top of the stack
  void pushObjectElement(AstNode*, AstNode*);

  void pushObjectElement(std::string_view, std::string_view);
  void pushObjectElement(std::string_view, AstNode*);

  /// @brief push a temporary value on the parser's stack
  void pushStack(void*);

  /// @brief pop a temporary value from the parser's stack
  void* popStack();

  /// @brief peek at a temporary value from the parser's stack
  void* peekStack();

  void beginSQL() {
    bool isSelectSubQuery = isSelect();
    _sqlContext.push_back({});
    sqlContext()._isSelectSubQuery = isSelectSubQuery;
    setSQL();
  }

  void endSQL() {
    _sqlContext.pop_back();
    if (sqlGraphInfo != nullptr && !sqlGraphInfo->startNodeUpdated) {
      _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                      "startNode is not be set");
    }
  }

  void beginSelect() { sqlContext()._isSelect = true; }

  void endSelect() {
    sqlContext()._isSelect = false;
    sqlContext()._willReturnNode = static_cast<AstNode*>(this->peekStack());
  }

  void setHaving(AstNode* expr) { sqlContext()._havingExprssionNode = expr; }

  bool isSelect() {
    if (_sqlContext.empty()) {
      return false;
    }
    return sqlContext()._isSelect;
  }
  bool isSelectSubQuery() {
    if (_sqlContext.empty()) {
      return false;
    }
    return sqlContext()._isSelectSubQuery;
  }

  bool isSQL() { return _isSQL; }

  void setSQL() { _isSQL = true; }

  void setAQL() { _isSQL = false; }

  void pushSelectPending(AstNode* p, std::string_view s) {
    sqlContext()._selectPendingQueue.push_back({p, s});
  }

  void pushAliasQueue(AstNode* p, std::string_view s) {
    sqlContext()._selectAliasQueue.push_back({p, s});
  }

  void pushSelectSubQueryQueue(AstNode* p) {
    sqlContext()._selectSubQueryQueue.push_back(p);
  }

  void pushSelectSubQueryPending(AstNode* p, std::string_view s) {
    TRI_ASSERT(_sqlContext.size() >= 2);
    _sqlContext[_sqlContext.size() - 2]._selectSubQueryPendingQueue.push_back(
        {p, s});
  }

  /// @brief 将待判定的节点变成collection节点或variableRef节点
  /// @warning 这个会清空判定队列和select_map
  void executeSelectPend();

  /// @brief 将待判定的节点变成collection节点或variableRef节点
  /// @warning 这个会清空判定队列
  void executeSelectSubQueryPend();

  /// @brief 将待判定的节点变成collection节点或variableRef节点
  void executeSelectPendWithoutPop();

  /// @brief 生成别名的let节点
  /// @warning 聚集函数不会在这个阶段显式生成let节点,防止其在where子句中被引用
  void produceAlias();

  /// @brief 生成聚集函数别名的let节点
  /// @details
  /// 聚集函数的别名应该被在having或order_by中使用,在进入having子句之前,
  /// 需要调用produceAggregateStep1(),产生变量,但不产生let子句节点
  /// 在collect节点生成后需要调用此函数来产生let子句节点
  void produceAggAlias();

  /// @brief 生成select中的嵌套子查询
  void produceSelectSubQuery();

  /// @brief 替换掉return节点的被group_by的变量
  /// @param  assignNode group_by产生的赋值节点
  void updateWillReturnNode(AstNode* assignNode);

  ///@brief 产生SQLContext的_aggArrayNode节点
  /// 用于处理select中调用的聚集函数，同时生成聚集函数的别名
  ///@warning 只生成变量，不生成let节点，let节点要在collect节点生成后生成
  void produceAggregateStep1();

  ///@brief 用于处理having中调用的聚集函数，返回aggArrayNode节点
  AstNode* produceAggregateStep2();

  void disableNULLAlia() { sqlContext()._allowNULLAlia = false; }

  bool allowNULLAlia() { return sqlContext()._allowNULLAlia; }

  void useNULLAlia() { sqlContext()._usedNULLAlia = true; }

  bool usedNULLAlia() { return sqlContext()._usedNULLAlia; }

  bool checkVariableNameIsCurrent(std::string_view variableName) {
    if (_ast.scopes()->canUseCurrentVariable() &&
        (variableName == Variable::NAME_CURRENT ||
         variableName == Variable::NAME_CURRENT.substr(1) ||
         variableName == Variable::NAME_CURRENT_Alias)) {
      return true;
    } else {
      return false;
    }
  }

  void kk();

  void addSQLCollectionNode(AstNode* p) { _sqlCollectionNodes.insert(p); }

  void addSQLCollection() {
    for (auto i : _sqlCollectionNodes) {
      if (i != nullptr && i->type == NODE_TYPE_COLLECTION) {
        _ast.query().collections().add(i->getString(),
                                       arangodb::AccessMode::Type::READ,
                                       aql::Collection::Hint::None);
      }
    }
    _sqlCollectionNodes.clear();
  }
  void removeSQLCollectionFromRoot(AstNode* root) {
    std::vector<AstNode*> needRemove = root->find(
        [this](AstNode* p) { return _sqlCollectionNodes.contains(p); },
        [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
    for (auto i : needRemove) {
      _sqlCollectionNodes.erase(i);
    }
  }
  ///@brief 将having中的group_by变量替换
  void processHaving();
  ///@brief 将order_by中的group_by变量替换
  void processOrderBy(AstNode* arrayNode);

  struct SQLGraphInfo {
    AstNode* varNodes = nullptr;
    AstNode* directionNode = nullptr;
    AstNode* startNode = nullptr;
    AstNode* endNode = nullptr;
    AstNode* collectionNode = nullptr;
    AstNode* nodeTraversal = nullptr;
    std::vector<AstNode*> varNames;
    bool startNodeUpdated = false;
  };

  std::unique_ptr<SQLGraphInfo> sqlGraphInfo;

  void beginGraph() {
    if (sqlGraphInfo != nullptr && !sqlGraphInfo->startNodeUpdated) {
      _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                      "startNode is not be set");
    }
    sqlGraphInfo = std::make_unique<SQLGraphInfo>();
  }

  ///@brief 这个变量只是暂时的,后面会被修改
  void setGraphVarNodes(AstNode* v1, AstNode* e, AstNode* v2, AstNode* path) {
    if (path->type != NODE_TYPE_NOP && e->type == NODE_TYPE_NOP) {
      _query.warnings().registerError(
          TRI_ERROR_QUERY_PARSE,
          "Edge nodes must be bound when paths are bound");
      return;
    }

    sqlGraphInfo->varNames.push_back(v1);
    sqlGraphInfo->varNames.push_back(e);
    sqlGraphInfo->varNames.push_back(v2);
    sqlGraphInfo->varNames.push_back(path);

    sqlGraphInfo->startNode = _ast.createNodeValueInt(1);

    AstNode* array = _ast.createNodeArray();
    array->members.push_back(_ast.createNodeValueInt(1));
    array->members.push_back(_ast.createNodeValueInt(1));

    sqlGraphInfo->varNodes = array;
  }

  void setGraphVarNodesAllShortest(AstNode* v1, AstNode* e, AstNode* v2,
                                   AstNode* path) {
    if (path->type == NODE_TYPE_NOP) {
      _query.warnings().registerError(
          TRI_ERROR_QUERY_PARSE,
          "path must be bound in all_shortest/k_shortest/any_k");
      return;
    }

    if (e->type != NODE_TYPE_NOP) {
      _query.warnings().registerError(
          TRI_ERROR_QUERY_PARSE,
          "edge cannot be bound in all_shortest/k_shortest/any_k");
      return;
    }

    sqlGraphInfo->varNames.push_back(v1);
    sqlGraphInfo->varNames.push_back(v2);

    sqlGraphInfo->startNode = _ast.createNodeValueInt(1);
    sqlGraphInfo->endNode = _ast.createNodeValueInt(1);

    AstNode* array = _ast.createNodeArray();
    AstNode* pathNode = _ast.createNodeVariable(path->getStringView(), true);
    array->members.push_back(pathNode);

    sqlGraphInfo->varNodes = array;
  }

  void setGraphVarNodes(AstNode* v1, AstNode* e, AstNode* v2,
                        std::string_view pathPointName) {
    sqlGraphInfo->varNames.push_back(v1);
    sqlGraphInfo->varNames.push_back(v2);

    sqlGraphInfo->startNode = _ast.createNodeValueInt(1);
    sqlGraphInfo->endNode = _ast.createNodeValueInt(1);

    AstNode* array = _ast.createNodeArray();
    AstNode* pathPointNode = _ast.createNodeVariable(pathPointName, true);
    array->members.push_back(pathPointNode);

    if (e->type != NODE_TYPE_NOP) {
      AstNode* edge = _ast.createNodeVariable(e->getStringView(), true);
      array->members.push_back(edge);
    }

    sqlGraphInfo->varNodes = array;
  }

  AstNode* buildNodeTraversal(AstNode* option) {
    auto infoNode = _ast.createNodeArray();
    infoNode->addMember(sqlGraphInfo->directionNode);
    infoNode->addMember(sqlGraphInfo->startNode);
    infoNode->addMember(sqlGraphInfo->collectionNode);
    infoNode->addMember(_ast.createNodeNop());
    infoNode->addMember(option);

    sqlGraphInfo->nodeTraversal =
        _ast.createNodeTraversal(sqlGraphInfo->varNodes, infoNode);
    return sqlGraphInfo->nodeTraversal;
  }

  AstNode* buildNodeAnyShortest(AstNode* option) {
    auto infoNode = _ast.createNodeArray();
    infoNode->addMember(sqlGraphInfo->directionNode);
    infoNode->addMember(sqlGraphInfo->startNode);
    infoNode->addMember(sqlGraphInfo->endNode);
    infoNode->addMember(sqlGraphInfo->collectionNode);
    infoNode->addMember(option);

    return _ast.createNodeShortestPath(sqlGraphInfo->varNodes, infoNode);
  }

  AstNode* buildNodeAllShortest() {
    auto infoNode = _ast.createNodeArray();
    infoNode->addMember(sqlGraphInfo->directionNode);
    infoNode->addMember(sqlGraphInfo->startNode);
    infoNode->addMember(sqlGraphInfo->endNode);
    infoNode->addMember(sqlGraphInfo->collectionNode);
    infoNode->addMember(_ast.createNodeNop());

    return _ast.createNodeEnumeratePaths(
        arangodb::graph::PathType::Type::AllShortestPaths,
        sqlGraphInfo->varNodes, infoNode);
  }

  AstNode* buildNodeKShortest(AstNode* option) {
    auto infoNode = _ast.createNodeArray();
    infoNode->addMember(sqlGraphInfo->directionNode);
    infoNode->addMember(sqlGraphInfo->startNode);
    infoNode->addMember(sqlGraphInfo->endNode);
    infoNode->addMember(sqlGraphInfo->collectionNode);
    infoNode->addMember(option);

    return _ast.createNodeEnumeratePaths(
        arangodb::graph::PathType::Type::KShortestPaths, sqlGraphInfo->varNodes,
        infoNode);
  }

  AstNode* buildNodeAnyK(AstNode* option) {
    auto infoNode = _ast.createNodeArray();
    infoNode->addMember(sqlGraphInfo->directionNode);
    infoNode->addMember(sqlGraphInfo->startNode);
    infoNode->addMember(sqlGraphInfo->endNode);
    infoNode->addMember(sqlGraphInfo->collectionNode);
    infoNode->addMember(option);

    return _ast.createNodeEnumeratePaths(
        arangodb::graph::PathType::Type::KPaths, sqlGraphInfo->varNodes,
        infoNode);
  }

  AstNode* buildNodeDirection(int direction, AstNode* step,
                              bool mustStep = false) {
    if (mustStep) {
      if (step->type == NODE_TYPE_NOP) {
        _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                        "step must be set");
        return nullptr;
      }
      TRI_ASSERT(step->type == NODE_TYPE_RANGE);
      return _ast.createNodeDirection(direction, step);
    }
    if (step->type == NODE_TYPE_NOP) {
      return _ast.createNodeDirection(direction, 1);
    }
    TRI_ASSERT(step->type == NODE_TYPE_RANGE);
    return _ast.createNodeDirection(direction, step);
  }

  ///@brief 设置起始点,根据起始点生成变量和重新设置方向
  bool updateStartNode(AstNode* expression, std::string_view startNodeName);
  ///@brief 设置起始点和终止点,根据起始点重新设置方向
  bool updateStartEndNode(AstNode* startExpression,
                          std::string_view startNodeName,
                          AstNode* endExpression, std::string_view endNodeName);

 private:
  struct SQLContext;

  [[nodiscard]] SQLContext& sqlContext() {
    TRI_ASSERT(!_sqlContext.empty());
    return _sqlContext.back();
  }

 private:
  /// @brief a pointer to the start of the query string
  QueryString const& queryString() const { return _queryString; }

  /// @brief the query
  QueryContext& _query;

  /// @brief abstract syntax tree for the query, build during parsing
  Ast& _ast;

  /// @brief query string (non-owning!)
  QueryString& _queryString;

  /// @brief lexer / scanner used when parsing the query (Aql/tokens.ll)
  void* _scanner;

  char const* _queryStringStart;

  /// @brief currently processed part of the query string
  char const* _buffer;

  /// @brief remaining length of the query string, used during parsing
  size_t _remainingLength;

  /// @brief current offset into query string, used during parsing
  size_t _offset;

  /// @brief pointer into query string, used temporarily during parsing
  char const* _marker;

  /// @brief a stack of things, used temporarily during parsing
  std::vector<void*> _stack;

  struct SQLContext {
    /// @brief 变量映射到在select子句中直接引用该变量的表达式
    /// @details 处理group_by时使用
    std::unordered_map<Variable*, AstNode*> _selectMap;
    ///@brief 暂存聚集函数别名的let节点,collect节点生成后将要被添加到ast中
    std::vector<AstNode*> _aggAliasLetNodes;
    ///@brief group_by的表达式节点与被替换后的引用节点
    /// @details
    /// 在updateWillReturnNode()函数中被置入,在produceAggregateStep2()被使用
    std::vector<std::pair<AstNode*, AstNode*>> _groupByNodes;
    bool _isSelect = false;
    bool _isSelectSubQuery = false;  // 是否是select中嵌套的子查询
    bool _allowNULLAlia = true;      // select是否允许空别名
    bool _usedNULLAlia = false;      // 空别名是否被使用
    ///@brief 用于替换select子句中的表达式
    std::deque<std::pair<AstNode*, std::string_view>> _selectPendingQueue;
    ///@brief 用于替换select子句子查询中的表达式
    std::deque<std::pair<AstNode*, std::string_view>>
        _selectSubQueryPendingQueue;
    ///@brief 记录当前返回的节点
    AstNode* _willReturnNode = nullptr;
    ///@brief 记录当前having表达式的节点
    AstNode* _havingExprssionNode = nullptr;
    ///@brief 将要被放入collect节点中的聚集数组节点
    ///@details 由produceAggregateStep1()生成
    AstNode* _aggArrayNode = nullptr;
    ///@brief 用于生成let子句(处理select子句中的别名)
    std::deque<std::pair<AstNode*, std::string_view>> _selectAliasQueue;
    ///@brief 用于生成select子查询中的let子句(延迟生成select子句中的子查询)
    std::deque<AstNode*> _selectSubQueryQueue;
  };

  std::unordered_set<AstNode*> _sqlCollectionNodes;
  std::deque<SQLContext> _sqlContext;
  bool _isSQL = false;
};
}  // namespace aql
}  // namespace arangodb

/// @brief forward for the parse function provided by the parser (.y)
int Aqlparse(arangodb::aql::Parser*);

/// @brief forward for the init function provided by the lexer (.l)
int Aqllex_init(void**);

/// @brief forward for the shutdown function provided by the lexer (.l)
int Aqllex_destroy(void*);

/// @brief forward for the context function provided by the lexer (.l)
void Aqlset_extra(arangodb::aql::Parser*, void*);
