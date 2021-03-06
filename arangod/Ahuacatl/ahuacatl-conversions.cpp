////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, conversions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
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
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Ahuacatl/ahuacatl-conversions.h"

#include "BasicsC/conversions.h"
#include "BasicsC/json.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/tri-strings.h"

#include "Ahuacatl/ahuacatl-context.h"
#include "Ahuacatl/ahuacatl-functions.h"
#include "Ahuacatl/ahuacatl-node.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief typedef for value list iteration callback function
////////////////////////////////////////////////////////////////////////////////

typedef bool (*convert_f) (TRI_string_buffer_t* const, const TRI_aql_node_t* const);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

static const char* GetStringOperator (const TRI_aql_node_type_e type) {
  switch (type) {
    case TRI_AQL_NODE_OPERATOR_UNARY_PLUS:
      return " + ";
    case TRI_AQL_NODE_OPERATOR_UNARY_MINUS:
      return " - ";
    case TRI_AQL_NODE_OPERATOR_UNARY_NOT:
      return " ! ";
    case TRI_AQL_NODE_OPERATOR_BINARY_AND:
      return " && ";
    case TRI_AQL_NODE_OPERATOR_BINARY_OR:
      return " || ";
    case TRI_AQL_NODE_OPERATOR_BINARY_PLUS:
      return " + ";
    case TRI_AQL_NODE_OPERATOR_BINARY_MINUS:
      return " - ";
    case TRI_AQL_NODE_OPERATOR_BINARY_TIMES:
      return " * ";
    case TRI_AQL_NODE_OPERATOR_BINARY_DIV:
      return " / ";
    case TRI_AQL_NODE_OPERATOR_BINARY_MOD:
      return " % ";
    case TRI_AQL_NODE_OPERATOR_BINARY_EQ:
      return " == ";
    case TRI_AQL_NODE_OPERATOR_BINARY_NE:
      return " != ";
    case TRI_AQL_NODE_OPERATOR_BINARY_LT:
      return " < ";
    case TRI_AQL_NODE_OPERATOR_BINARY_LE:
      return " <= ";
    case TRI_AQL_NODE_OPERATOR_BINARY_GT:
      return " > ";
    case TRI_AQL_NODE_OPERATOR_BINARY_GE:
      return " >= ";
    case TRI_AQL_NODE_OPERATOR_BINARY_IN:
      return " in ";
    default:
      TRI_ASSERT(false);
      return NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief append list values to a string buffer
////////////////////////////////////////////////////////////////////////////////

static bool AppendListValues (TRI_string_buffer_t* const buffer,
                              const TRI_aql_node_t* const node,
                              convert_f func) {
  size_t i, n;

  n = node->_members._length;
  for (i = 0; i < n; ++i) {
    if (i > 0) {
      if (TRI_AppendString2StringBuffer(buffer, ", ", 2) != TRI_ERROR_NO_ERROR) {
        return false;
      }
    }

    if (! func(buffer, TRI_AQL_NODE_MEMBER(node, i))) {
      return false;
    }
  }

  return true;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create a json struct from a value node
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_NodeJsonAql (TRI_aql_context_t* const context,
                             const TRI_aql_node_t* const node) {
  switch (node->_type) {
    case TRI_AQL_NODE_VALUE: {
      switch (node->_value._type) {
        case TRI_AQL_TYPE_FAIL:
        case TRI_AQL_TYPE_NULL:
          return TRI_CreateNullJson(TRI_UNKNOWN_MEM_ZONE);
        case TRI_AQL_TYPE_BOOL:
          return TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, node->_value._value._bool);
        case TRI_AQL_TYPE_INT:
          return TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (double) node->_value._value._int);
        case TRI_AQL_TYPE_DOUBLE:
          return TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, node->_value._value._double);
        case TRI_AQL_TYPE_STRING:
          return TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, node->_value._value._string);
      }
      TRI_ASSERT(false);
      return NULL;
    }
    case TRI_AQL_NODE_LIST: {
      const size_t n = node->_members._length;
      TRI_json_t* result = TRI_CreateList2Json(TRI_UNKNOWN_MEM_ZONE, n);

      if (result != NULL) {
        size_t i;

        for (i = 0; i < n; ++i) {
          TRI_json_t* subValue = TRI_NodeJsonAql(context, TRI_AQL_NODE_MEMBER(node, i));

          if (subValue != NULL) {
            TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, result, subValue);
          }
        }
      }
      return result;
    }
    case TRI_AQL_NODE_ARRAY: {
      const size_t n = node->_members._length;
      TRI_json_t* result = TRI_CreateArray2Json(TRI_UNKNOWN_MEM_ZONE, n);

      if (result != NULL) {
        size_t i;

        for (i = 0; i < n; ++i) {
          TRI_aql_node_t* element = TRI_AQL_NODE_MEMBER(node, i);
          TRI_json_t* subValue = TRI_NodeJsonAql(context, TRI_AQL_NODE_MEMBER(element, 0));

          if (subValue) {
            TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE,
                                 result,
                                 TRI_AQL_NODE_STRING(element),
                                 subValue);
          }
        }
      }
      return result;
    }

    default: {
      return NULL;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a value node from a json struct
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_JsonNodeAql (TRI_aql_context_t* const context,
                                 const TRI_json_t* const json) {
  TRI_aql_node_t* node = NULL;
  char* value;

  switch (json->_type) {
    case TRI_JSON_UNUSED:
      break;

    case TRI_JSON_NULL:
      node = TRI_CreateNodeValueNullAql(context);
      break;

    case TRI_JSON_BOOLEAN:
      node = TRI_CreateNodeValueBoolAql(context, json->_value._boolean);
      break;

    case TRI_JSON_NUMBER:
      node = TRI_CreateNodeValueDoubleAql(context, json->_value._number);
      break;

    case TRI_JSON_STRING:
    case TRI_JSON_STRING_REFERENCE:
      // the conversion is the same for both...
      value = TRI_RegisterStringAql(context,
                                    json->_value._string.data,
                                    json->_value._string.length - 1,
                                    false);
      node = TRI_CreateNodeValueStringAql(context, value);
      break;

    case TRI_JSON_LIST: {
      node = TRI_CreateNodeListAql(context);

      if (node != NULL) {
        size_t i, n;

        n = json->_value._objects._length;

        for (i = 0; i < n; ++i) {
          TRI_json_t* subJson;
          TRI_aql_node_t* member;

          subJson = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);
          member = TRI_JsonNodeAql(context, subJson);

          if (member) {
            TRI_PushBackVectorPointer(&node->_members, (void*) member);
          }
          else {
            TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL);
            return NULL;
          }
        }
      }
      break;
    }
    case TRI_JSON_ARRAY: {
      node = TRI_CreateNodeArrayAql(context);

      if (node != NULL) {
        size_t i, n;

        n = json->_value._objects._length;

        for (i = 0; i < n; i += 2) {
          TRI_json_t* nameJson;
          TRI_json_t* valueJson;
          TRI_aql_node_t* member;
          TRI_aql_node_t* valueNode;
          char* name;

          // json_t containing the array element name
          nameJson = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);

          TRI_ASSERT(TRI_IsStringJson(nameJson));
          name = TRI_RegisterStringAql(context,
                                       nameJson->_value._string.data,
                                       nameJson->_value._string.length - 1,
                                       false);
          if (name == NULL) {
            TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL);
            return NULL;
          }

          // json_t containing the array element value
          valueJson = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i + 1);
          TRI_ASSERT(valueJson);

          valueNode = TRI_JsonNodeAql(context, valueJson);
          if (! valueNode) {
            TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL);
            return NULL;
          }

          member = TRI_CreateNodeArrayElementAql(context, name, valueNode);
          if (member) {
            TRI_PushBackVectorPointer(&node->_members, (void*) member);
          }
          else {
            TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL);
            return NULL;
          }
        }
      }
      break;
    }
  }

  if (node == NULL) {
    TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL);
  }

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a value node to its Javascript representation
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValueJavascriptAql (TRI_string_buffer_t* const buffer,
                             const TRI_aql_value_t* const value,
                             const TRI_aql_value_type_e type) {
  switch (type) {
    case TRI_AQL_TYPE_FAIL: {
      return (TRI_AppendString2StringBuffer(buffer, "fail", 4) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_NULL: {
      return (TRI_AppendString2StringBuffer(buffer, "null", 4) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_BOOL: {
      if (value->_value._bool) {
        return (TRI_AppendString2StringBuffer(buffer, "true", 4) == TRI_ERROR_NO_ERROR);
      }
      else {
        return (TRI_AppendString2StringBuffer(buffer, "false", 5) == TRI_ERROR_NO_ERROR);
      }
    }

    case TRI_AQL_TYPE_INT: {
      return (TRI_AppendInt64StringBuffer(buffer, value->_value._int) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_DOUBLE: {
      return (TRI_AppendDoubleStringBuffer(buffer, value->_value._double) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_STRING: {
      if (TRI_AppendCharStringBuffer(buffer, '"') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (TRI_AppendJsonEncodedStringStringBuffer(buffer, value->_value._string, false) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return (TRI_AppendCharStringBuffer(buffer, '"') == TRI_ERROR_NO_ERROR);
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a node to its Javascript representation
////////////////////////////////////////////////////////////////////////////////

bool TRI_NodeJavascriptAql (TRI_string_buffer_t* const buffer,
                            const TRI_aql_node_t* const node) {
  switch (node->_type) {
    case TRI_AQL_NODE_VALUE:
      return TRI_ValueJavascriptAql(buffer, &node->_value, node->_value._type);
    case TRI_AQL_NODE_ARRAY_ELEMENT:
      if (! TRI_ValueJavascriptAql(buffer, &node->_value, TRI_AQL_TYPE_STRING)) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, ':') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_NodeJavascriptAql(buffer, TRI_AQL_NODE_MEMBER(node, 0));
    case TRI_AQL_NODE_LIST:
      if (TRI_AppendCharStringBuffer(buffer, '[') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! AppendListValues(buffer, node, &TRI_NodeJavascriptAql)) {
        return false;
      }

      return (TRI_AppendCharStringBuffer(buffer, ']') == TRI_ERROR_NO_ERROR);
    case TRI_AQL_NODE_ARRAY:
      if (TRI_AppendCharStringBuffer(buffer, '{') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! AppendListValues(buffer, node, &TRI_NodeJavascriptAql)) {
        return false;
      }

      return (TRI_AppendCharStringBuffer(buffer, '}') == TRI_ERROR_NO_ERROR);
    default:
      return false;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a value node to a string representation, used for printing it
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValueStringAql (TRI_string_buffer_t* const buffer,
                         const TRI_aql_value_t* const value,
                         const TRI_aql_value_type_e type) {
  switch (type) {
    case TRI_AQL_TYPE_FAIL: {
      return (TRI_AppendString2StringBuffer(buffer, "fail", 4) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_NULL: {
      return (TRI_AppendString2StringBuffer(buffer, "null", 4) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_BOOL: {
      if (value->_value._bool) {
        return (TRI_AppendString2StringBuffer(buffer, "true", 4) == TRI_ERROR_NO_ERROR);
      }
      else {
        return (TRI_AppendString2StringBuffer(buffer, "false", 5) == TRI_ERROR_NO_ERROR);
      }
    }

    case TRI_AQL_TYPE_INT: {
      return (TRI_AppendInt64StringBuffer(buffer, value->_value._int) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_DOUBLE: {
      return (TRI_AppendDoubleStringBuffer(buffer, value->_value._double) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_TYPE_STRING: {
      if (TRI_AppendCharStringBuffer(buffer, '"') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (TRI_AppendStringStringBuffer(buffer, value->_value._string) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return (TRI_AppendCharStringBuffer(buffer, '"') == TRI_ERROR_NO_ERROR);
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a node to its string representation, used for printing it
////////////////////////////////////////////////////////////////////////////////

bool TRI_NodeStringAql (TRI_string_buffer_t* const buffer,
                        const TRI_aql_node_t* const node) {
  switch (node->_type) {
    case TRI_AQL_NODE_VALUE: {
      return TRI_ValueStringAql(buffer, &node->_value, node->_value._type);
    }

    case TRI_AQL_NODE_PARAMETER: {
      return TRI_AppendStringStringBuffer(buffer, TRI_AQL_NODE_STRING(node)) == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_ARRAY_ELEMENT: {
      if (! TRI_ValueStringAql(buffer, &node->_value, TRI_AQL_TYPE_STRING)) {
        return false;
      }

      if (TRI_AppendString2StringBuffer(buffer, " : ", 3) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0));
    }

    case TRI_AQL_NODE_LIST: {
      if (TRI_AppendString2StringBuffer(buffer, "[ ", 2) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! AppendListValues(buffer, node, &TRI_NodeStringAql)) {
        return false;
      }

      return (TRI_AppendString2StringBuffer(buffer, " ]", 2) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_NODE_ARRAY: {
      if (TRI_AppendString2StringBuffer(buffer, "{ ", 2) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! AppendListValues(buffer, node, &TRI_NodeStringAql)) {
        return false;
      }

      return (TRI_AppendString2StringBuffer(buffer, " }", 2) == TRI_ERROR_NO_ERROR);
    }

    case TRI_AQL_NODE_OPERATOR_UNARY_PLUS:
    case TRI_AQL_NODE_OPERATOR_UNARY_MINUS:
    case TRI_AQL_NODE_OPERATOR_UNARY_NOT: {
      if (TRI_AppendStringStringBuffer(buffer, GetStringOperator(node->_type)) != TRI_ERROR_NO_ERROR) {
        return false;
      }
      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0));
    }

    case TRI_AQL_NODE_OPERATOR_BINARY_AND:
    case TRI_AQL_NODE_OPERATOR_BINARY_OR:
    case TRI_AQL_NODE_OPERATOR_BINARY_PLUS:
    case TRI_AQL_NODE_OPERATOR_BINARY_MINUS:
    case TRI_AQL_NODE_OPERATOR_BINARY_TIMES:
    case TRI_AQL_NODE_OPERATOR_BINARY_DIV:
    case TRI_AQL_NODE_OPERATOR_BINARY_MOD:
    case TRI_AQL_NODE_OPERATOR_BINARY_EQ:
    case TRI_AQL_NODE_OPERATOR_BINARY_NE:
    case TRI_AQL_NODE_OPERATOR_BINARY_LT:
    case TRI_AQL_NODE_OPERATOR_BINARY_LE:
    case TRI_AQL_NODE_OPERATOR_BINARY_GT:
    case TRI_AQL_NODE_OPERATOR_BINARY_GE:
    case TRI_AQL_NODE_OPERATOR_BINARY_IN: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      if (TRI_AppendStringStringBuffer(buffer, GetStringOperator(node->_type)) != TRI_ERROR_NO_ERROR) {
        return false;
      }
      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 1));
    }

    case TRI_AQL_NODE_OPERATOR_TERNARY: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      if (TRI_AppendString2StringBuffer(buffer, " ? ", 3) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 1))) {
        return false;
      }

      if (TRI_AppendString2StringBuffer(buffer, " : ", 3) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 2));
    }

    case TRI_AQL_NODE_ATTRIBUTE_ACCESS: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, '.') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_AppendStringStringBuffer(buffer, TRI_AQL_NODE_STRING(node)) == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_BOUND_ATTRIBUTE_ACCESS: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, '.') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 1));
    }

    case TRI_AQL_NODE_INDEXED: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, '[') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 1))) {
        return false;
      }

      return TRI_AppendCharStringBuffer(buffer, ']') == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_FCALL: {
      TRI_aql_function_t* function = (TRI_aql_function_t*) TRI_AQL_NODE_DATA(node);

      if (TRI_AppendStringStringBuffer(buffer, function->_externalName) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, '(') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      return TRI_AppendCharStringBuffer(buffer, ')') == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_FCALL_USER: {
      if (TRI_AppendStringStringBuffer(buffer, TRI_AQL_NODE_STRING(node)) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (TRI_AppendCharStringBuffer(buffer, '(') != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      return TRI_AppendCharStringBuffer(buffer, ')') == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_EXPAND: {
      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 3));
    }

    case TRI_AQL_NODE_REFERENCE: {
      return TRI_AppendStringStringBuffer(buffer, TRI_AQL_NODE_STRING(node)) == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_COLLECTION: {
      TRI_aql_node_t* nameNode = TRI_AQL_NODE_MEMBER(node, 0);
      char* name = TRI_AQL_NODE_STRING(nameNode);

      return TRI_AppendStringStringBuffer(buffer, name) == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_SORT: {
      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0));
    }

    case TRI_AQL_NODE_SORT_ELEMENT: {
      if (! TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 0))) {
        return false;
      }

      return TRI_AppendStringStringBuffer(buffer, (TRI_AQL_NODE_BOOL(node) ? " ASC" : " DESC")) == TRI_ERROR_NO_ERROR;
    }

    case TRI_AQL_NODE_ASSIGN: {
      TRI_aql_node_t* nameNode = TRI_AQL_NODE_MEMBER(node, 0);
      char* name = TRI_AQL_NODE_STRING(nameNode);

      if (TRI_AppendStringStringBuffer(buffer, name) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      if (TRI_AppendString2StringBuffer(buffer, " = ", 3) != TRI_ERROR_NO_ERROR) {
        return false;
      }

      return TRI_NodeStringAql(buffer, TRI_AQL_NODE_MEMBER(node, 1));
    }

    default: {
      // nada
    }
  }

  return true;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
