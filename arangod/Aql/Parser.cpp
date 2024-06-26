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

#include "Parser.h"

#include "Aql/Function.h"
#include "Aql/Variable.h"
#include "Assertions/Assert.h"
#include "Basics/Common.h"
#include "Basics/ScopeGuard.h"
#include "Aql/AstNode.h"
#include "Aql/ExecutionPlan.h"
#include "Aql/QueryContext.h"
#include "Aql/QueryResult.h"
#include "Aql/QueryString.h"
#include "Aql/Aggregator.h"

#include <absl/strings/str_cat.h>

#include <string_view>
#include <unordered_map>
#include <vector>

using namespace arangodb::aql;

/// @brief create the parser
Parser::Parser(QueryContext& query, Ast& ast, QueryString& qs)
    : _query(query),
      _ast(ast),
      _queryString(qs),
      _scanner(nullptr),
      _queryStringStart(nullptr),
      _buffer(nullptr),
      _remainingLength(0),
      _offset(0),
      _marker(nullptr),
      _stack() {
  _stack.reserve(4);

  _queryStringStart = _queryString.data();
  _buffer = qs.data();
  _remainingLength = qs.size();
}

/// @brief destroy the parser
Parser::~Parser() = default;

/// @brief set data for write queries
bool Parser::configureWriteQuery(AstNode const* collectionNode,
                                 AstNode* optionNode) {
  bool isExclusiveAccess = false;

  if (optionNode != nullptr) {
    // already validated at parse-time
    TRI_ASSERT(optionNode->isObject());
    TRI_ASSERT(optionNode->isConstant());
    isExclusiveAccess = ExecutionPlan::hasExclusiveAccessOption(optionNode);
  }

  // now track which collection is going to be modified
  _ast.addWriteCollection(collectionNode, isExclusiveAccess);

  // register that we have seen a modification operation
  _ast.setContainsModificationNode();

  return true;
}

/// @brief parse the query
void Parser::parse() {
  if (queryString().empty() || remainingLength() == 0) {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_QUERY_EMPTY);
  }

  // start main scope
  auto scopes = _ast.scopes();
  scopes->start(AQL_SCOPE_MAIN);

  TRI_ASSERT(_scanner == nullptr);
  if (Aqllex_init(&_scanner) != 0) {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_OUT_OF_MEMORY);
  }

  TRI_ASSERT(_scanner != nullptr);
  Aqlset_extra(this, _scanner);

  auto guard = scopeGuard([this]() noexcept {
    Aqllex_destroy(_scanner);
    _scanner = nullptr;
  });

  // parse the query string
  if (Aqlparse(this)) {
    // lexing/parsing failed
    THROW_ARANGO_EXCEPTION(TRI_ERROR_QUERY_PARSE);
  }

  // end main scope
  TRI_ASSERT(scopes->numActive() > 0);

  while (scopes->numActive() > 0) {
    scopes->endCurrent();
  }

  TRI_ASSERT(scopes->numActive() == 0);
  addSQLCollection();
}

/// @brief parse the query and retun parse details
QueryResult Parser::parseWithDetails() {
  parse();

  QueryResult result;
  result.collectionNames = _query.collections().collectionNames();
  result.bindParameters = _ast.bindParameters();
  auto builder = std::make_shared<VPackBuilder>();
  _ast.toVelocyPack(*builder, false);
  result.data = std::move(builder);

  return result;
}

/// @brief register a parse error, position is specified as line / column
void Parser::registerParseError(ErrorCode errorCode, char const* format,
                                std::string_view data, int line, int column) {
  char buffer[512];
  // make sure the buffer is always initialized
  buffer[0] = '\0';
  buffer[sizeof(buffer) - 1] = '\0';

  snprintf(buffer, sizeof(buffer) - 1, format, data.data());

  registerParseError(errorCode, buffer, line, column);
}

