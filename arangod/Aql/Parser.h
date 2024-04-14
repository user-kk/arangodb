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
#include <string_view>
#include <unordered_map>
#include "Aql/Ast.h"
#include "Aql/AstNode.h"
#include "Aql/Collection.h"
#include "Aql/QueryContext.h"
#include "Aql/Variable.h"
#include "Assertions/Assert.h"
#include "Basics/Common.h"

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

  /// @brief pop an array value from the parser's stack
  /// the array must have been added to the stack via pushArray
  AstNode* popArray();

  /// @brief push an AstNode into the array element on top of the stack
  void pushArrayElement(AstNode*);

  /// @brief push an AstNode into the object element on top of the stack
  void pushObjectElement(char const*, size_t, AstNode*);

  /// @brief push an AstNode into the object element on top of the stack
  void pushObjectElement(AstNode*, AstNode*);

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

  void endSQL() { _sqlContext.pop_back(); }

  void beginSelect() { sqlContext()._isSelect = true; }

  void endSelect() {
    sqlContext()._isSelect = false;
    sqlContext()._willReturnNode = static_cast<AstNode*>(this->peekStack());
  }

  void beginHaving() { sqlContext()._having = true; }

  void endHaving(AstNode* expr) {
    sqlContext()._having = false;
    sqlContext()._havingExprssionNode = expr;
  }

  bool isSelect() {
    if (_sqlContext.empty()) {
      return false;
    }
    return sqlContext()._isSelect;
  }
  bool isHaving() {
    if (_sqlContext.empty()) {
      return false;
    }
    return sqlContext()._having;
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

  void pushHavingPending(AstNode* p, std::string_view s) {
    sqlContext()._havingPendingQueue.push_back({p, s});
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
  void produceAlias();

  /// @brief 生成select中的嵌套子查询
  void produceSelectSubQuery();

  /// @brief 替换掉return节点的被group_by的变量
  /// @param  assignNode group_by产生的赋值节点
  void updateWillReturnNode(AstNode* assignNode);

  AstNode* produceAggregate();

  /// @brief 将待判定的节点变成collection节点或variableRef节点
  /// @warning 这个会清空判定队列
  void executeHavingPend();

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
    /// @brief
    /// 变量映射到在select子句中直接引用该变量的表达式(处理group_by时使用)
    std::unordered_map<Variable*, AstNode*> _selectMap;
    ///@brief 调用了聚集函数的字段的别名和其对应的聚集变量(处理having时使用)
    std::unordered_map<std::string_view, Variable*> _aggAliasToVar;

    bool _isSelect = false;
    bool _having = false;
    bool _isSelectSubQuery = false;  // 是否是select中嵌套的子查询
    bool _allowNULLAlia = true;      // select是否允许空别名
    bool _usedNULLAlia = false;      // 空别名是否被使用
    ///@brief 用于替换select子句中的表达式
    std::deque<std::pair<AstNode*, std::string_view>> _selectPendingQueue;
    ///@brief 用于替换having子句中的表达式
    std::deque<std::pair<AstNode*, std::string_view>> _havingPendingQueue;
    ///@brief 用于替换select子句子查询中的表达式
    std::deque<std::pair<AstNode*, std::string_view>>
        _selectSubQueryPendingQueue;
    ///@brief 记录当前返回的节点
    AstNode* _willReturnNode = nullptr;
    ///@brief 记录当前having表达式的节点
    AstNode* _havingExprssionNode = nullptr;
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
