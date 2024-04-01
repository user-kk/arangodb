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
  while (!_selectPendingQueue.empty()) {
    pendingNode = _selectPendingQueue.front().first;

    std::string_view variableName = _selectPendingQueue.front().second;
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (_ast.scopes()->canUseCurrentVariable() &&
          (variableName == Variable::NAME_CURRENT ||
           variableName == Variable::NAME_CURRENT.substr(1))) {
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
                                       false);
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
    _selectPendingQueue.pop_front();
  }
  clearSelectMap();
}

void Parser::updateWillReturnNode(AstNode* assignNode) {
  TRI_ASSERT(assignNode->type == NODE_TYPE_ASSIGN &&
             assignNode->members.size() == 2);
  AstNode* member = assignNode->members[1];

  // 检查聚集时使用的是变量还是表达式
  if (member->type == NODE_TYPE_REFERENCE) {
    Variable* v = static_cast<Variable*>(member->getData());
    TRI_ASSERT(v != nullptr);
    if (_selectMap.contains(v->id)) {
      // 使用id在express.memebers[0]中寻找到对应的节点并替换
      TRI_ASSERT(_selectMap[v->id] < _willReturnNode->members.size());
      TRI_ASSERT(!_willReturnNode->members[_selectMap[v->id]]->members.empty());
      Variable* collectVar =
          static_cast<Variable*>(assignNode->members[0]->getData());
      AstNode* refNode = _ast.createNodeReference(collectVar);
      _willReturnNode->members[_selectMap[v->id]]->members[0] = refNode;
    } else {
      // TODO:报错
    }
  } else {
    bool found = false;

    for (auto& objectElementNode : _willReturnNode->members) {
      if (AstNode::equal(objectElementNode->members[0], member)) {
        found = true;
        TRI_ASSERT(assignNode->members[0]->type == NODE_TYPE_VARIABLE);
        Variable* collectVar =
            static_cast<Variable*>(assignNode->members[0]->getData());
        AstNode* refNode = _ast.createNodeReference(collectVar);
        objectElementNode->members[0] = refNode;
      }
    }

    if (!found) {
      // TODO:报错
    }
  }
}

void arangodb::aql::Parser::produceAlias() {
  for (size_t i = 0; i < _selectAliasQueue.size(); ++i) {
    auto [exprNode, name] = _selectAliasQueue[i];

    if (exprNode->type == NODE_TYPE_FCALL) {
      continue;
    }

    size_t vId = 0;
    auto node = _ast.createNodeLet(name, exprNode, true, vId);
    addSelectMap(vId, i);
    _ast.addOperation(node);
  }

  _selectAliasQueue.clear();
};
void arangodb::aql::Parser::executeSelectPendWithoutPop() {
  for (auto& [pendingNode, variableName] : _selectPendingQueue) {
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (_ast.scopes()->canUseCurrentVariable() &&
          (variableName == Variable::NAME_CURRENT ||
           variableName == Variable::NAME_CURRENT.substr(1))) {
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
                                       false);
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
  }
};

arangodb::aql::AstNode* arangodb::aql::Parser::produceAggregate() {
  // 聚集函数的节点和它的别名
  std::unordered_map<AstNode*, std::string_view> map;

  // 遍历找到fcall的节点,检查是否是聚集函数
  for (auto& objectElementNode : _willReturnNode->members) {
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

    // 将是聚集函数的节点和它的别名保存起来
    map.insert({fNode, objectElementNode->getStringView()});
  }

  // 要返回的结果
  AstNode* aggArrayNode = _ast.createNodeArray();

  std::vector<AstNode*> fAggNodes =
      _willReturnNode->find([](AstNode* p) -> bool {
        // 找到fcall的节点,检查是否是聚集函数
        if (p->type != NODE_TYPE_FCALL) {
          return false;
        }
        auto f = static_cast<arangodb::aql::Function*>(p->getData());
        if (!Aggregator::isValid(f->name)) {
          return false;
        }

        return true;
      });
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
      _aggAliasToVar.insert({map[fNode], var});
    }
    AstNode* refNode = _ast.createNodeReference(var);
    *fNode = *refNode;
  }

  if (_havingExprssionNode == nullptr) {
    return aggArrayNode;
  }

  // 现在having中一定有值,修改having中的聚集表达式,使之成为聚集变量
  std::vector<AstNode*> fAggNodesInHaving =
      _havingExprssionNode->find([](AstNode* p) -> bool {
        // 找到fcall的节点,检查是否是聚集函数
        if (p->type != NODE_TYPE_FCALL) {
          return false;
        }
        auto f = static_cast<arangodb::aql::Function*>(p->getData());
        if (!Aggregator::isValid(f->name)) {
          return false;
        }

        return true;
      });
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
    *fNode = *refNode;
  }

  return aggArrayNode;
};
void arangodb::aql::Parser::executeHavingPend() {
  AstNode* pendingNode = nullptr;
  while (!_havingPendingQueue.empty()) {
    pendingNode = _havingPendingQueue.front().first;

    std::string_view variableName = _havingPendingQueue.front().second;
    auto variable = _ast.scopes()->getVariable(variableName, true);

    if (variable == nullptr) {
      // variable does not exist
      // now try special variables
      if (_ast.scopes()->canUseCurrentVariable() &&
          (variableName == Variable::NAME_CURRENT ||
           variableName == Variable::NAME_CURRENT.substr(1))) {
        variable = _ast.scopes()->getCurrentVariable();
      }
    }
    AstNode* node = nullptr;
    if (variable != nullptr) {
      // variable alias exists, now use it
      node = _ast.createNodeReference(variable);
    }
    // 为collection时
    if (node == nullptr) {
      if (_aggAliasToVar.contains(variableName)) {  // 检测是否是聚集变量
        auto v = _aggAliasToVar[variableName];
        node = _ast.createNodeReference(v);
      } else {
        // variable not found. so it must have been a collection or view
        auto const& resolver = this->query().resolver();
        node = _ast.createNodeDataSource(resolver, variableName,
                                         arangodb::AccessMode::Type::READ, true,
                                         false);
      }
    }

    TRI_ASSERT(node != nullptr);

    *pendingNode = *node;
    _havingPendingQueue.pop_front();
  }
};