/// @brief register a parse error, position is specified as line / column
void Parser::registerParseError(ErrorCode errorCode, std::string_view data,
                                int line, int column) {
  TRI_ASSERT(errorCode != TRI_ERROR_NO_ERROR);
  TRI_ASSERT(data.data() != nullptr);

  // note: line numbers reported by bison/flex start at 1, columns start at 0
  auto errorMessage =
      absl::StrCat(data, " near '", queryString().extractRegion(line, column),
                   "' at position ", line, ":", (column + 1));

  _query.warnings().registerError(errorCode, errorMessage);
}

/// @brief register a warning
void Parser::registerWarning(ErrorCode errorCode, std::string_view data,
                             [[maybe_unused]] int line,
                             [[maybe_unused]] int column) {
  // ignore line and column for now
  _query.warnings().registerWarning(errorCode, data);
}

/// @brief push an AstNode array element on top of the stack
/// the array must be removed from the stack via popArray
void Parser::pushArray(AstNode* array) {
  TRI_ASSERT(array != nullptr);
  TRI_ASSERT(array->type == NODE_TYPE_ARRAY);
  array->setFlag(DETERMINED_CONSTANT, VALUE_CONSTANT);
  pushStack(array);
}

void Parser::pushObject() { pushStack(_ast.createNodeObject()); }

/// @brief pop an array value from the parser's stack
/// the array must have been added to the stack via pushArray
AstNode* Parser::popArray() {
  AstNode* array = static_cast<AstNode*>(popStack());
  TRI_ASSERT(array->type == NODE_TYPE_ARRAY);
  return array;
}

/// @brief push an AstNode into the array element on top of the stack
void Parser::pushArrayElement(AstNode* node) {
  TRI_ASSERT(node != nullptr);
  auto array = static_cast<AstNode*>(peekStack());
  TRI_ASSERT(array->type == NODE_TYPE_ARRAY);
  array->addMember(node);
  if (array->hasFlag(AstNodeFlagType::VALUE_CONSTANT) && !node->isConstant()) {
    array->removeFlag(AstNodeFlagType::VALUE_CONSTANT);
  }
}

/// @brief push an AstNode into the object element on top of the stack
void Parser::pushObjectElement(char const* attributeName, size_t nameLength,
                               AstNode* node) {
  auto object = static_cast<AstNode*>(peekStack());
  TRI_ASSERT(object != nullptr);
  TRI_ASSERT(object->type == NODE_TYPE_OBJECT);
  auto element = _ast.createNodeObjectElement(
      std::string_view(attributeName, nameLength), node);
  object->addMember(element);
}

/// @brief push an AstNode into the object element on top of the stack
void Parser::pushObjectElement(AstNode* attributeName, AstNode* node) {
  auto object = static_cast<AstNode*>(peekStack());
  TRI_ASSERT(object != nullptr);
  TRI_ASSERT(object->type == NODE_TYPE_OBJECT);
  auto element = _ast.createNodeCalculatedObjectElement(attributeName, node);
  object->addMember(element);
}

void Parser::pushObjectElement(std::string_view key, std::string_view value) {
  const char* keyStr = _ast.resources().registerString(key);
  const char* valueStr = _ast.resources().registerString(value);

  AstNode* valueNode = _ast.createNodeValueString(valueStr, value.length());

  pushObjectElement(keyStr, key.length(), valueNode);
}

void Parser::pushObjectElement(std::string_view key, AstNode* node) {
  const char* keyStr = _ast.resources().registerString(key);

  pushObjectElement(keyStr, key.length(), node);
}

/// @brief push a temporary value on the parser's stack
void Parser::pushStack(void* value) {
  TRI_ASSERT(value != nullptr);
  _stack.emplace_back(value);
}

/// @brief pop a temporary value from the parser's stack
void* Parser::popStack() {
  TRI_ASSERT(!_stack.empty());

  void* result = _stack.back();
  TRI_ASSERT(result != nullptr);
  _stack.pop_back();
  return result;
}

/// @brief peek at a temporary value from the parser's stack
void* Parser::peekStack() {
  TRI_ASSERT(!_stack.empty());

  return _stack.back();
}

void Parser::executeSelectPend() {
  AstNode* pendingNode = nullptr;
  SQLContext& ctx = sqlContext();
  while (!ctx._selectPendingQueue.empty()) {
    pendingNode = ctx._selectPendingQueue.front().first;

    std::string_view variableName = ctx._selectPendingQueue.front().second;
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (this->checkVariableNameIsCurrent(variableName)) {
        variable = _ast.scopes()->getCurrentVariable();
      }
    }
    AstNode* node = nullptr;
    if (variable != nullptr) {
      // variable alias exists, now use it
      node = _ast.createNodeReference(variable);
    }

    if (node == nullptr) {  // 为collection时
      // variable not found. so it must have been a collection or view
      auto const& resolver = this->query().resolver();
      node = _ast.createNodeDataSource(resolver, variableName,
                                       arangodb::AccessMode::Type::READ, true,
                                       false, true);
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
    ctx._selectPendingQueue.pop_front();
  }
  ctx._selectMap.clear();
}

void Parser::updateWillReturnNode(AstNode* assignNode) {
  TRI_ASSERT(assignNode->type == NODE_TYPE_ASSIGN &&
             assignNode->members.size() == 2 &&
             assignNode->members[0]->type == NODE_TYPE_VARIABLE);
  SQLContext& ctx = sqlContext();
  AstNode* member = assignNode->members[1];

  Variable* collectVar =
      static_cast<Variable*>(assignNode->members[0]->getData());
  AstNode* refNode = _ast.createNodeReference(collectVar);
  ctx._groupByNodes.push_back({member, refNode});

  // 检查聚集时使用的是变量还是表达式
  if (member->type == NODE_TYPE_REFERENCE) {
    Variable* v = static_cast<Variable*>(member->getData());
    TRI_ASSERT(v != nullptr);
    if (ctx._selectMap.contains(v)) {
      // 寻找到对应的节点并替换
      removeSQLCollectionFromRoot(ctx._selectMap[v]);
      *(ctx._selectMap[v]) = *refNode;
    }
  } else {
    std::vector<AstNode*> needReplace = ctx._willReturnNode->find(
        [member](AstNode* p) { return AstNode::equal(p, member); },
        [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
    for (AstNode* node : needReplace) {
      removeSQLCollectionFromRoot(node);
      *node = *refNode;
    }
  }
}

void arangodb::aql::Parser::produceAlias() {
  SQLContext& ctx = sqlContext();
  for (auto [exprNode, name] : ctx._selectAliasQueue) {
    if (exprNode->type == NODE_TYPE_FCALL) {
      // 现在找到了f_call的节点,检查是否是聚集函数
      auto f = static_cast<arangodb::aql::Function*>(exprNode->getData());
      if (Aggregator::isValid(f->name)) {
        continue;  // 聚集函数不产生别名
      }
      // 普通函数生成别名,除了row_number和dense_rank函数
      // 这两个函数如果生成别名会被不同的节点连续重复调用两次,这是被禁止的
      if (f->name == "ROW_NUMBER" || f->name == "DENSE_RANK") {
        continue;
      }
    }

    Variable* vPtr = nullptr;
    auto node = _ast.createNodeLet(name, exprNode, true, vPtr);
    TRI_ASSERT(vPtr != nullptr);
    ctx._selectMap.insert({vPtr, exprNode});
    _ast.addOperation(node);
  }
  ctx._selectAliasQueue.clear();
}

void arangodb::aql::Parser::executeSelectPendWithoutPop() {
  SQLContext& ctx = sqlContext();
  for (auto& [pendingNode, variableName] : ctx._selectPendingQueue) {
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (this->checkVariableNameIsCurrent(variableName)) {
        variable = _ast.scopes()->getCurrentVariable();
      }
    }
    AstNode* node = nullptr;
    if (variable != nullptr) {
      // variable alias exists, now use it
      node = _ast.createNodeReference(variable);
    }

    if (node == nullptr) {  // 为collection时
      // variable not found. so it must have been a collection or view
      auto const& resolver = this->query().resolver();
      node = _ast.createNodeDataSource(resolver, variableName,
                                       arangodb::AccessMode::Type::READ, true,
                                       false, true);
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
  }
}

void arangodb::aql::Parser::produceAggregateStep1() {
  // 聚集函数的节点和它的别名
  std::unordered_map<AstNode*, std::string_view> map;
  SQLContext& ctx = sqlContext();
  // 遍历找到fcall的节点,检查是否是聚集函数
  for (auto& objectElementNode : ctx._willReturnNode->members) {
    if (objectElementNode->type != NODE_TYPE_OBJECT_ELEMENT) {
      break;
    }
    TRI_ASSERT(objectElementNode->members.size() == 1);
    if (objectElementNode->members[0]->type != NODE_TYPE_FCALL) {
      continue;
    }

    // 现在找到了f_call的节点,检查是否是聚集函数
    AstNode* fNode = objectElementNode->members[0];
    auto f = static_cast<arangodb::aql::Function*>(fNode->getData());
    if (!Aggregator::isValid(f->name)) {
      continue;
    }

    // select临时生成的别名不会被加入map
    if (objectElementNode->getStringView() == "_") {
      continue;
    }

    // 将是聚集函数的节点和它的别名保存起来
    map.insert({fNode, objectElementNode->getStringView()});
  }

  // 要返回的结果
  AstNode* aggArrayNode = _ast.createNodeArray();

  std::vector<AstNode*> fAggNodes = ctx._willReturnNode->find(
      [](AstNode* p) -> bool {
        // 找到fcall的节点,检查是否是聚集函数
        if (p->type != NODE_TYPE_FCALL) {
          return false;
        }
        auto f = static_cast<arangodb::aql::Function*>(p->getData());
        if (!Aggregator::isValid(f->name)) {
          return false;
        }

        return true;
      },
      [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });

  /// 调用了聚集函数的字段的别名和其对应的聚集变量
  std::unordered_map<std::string_view, Variable*> aggAliasToVar;

  for (AstNode* fNode : fAggNodes) {
    // 现在确定了是聚集函数
    AstNode* fAggNode = fNode->clone(&_ast);

    // 生成一个聚集变量
    std::string vName = _ast.variables()->nextName();
    // 生成赋值节点
    AstNode* assignNode =
        _ast.createNodeAssign(vName.c_str(), vName.size(), fAggNode, false);
    // 添加到要返回的数组node中
    aggArrayNode->addMember(assignNode);

    // 修改
    Variable* var = static_cast<Variable*>(assignNode->members[0]->getData());
    if (map.contains(fNode)) {
      aggAliasToVar.insert({map[fNode], var});
    }
    AstNode* refNode = _ast.createNodeReference(var);
    removeSQLCollectionFromRoot(fNode);
    *fNode = *refNode;
  }
  ctx._aggAliasLetNodes.reserve(aggAliasToVar.size());

  for (auto [vName, var] : aggAliasToVar) {
    AstNode* refNode = _ast.createNodeReference(var);
    ctx._aggAliasLetNodes.push_back(_ast.createNodeLet(vName, refNode, true));
  }

  ctx._aggArrayNode = aggArrayNode;
};

arangodb::aql::AstNode* arangodb::aql::Parser::produceAggregateStep2() {
  SQLContext& ctx = sqlContext();
  AstNode* aggArrayNode = ctx._aggArrayNode;
  if (ctx._havingExprssionNode == nullptr) {
    return aggArrayNode;
  }

  // 现在having中一定有值,修改having中的聚集表达式,使之成为聚集变量
  std::vector<AstNode*> fAggNodesInHaving = ctx._havingExprssionNode->find(
      [](AstNode* p) -> bool {
        // 找到fcall的节点,检查是否是聚集函数
        if (p->type != NODE_TYPE_FCALL) {
          return false;
        }
        auto f = static_cast<arangodb::aql::Function*>(p->getData());
        if (!Aggregator::isValid(f->name)) {
          return false;
        }

        return true;
      },
      [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
  for (AstNode* fNode : fAggNodesInHaving) {
    // 现在确定了是聚集函数
    AstNode* fAggNode = fNode->clone(&_ast);

    // 生成一个聚集变量
    std::string vName = _ast.variables()->nextName();
    // 生成赋值节点
    AstNode* assignNode =
        _ast.createNodeAssign(vName.c_str(), vName.size(), fAggNode, false);
    // 添加到要返回的数组node中
    aggArrayNode->addMember(assignNode);

    // 修改
    Variable* var = static_cast<Variable*>(assignNode->members[0]->getData());
    AstNode* refNode = _ast.createNodeReference(var);
    removeSQLCollectionFromRoot(fNode);
    *fNode = *refNode;
  }

  return aggArrayNode;
}

void arangodb::aql::Parser::kk() {
  TRI_ASSERT(!_sqlContext.empty());
  return;
}

void arangodb::aql::Parser::produceSelectSubQuery() {
  auto& ctx = sqlContext();
  for (auto node : ctx._selectSubQueryQueue) {
    _ast.addOperation(node);
  }
  ctx._selectSubQueryQueue.clear();
}

void arangodb::aql::Parser::executeSelectSubQueryPend() {
  AstNode* pendingNode = nullptr;
  SQLContext& ctx = sqlContext();
  while (!ctx._selectSubQueryPendingQueue.empty()) {
    pendingNode = ctx._selectSubQueryPendingQueue.front().first;

    std::string_view variableName =
        ctx._selectSubQueryPendingQueue.front().second;
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (this->checkVariableNameIsCurrent(variableName)) {
        variable = _ast.scopes()->getCurrentVariable();
      }
    }
    AstNode* node = nullptr;
    if (variable != nullptr) {
      // variable alias exists, now use it
      node = _ast.createNodeReference(variable);
    }

    if (node == nullptr) {  // 为collection时
      // variable not found. so it must have been a collection or view
      auto const& resolver = this->query().resolver();
      node = _ast.createNodeDataSource(resolver, variableName,
                                       arangodb::AccessMode::Type::READ, true,
                                       false, true);
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
    ctx._selectSubQueryPendingQueue.pop_front();
  }
}

void arangodb::aql::Parser::produceAggAlias() {
  SQLContext& ctx = sqlContext();

  for (auto i : ctx._aggAliasLetNodes) {
    // 调用这个函数时已经进入了collect的scope,需要重新注册
    AstNode* varNode = i->getMemberUnchecked(0);
    TRI_ASSERT(varNode->type == NODE_TYPE_VARIABLE);
    auto v = static_cast<Variable*>(varNode->getData());
    _ast.scopes()->addVariable(v);
    // 添加let节点
    _ast.addOperation(i);
  }
  ctx._aggAliasLetNodes.clear();
}

void arangodb::aql::Parser::processHaving() {
  SQLContext& ctx = sqlContext();
  if (ctx._havingExprssionNode == nullptr) {
    return;
  }
  for (auto [groupNode, refNode] : ctx._groupByNodes) {
    // 检查聚集时使用的是变量还是表达式
    if (groupNode->type == NODE_TYPE_REFERENCE) {
      Variable* v = static_cast<Variable*>(groupNode->getData());
      TRI_ASSERT(v != nullptr);
      std::string_view name = v->name;
      std::vector<AstNode*> needReplace = ctx._havingExprssionNode->find(
          [name](AstNode* p) {
            if (p->type != NODE_TYPE_REFERENCE) {
              return false;
            }

            if (Variable* v = static_cast<Variable*>(p->getData());
                v->name != name) {
              return false;
            }
            return true;
          },
          [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
      for (AstNode* node : needReplace) {
        // 替换
        removeSQLCollectionFromRoot(node);
        *node = *refNode;
      }

    } else {
      std::vector<AstNode*> needReplace = ctx._havingExprssionNode->find(
          [groupNode](AstNode* p) { return AstNode::equal(p, groupNode); },
          [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
      for (AstNode* node : needReplace) {
        // 替换
        removeSQLCollectionFromRoot(node);
        *node = *refNode;
      }
    }
  }
}

void arangodb::aql::Parser::processOrderBy(AstNode* arrayNode) {
  SQLContext& ctx = sqlContext();
  TRI_ASSERT(arrayNode->type == NODE_TYPE_ARRAY);
  size_t n = arrayNode->numMembers();

  for (size_t i = 0; i < n; i++) {
    AstNode* expNode = arrayNode->getMemberUnchecked(i);
    for (auto [groupNode, refNode] : ctx._groupByNodes) {
      if (groupNode->type == NODE_TYPE_REFERENCE) {
        Variable* v = static_cast<Variable*>(groupNode->getData());
        TRI_ASSERT(v != nullptr);
        std::string_view name = v->name;
        std::vector<AstNode*> needReplace = expNode->find(
            [name](AstNode* p) {
              if (p->type == NODE_TYPE_COLLECTION &&
                  p->getStringView() == name) {
                return true;
              }
              return false;
            },
            [](AstNode* p) { return p->type == NODE_TYPE_SUBQUERY; });
        for (AstNode* node : needReplace) {
          // 替换
          removeSQLCollectionFromRoot(node);
          *node = *refNode;
        }
      }
    }
  }
};
bool arangodb::aql::Parser::updateStartNode(AstNode* expression,
                                            std::string_view startNodeName) {
  if (sqlGraphInfo == nullptr) {
    return false;
  }

  if (sqlGraphInfo->startNodeUpdated) {
    return false;
  }

  // 重新设置方向和重新生成变量

  auto it =
      std::ranges::find_if(sqlGraphInfo->varNames, [startNodeName](AstNode* p) {
        if (p->type != NODE_TYPE_NOP && p->getString() == startNodeName) {
          return true;
        }
        return false;
      });
  if (it == sqlGraphInfo->varNames.end()) {
    return false;
  }
  auto pos = it - sqlGraphInfo->varNames.begin();
  switch (pos) {
    case 0: {
      // 方向不用重新设置,只需要生成变量
      // point
      AstNode* point = _ast.createNodeVariable(
          sqlGraphInfo->varNames[2]->getStringView(), true);
      *(sqlGraphInfo->varNodes->members[0]) = *point;
      // edge
      if (sqlGraphInfo->varNames[1]->type != NODE_TYPE_NOP) {
        AstNode* edge = _ast.createNodeVariable(
            sqlGraphInfo->varNames[1]->getStringView(), true);
        *(sqlGraphInfo->varNodes->members[1]) = *edge;
      } else {
        if (sqlGraphInfo->nodeTraversal != nullptr) {
          sqlGraphInfo->nodeTraversal->members.pop_back();
        } else {
          sqlGraphInfo->varNodes->members.pop_back();
        }
      }
      // path
      if (sqlGraphInfo->varNames[3]->type != NODE_TYPE_NOP) {
        AstNode* path = _ast.createNodeVariable(
            sqlGraphInfo->varNames[3]->getStringView(), true);
        if (sqlGraphInfo->nodeTraversal != nullptr) {  // start_as 写在on子句中
          sqlGraphInfo->nodeTraversal->members.push_back(path);
        } else {  // start_as 写在外面
          sqlGraphInfo->varNodes->members.push_back(path);
        }
      }

      break;
    }
    case 1: {
      _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                      "edge cannot be as a startNode");
      return false;
    }
    case 2: {
      // 换方向
      int direction = sqlGraphInfo->directionNode->members[0]->getIntValue();
      int newDirection = 0;
      if (direction == 1) {
        newDirection = 2;
      } else if (direction == 2) {
        newDirection = 1;
      }
      sqlGraphInfo->directionNode->members[0] =
          _ast.createNodeValueInt(newDirection);

      // 生成变量
      // point
      AstNode* point = _ast.createNodeVariable(
          sqlGraphInfo->varNames[0]->getStringView(), true);
      *(sqlGraphInfo->varNodes->members[0]) = *point;
      // edge
      if (sqlGraphInfo->varNames[1]->type != NODE_TYPE_NOP) {
        AstNode* edge = _ast.createNodeVariable(
            sqlGraphInfo->varNames[1]->getStringView(), true);
        *(sqlGraphInfo->varNodes->members[1]) = *edge;
      } else {
        if (sqlGraphInfo->nodeTraversal != nullptr) {
          sqlGraphInfo->nodeTraversal->members.pop_back();
        }
        sqlGraphInfo->varNodes->members.pop_back();
      }
      // path
      if (sqlGraphInfo->varNames[3]->type != NODE_TYPE_NOP) {
        AstNode* path = _ast.createNodeVariable(
            sqlGraphInfo->varNames[3]->getStringView(), true);
        if (sqlGraphInfo->nodeTraversal != nullptr) {  // start_as 写在on子句中
          sqlGraphInfo->nodeTraversal->members.push_back(path);
        } else {  // start_as 写在外面
          sqlGraphInfo->varNodes->members.push_back(path);
        }
      }
      break;
    }
    case 3: {
      _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                      "path cannot be as a startNode");
      return false;
    }
    default: {
      TRI_ASSERT(false);
    }
  }

  *(sqlGraphInfo->startNode) = *expression;
  sqlGraphInfo->startNodeUpdated = true;
  return true;
}
bool arangodb::aql::Parser::updateStartEndNode(AstNode* startExpression,
                                               std::string_view startNodeName,
                                               AstNode* endExpression,
                                               std::string_view endNodeName) {
  if (sqlGraphInfo == nullptr) {
    return false;
  }

  if (sqlGraphInfo->startNodeUpdated) {
    return false;
  }

  if (startNodeName == endNodeName) {
    _query.warnings().registerError(TRI_ERROR_QUERY_PARSE,
                                    "start point cannot be end point");
    return false;
  }

  TRI_ASSERT(sqlGraphInfo->varNames.size() == 2);

  bool asc = sqlGraphInfo->varNames[0]->getStringView() == startNodeName &&
             sqlGraphInfo->varNames[1]->getStringView() == endNodeName;
  bool desc = sqlGraphInfo->varNames[0]->getStringView() == endNodeName &&
              sqlGraphInfo->varNames[1]->getStringView() == startNodeName;

  if (!asc && !desc) {
    _query.warnings().registerError(
        TRI_ERROR_QUERY_PARSE,
        "start point and end point not match declaration");
    return false;
  }

  if (desc) {
    // 换方向
    int direction = sqlGraphInfo->directionNode->members[0]->getIntValue();
    int newDirection = 0;
    if (direction == 1) {
      newDirection = 2;
    } else if (direction == 2) {
      newDirection = 1;
    }
    sqlGraphInfo->directionNode->members[0] =
        _ast.createNodeValueInt(newDirection);
  }

  *(sqlGraphInfo->startNode) = *startExpression;
  *(sqlGraphInfo->endNode) = *endExpression;
  sqlGraphInfo->startNodeUpdated = true;
  return true;
};