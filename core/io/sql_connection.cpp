/**************************************************************************/
/*  sql_connection.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

/**************************************************************************/
/*  Portions of this file are derived from PDO (PHP Data Objects)         */
/*  https://www.php.net/manual/en/book.pdo.php                            */
/*                                                                        */
/*  The following code is copyright (c) The PHP Group                     */
/*  and is licensed under the PHP License, version 3.01:                  */
/*  https://www.php.net/license/3_01.txt                                  */
/**************************************************************************/

#include "sql_connection.h"

const char *const SQL_CODE_UNSUPPORTED = "IM001";

//
// SQLError implementation
//

SQLError::SQLError() {
}

SQLError::SQLError(const char *p_function, const char *p_file, int p_line, const String &p_standard_code, const String &p_driver_code, const String &p_message) {
	function = p_function;
	file = p_file;
	line = p_line;
	standard_code = p_standard_code;
	driver_code = p_driver_code;
	message = p_message;
}

void SQLError::clear() {
	function = nullptr;
	file = nullptr;
	line = 0;
	standard_code.clear();
	driver_code.clear();
	message.clear();
}

Array SQLError::to_array() const {
	Array error_info;
	error_info.push_back(get_error_code());
	error_info.push_back(driver_code);
	error_info.push_back(message);
	if (function && file) {
		error_info.push_back(vformat("In function %s at %s:%d", function, file, line));
	} else {
		error_info.push_back("Unknown function");
	}
	return error_info;
}

String SQLError::get_error_code() const {
	if (standard_code.is_empty() && !message.is_empty()) {
		return "HY000";
	} else {
		return standard_code;
	}
}

//
// SQLDriver implementation
//

Vector<Ref<SQLDriver>> SQLDriver::drivers;

SQLDriver::SQLDriver(int p_features) :
		features(p_features) {
}

void SQLDriver::add_driver(Ref<SQLDriver> p_driver) {
	ERR_FAIL_COND(p_driver.is_null());
	ERR_FAIL_COND(get_driver_by_name(p_driver->get_name()).is_valid());
	drivers.push_back(p_driver);
}

void SQLDriver::remove_driver(Ref<SQLDriver> p_driver) {
	ERR_FAIL_COND(!drivers.has(p_driver));
	drivers.erase(p_driver);
}

Ref<SQLDriver> SQLDriver::get_driver_by_name(const String &p_name) {
	for (int64_t i = 0; i < drivers.size(); ++i) {
		if (drivers[i]->get_name() == p_name) {
			return drivers[i];
		}
	}
	return Ref<SQLDriver>();
}

PackedStringArray SQLDriver::get_driver_names() {
	PackedStringArray driver_names;
	for (int64_t i = 0; i < drivers.size(); ++i) {
		driver_names.push_back(drivers[i]->get_name());
	}
	return driver_names;
}

//
// SQLStatement implementation
//

SQLStatement::SQLStatement() {
}

SQLStatement::~SQLStatement() {
	if (impl) {
		memdelete(impl);
	}

	if (connection.is_valid()) {
		--connection->statement_count;
		connection.unref();
	}
}

String SQLStatement::get_query_string() const {
	return query_string;
}

bool SQLStatement::set_attribute(Attribute p_attribute, const Variant &p_value) {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();
	return impl->set_attribute(p_attribute, p_value);
}

Variant SQLStatement::get_attribute(Attribute p_attribute) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();
	return impl->get_attribute(p_attribute);
}

int SQLStatement::get_fetch_mode() const {
	return fetch_mode;
}

Variant SQLStatement::get_fetch_param() const {
	return fetch_param;
}

static bool _validate_fetch_mode(int p_fetch_mode, const Variant &p_param, bool p_allow_default) {
	const int mode = p_fetch_mode & ~SQLStatement::FETCH_FLAGS;
	int flags = p_fetch_mode & SQLStatement::FETCH_FLAGS;

	switch (mode) {
		case SQLStatement::FETCH_DEFAULT:
			if ((flags & (SQLStatement::FETCH_GROUP | SQLStatement::FETCH_UNIQUE)) == 0) {
				ERR_FAIL_COND_V(!p_allow_default, false);
			}
			ERR_FAIL_COND_V(p_param.get_type() != Variant::NIL, false);
			break;
		case SQLStatement::FETCH_BOTH:
		case SQLStatement::FETCH_NAMED:
		case SQLStatement::FETCH_ASSOC:
		case SQLStatement::FETCH_ARRAY:
		case SQLStatement::FETCH_KEY_PAIR:
			ERR_FAIL_COND_V(p_param.get_type() != Variant::NIL, false);
			break;
		case SQLStatement::FETCH_CLASS:
			if ((flags & SQLStatement::FETCH_CLASSNAME) == 0) {
				ERR_FAIL_COND_V(!p_param.is_string(), false);
				ERR_FAIL_COND_V(!ClassDB::can_instantiate(p_param), false);
			}
			break;
		case SQLStatement::FETCH_INTO:
			ERR_FAIL_COND_V(p_param.is_null(), false);
			break;
		case SQLStatement::FETCH_COLUMN:
			if (p_param.get_type() != Variant::NIL) {
				ERR_FAIL_COND_V(!Variant::can_convert(p_param.get_type(), Variant::INT), false);
			}
			break;
		case SQLStatement::FETCH_CALL:
			ERR_FAIL_COND_V(p_param.get_type() != Variant::CALLABLE, false);
			break;
		default:
			ERR_FAIL_V(false);
	}

	if ((flags & SQLStatement::FETCH_CLASSNAME) != 0) {
		ERR_FAIL_COND_V(mode != SQLStatement::FETCH_CLASS, false);
		flags &= ~SQLStatement::FETCH_CLASSNAME;
	}
	ERR_FAIL_COND_V(flags != 0 && flags != SQLStatement::FETCH_GROUP && flags != SQLStatement::FETCH_UNIQUE, false);
	return true;
}

bool SQLStatement::_get_fetch_mode(int &r_mode, Variant &r_param, bool p_fetch_all) {
	if (r_mode == FETCH_DEFAULT) {
		// These are already validated in _validate_fetch_mode
		if (fetch_mode == FETCH_DEFAULT) {
			r_mode = connection->default_fetch_mode;
			r_param = Variant();
		} else {
			r_mode = fetch_mode;
			r_param = fetch_param;
		}
	} else if (!_validate_fetch_mode(r_mode, r_param, false)) {
		return false;
	}

	// Do checks that we could not do in _validate_fetch_mode
	if ((r_mode & (FETCH_GROUP | FETCH_UNIQUE)) != 0) {
		ERR_FAIL_COND_V(!p_fetch_all, false);
		ERR_FAIL_COND_V(columns.size() < 2, false);
		if ((r_mode & ~FETCH_FLAGS) == FETCH_DEFAULT) {
			r_mode |= FETCH_BOTH;
		}
	}

	const int mode = r_mode & ~FETCH_FLAGS;
	switch (mode) {
		case FETCH_INTO:
			ERR_FAIL_COND_V(p_fetch_all, false);
			break;
		case FETCH_KEY_PAIR:
			ERR_FAIL_COND_V(!p_fetch_all, false);
			ERR_FAIL_COND_V(columns.size() != 2, false);
			break;
		case FETCH_COLUMN: {
			const int index = r_param;
			ERR_FAIL_INDEX_V(index, (int)columns.size(), false);
			break;
		}
		default:
			break;
	}
	return true;
}

Variant SQLStatement::_fetch_row(int p_mode, const Variant &p_param, int p_start_column, int p_end_column) {
	const int mode = p_mode & ~FETCH_FLAGS;
	switch (mode) {
		case FETCH_BOTH: {
			Dictionary dictionary;
			dictionary.reserve((p_end_column - p_start_column) * 2);
			for (int i = p_start_column; i < p_end_column; ++i) {
				const Variant value = _fetch_value(i);
				dictionary[i - p_start_column] = value;
				dictionary[columns[i].name] = value;
			}
			return dictionary;
		}
		case FETCH_ASSOC: {
			Dictionary dictionary;
			dictionary.reserve(p_end_column - p_start_column);
			for (int i = p_start_column; i < p_end_column; ++i) {
				dictionary[columns[i].name] = _fetch_value(i);
			}
			return dictionary;
		}
		case FETCH_NAMED: {
			Dictionary dictionary;
			dictionary.reserve(p_end_column - p_start_column);
			for (int i = p_start_column; i < p_end_column; ++i) {
				const Variant value = _fetch_value(i);

				Variant *const entry = dictionary.getptr(columns[i].name);
				if (entry) {
					if (entry->get_type() == Variant::ARRAY) {
						VariantInternal::get_array(entry)->push_back(value);
					} else {
						const Array array = { *entry, value };
						*entry = array;
					}
				} else {
					dictionary[columns[i].name] = value;
				}
			}
			return dictionary;
		}
		case FETCH_ARRAY: {
			Array array;
			array.resize(p_end_column - p_start_column);
			for (int i = p_start_column; i < p_end_column; ++i) {
				array.set(i - p_start_column, _fetch_value(i));
			}
			return array;
		}
		case FETCH_CLASS:
			if ((p_mode & FETCH_CLASSNAME) != 0) {
				const Variant class_name = _fetch_value(p_start_column);
				if (!Variant::can_convert(class_name.get_type(), Variant::STRING_NAME)) {
					_push_error(SQL_ERROR(String(), String(), "First column must be convertible to StringName."));
					return Variant();
				}

				return _fetch_object(class_name, p_start_column + 1, p_end_column);
			}
			return _fetch_object(p_param, p_start_column, p_end_column);
		case FETCH_INTO: {
			Object *const object = p_param;
			for (int i = p_start_column; i < p_end_column; ++i) {
				Column &column = columns[i];
				if (column.string_name == StringName() && !column.name.is_empty()) {
					column.string_name = column.name;
				}

				bool valid;
				object->set(column.string_name, _fetch_value(i), &valid);
				if (!valid) {
					_push_error(SQL_ERROR(String(), String(), vformat("Cannot set property '%s' on object.", column.name)));
					return Variant();
				}
			}
			return object;
		}
		case FETCH_COLUMN:
			return _fetch_value(p_param);
		case FETCH_CALL: {
			Array arguments;
			arguments.resize(p_end_column - p_start_column);
			for (int i = p_start_column; i < p_end_column; ++i) {
				arguments.set(i - p_start_column, _fetch_value(i));
			}

			const Callable callable = p_param;
			return callable.callv(arguments);
		}
		default:
			return Variant();
	}
}

Object *SQLStatement::_fetch_object(const StringName &p_class_name, int p_start_column, int p_end_column) {
	Object *const object = ClassDB::instantiate(p_class_name);
	if (!object) {
		_push_error(SQL_ERROR(String(), String(), vformat("Cannot instantiate class '%s'.", p_class_name)));
		return nullptr;
	}

	for (int i = p_start_column; i < p_end_column; ++i) {
		Column &column = columns[i];
		if (column.string_name == StringName() && !column.name.is_empty()) {
			column.string_name = column.name;
		}

		bool valid;
		object->set(column.string_name, _fetch_value(i), &valid);
		if (!valid) {
			memdelete(object);
			_push_error(SQL_ERROR(String(), String(), vformat("Cannot set property '%s' on class instance.", column.name)));
			return nullptr;
		}
	}
	return object;
}

Variant SQLStatement::_fetch_value(int p_column) {
	Variant value;
	if (!impl->get_value(p_column, value)) {
		return Variant();
	}

	if (connection->fetch_nulls_mode == NULL_EMPTY_STRING && value.is_string() && value.is_zero()) {
		value = Variant();
	}
	if (connection->fetch_nulls_mode == NULL_TO_STRING && value.get_type() == Variant::NIL) {
		value = String();
	}
	return value;
}

bool SQLStatement::_fetch_next_row(FetchOrientation p_orientation, int64_t p_offset) {
	if (!executed) {
		return false;
	}

	if (!impl->fetch(p_orientation, p_offset)) {
		return false;
	}

	return true;
}

Variant SQLStatement::fetch_all(int p_mode, const Variant &p_param) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();

	int mode(p_mode);
	Variant param(p_param);
	if (!_get_fetch_mode(mode, param, true)) {
		return Variant();
	}

	if (mode == FETCH_KEY_PAIR) {
		Dictionary dictionary;
		while (true) {
			if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
				break;
			}

			dictionary[_fetch_value(0)] = _fetch_value(1);
		}
		return dictionary;
	} else if ((mode & FETCH_UNIQUE) != 0) {
		Dictionary dictionary;
		while (true) {
			if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
				break;
			}

			dictionary[_fetch_value(0)] = _fetch_row(mode, param, 1, (int)columns.size());
		}
		return dictionary;
	} else if ((mode & FETCH_GROUP) != 0) {
		Dictionary dictionary;
		while (true) {
			if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
				break;
			}

			const Variant key = _fetch_value(0);
			const Variant row = _fetch_row(mode, param, 1, (int)columns.size());

			Variant *const entry = dictionary.getptr(key);
			if (entry) {
				VariantInternal::get_array(entry)->push_back(row);
			} else {
				dictionary[key] = Array{ row };
			}
		}
		return dictionary;
	} else {
		if (columns.is_empty()) {
			return Variant();
		}

		Array rows;
		while (true) {
			if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
				break;
			}

			rows.push_back(_fetch_row(mode, param, 0, (int)columns.size()));
		}
		return rows;
	}
}

Variant SQLStatement::fetch(int p_mode, FetchOrientation p_orientation, int64_t p_offset) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();

	int mode(p_mode);
	Variant param;
	if (!_get_fetch_mode(mode, param, false)) {
		return Variant();
	}

	if (!_fetch_next_row(p_orientation, p_offset)) {
		return Variant();
	}

	if (columns.is_empty()) {
		return Variant();
	}

	return _fetch_row(mode, param, 0, (int)columns.size());
}

Variant SQLStatement::fetch_column(int p_column) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();

	if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
		return Variant();
	}

	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), Variant());
	return _fetch_value(p_column);
}

Object *SQLStatement::fetch_object(const StringName &p_class_name) {
	ERR_FAIL_NULL_V(impl, nullptr);
	error.clear();

	if (!_fetch_next_row(FETCH_ORI_NEXT, 0)) {
		return nullptr;
	}

	return _fetch_object(p_class_name, 0, (int)columns.size());
}

bool SQLStatement::set_fetch_mode(int p_mode, const Variant &p_param) {
	if (!_validate_fetch_mode(p_mode, p_param, true)) {
		return false;
	}

	fetch_mode = p_mode;
	fetch_param = p_param;
	return true;
}

bool SQLStatement::close_cursor() {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();

	if (connection->driver->has_features(SQLDriver::FEAT_CLOSE_CURSOR)) {
		if (!impl->close_cursor()) {
			return false;
		}
	} else {
		// Emulate it by fetching and discarding rows
		do {
			while (impl->fetch(FETCH_ORI_NEXT, 0)) {
			}
			if (!connection->driver->has_features(SQLDriver::FEAT_NEXT_ROWSET) || !impl->do_next_rowset()) {
				break;
			}
		} while (true);
	}

	executed = false;
	return true;
}

bool SQLStatement::next_rowset() {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();

	if (!connection->driver->has_features(SQLDriver::FEAT_NEXT_ROWSET)) {
		_push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), String("Driver does not support multiple rowsets.")));
		return false;
	}

	return impl->do_next_rowset();
}

bool SQLStatement::_rewrite_name_to_position(Parameter &r_parameter) {
	if (parameters_map.is_empty()) {
		return true;
	}
	if (!impl->get_named_rewrite_template().is_empty()) {
		return true;
	}

	if (r_parameter.name.is_empty()) {
		if (parameters_map.has(r_parameter.index)) {
			r_parameter.name = parameters_map[r_parameter.index];
		}
	} else {
		int position = 0;
		for (const auto &it : parameters_map) {
			if (it.value != r_parameter.name) {
				++position;
				continue;
			}
			if (r_parameter.index >= 0) {
				_push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), "Cannot handle repeating the same :named parameter for multiple positions with this driver, "
																	  "as it might be unsafe to do so. Consider using a separate name for each parameter instead"));
				return false;
			}
			r_parameter.index = position;
			return true;
		}
	}

	_push_error(SQL_ERROR("HY093", String(), "Parameter was not defined."));
	return false;
}

bool SQLStatement::_register_parameter(Parameter &r_parameter) {
	if (!r_parameter.name.is_empty() && r_parameter.name[0] != U':') {
		r_parameter.name = U':' + r_parameter.name; // Ensure it starts with ':'
	}

	if (!_rewrite_name_to_position(r_parameter)) {
		return false;
	}
	if (!impl->handle_parameter_event(SQLDriverStatement::PARAMETER_EVENT_NORMALIZE, parameters_map, r_parameter)) {
		return false;
	}

	if (r_parameter.name.is_empty()) {
		parameters[r_parameter.index] = r_parameter;
	} else {
		if (r_parameter.index >= 0) {
			parameters.erase(r_parameter.index);
		}
		parameters[r_parameter.name] = r_parameter;
	}

	if (!impl->handle_parameter_event(SQLDriverStatement::PARAMETER_EVENT_ADD, parameters_map, r_parameter)) {
		if (r_parameter.name.is_empty()) {
			parameters.erase(r_parameter.index);
		} else {
			parameters.erase(r_parameter.name);
		}
		return false;
	}

	return true;
}

bool SQLStatement::_parse_parameters() {
	active_query_string.clear();

	struct Placeholder {
		Span<char32_t> name;
		String quoted;
		int position = -1;
	};
	LocalVector<Placeholder> placeholders;

	int query_type = SQLDriverStatement::PLACEHOLDER_NONE;
	const char32_t *custom_quote_pos = nullptr;
	size_t custom_quote_len = 0;
	int placeholder_position = 0;
	bool escapes = false;

	const SQLDriverStatement::PlaceholderType placeholder_type(impl->get_placeholder_type());

	const char32_t *c(query_string.ptr());
	while (*c) {
		const char32_t *const token(c);
		const SQLDriverStatement::TokenType token_type(impl->parse_query_token(c));
		const size_t token_len(c - token);

		if (custom_quote_pos) {
			if (token_type == SQLDriverStatement::TOKEN_CUSTOM_QUOTE && custom_quote_len == token_len &&
					std::memcmp(token, custom_quote_pos, custom_quote_len * sizeof(char32_t)) == 0) {
				custom_quote_pos = nullptr;
				custom_quote_len = 0;
				continue;
			} else if (token_type == SQLDriverStatement::TOKEN_ESCAPED_QUESTION) {
				WARN_PRINT("Escaped question marks inside custom quotes are deprecated and will not be supported in future versions.");
			} else {
				continue;
			}
		} else {
			if (token_type == SQLDriverStatement::TOKEN_CUSTOM_QUOTE) {
				// Start of a custom quote, keep a reference to search for the matching closing quote
				custom_quote_pos = token;
				custom_quote_len = token_len;
				continue;
			}
			if (token_type == SQLDriverStatement::TOKEN_TEXT) {
				continue;
			}
			if (token_type == SQLDriverStatement::TOKEN_ESCAPED_QUESTION && placeholder_type == SQLDriverStatement::PLACEHOLDER_POSITIONAL) {
				// Escaped question marks unsupported, treat as text
				continue;
			}
			if (token_type == SQLDriverStatement::TOKEN_NAMED_PARAMETER) {
				if (token > query_string.ptr() && is_ascii_alphanumeric_char(*(token - 1))) {
					continue;
				}
				query_type |= SQLDriverStatement::PLACEHOLDER_NAMED;
			} else if (token_type == SQLDriverStatement::TOKEN_POSITIONAL_PARAMETER) {
				query_type |= SQLDriverStatement::PLACEHOLDER_POSITIONAL;
			}
		}

		Placeholder plc;
		plc.name = Span<char32_t>(token, token_len);
		if (token_type == SQLDriverStatement::TOKEN_ESCAPED_QUESTION) {
			plc.quoted = "?";
			escapes = true;
		} else {
			plc.position = placeholder_position++;
		}
		placeholders.push_back(plc);
	}

	// Did the query make sense to me?
	if (query_type == (SQLDriverStatement::PLACEHOLDER_NAMED | SQLDriverStatement::PLACEHOLDER_POSITIONAL)) {
		_push_error(SQL_ERROR("HY093", String(), "Mixed named and positional parameters."));
		return false;
	}

	bool do_checks = true;
	if (placeholder_type == SQLDriverStatement::PLACEHOLDER_NONE && !parameters.is_empty() && placeholder_position != parameters.size()) {
		// Extra bit of validation for instances when same params are bound more than once
		if (query_type != SQLDriverStatement::PLACEHOLDER_NONE && placeholder_position > (int)parameters.size()) {
			bool ok = true;
			for (const Placeholder &plc : placeholders) {
				if (!parameters.has(String::utf32_unchecked(plc.name))) {
					ok = false;
					break;
				}
			}
			if (ok) {
				do_checks = false;
			}
		}
		if (do_checks) {
			_push_error(SQL_ERROR("HY093", String(), "Number of bound variables does not match number of tokens."));
			return false;
		}
	}

	const String named_rewrite_template(impl->get_named_rewrite_template());

	bool do_mapping = true;
	if (do_checks) {
		// Nothing to do
		if (placeholders.is_empty()) {
			active_query_string = query_string;
			return true;
		}

		if (placeholder_type == query_type && named_rewrite_template.is_empty()) {
			// Query matches native syntax
			if (escapes) {
				do_mapping = false;
			} else {
				active_query_string = query_string;
				return true;
			}
		} else if (query_type == SQLDriverStatement::PLACEHOLDER_NAMED && !named_rewrite_template.is_empty()) {
			// magic/hack: We we pretend that the query was positional even if it was
			// named so that we fall into the named rewrite case below. Not too pretty, but it works.
			query_type = SQLDriverStatement::PLACEHOLDER_POSITIONAL;
		}
	}

	if (do_mapping) {
		if (placeholder_type == SQLDriverStatement::PLACEHOLDER_NONE) {
			if (!parameters.is_empty()) {
				for (Placeholder &plc : placeholders) {
					if (plc.position < 0) {
						continue;
					}
					if (query_type == SQLDriverStatement::PLACEHOLDER_NONE) {
						continue;
					}

					Parameter *param;
					if (query_type == SQLDriverStatement::PLACEHOLDER_POSITIONAL) {
						param = parameters.getptr(plc.position);
					} else {
						param = parameters.getptr(String::utf32_unchecked(plc.name));
					}
					if (!param) {
						_push_error(SQL_ERROR("HY093", String(), "Parameter was not defined."));
						return false;
					}

					switch (param->value.get_type()) {
						case Variant::NIL:
							plc.quoted = "NULL";
							break;
						case Variant::BOOL:
							plc.quoted = param->value.booleanize() ? "1" : "0";
							break;
						case Variant::INT:
						case Variant::FLOAT:
							plc.quoted = param->value.stringify();
							break;
						default:
							if (connection->driver->has_features(SQLDriver::FEAT_QUOTE)) {
								plc.quoted = connection->impl->quote(param->value);
								if (plc.quoted.is_empty()) {
									return false;
								}
							} else {
								plc.quoted = param->value.stringify();
							}
							break;
					}
				}
			}
		} else if (query_type == SQLDriverStatement::PLACEHOLDER_POSITIONAL) {
			// Rewrite ? to :paramX
			String tmpl(named_rewrite_template);
			if (tmpl.is_empty()) {
				tmpl = ":gd%d";
			}

			placeholder_position = 1;
			for (Placeholder &plc : placeholders) {
				if (plc.position < 0) {
					continue;
				}

				const String name = String::utf32_unchecked(plc.name);
				if (name == "?" || !parameters_map.has(name)) {
					plc.quoted = vformat(tmpl, placeholder_position++);
					if (!named_rewrite_template.is_empty()) {
						parameters_map[name] = plc.quoted;
					}
				} else {
					plc.quoted = name;
				}
				parameters_map[plc.position] = plc.quoted;
			}
		} else {
			// Rewrite :name to ?
			for (Placeholder &plc : placeholders) {
				parameters_map[plc.position] = String::utf32_unchecked(plc.name);
				plc.quoted = "?";
			}
		}
	}

	// Build the query
	c = query_string.ptr();
	active_query_string.clear();
	for (const Placeholder &plc : placeholders) {
		const size_t len = plc.name.ptr() - c;
		if (len) {
			active_query_string.append_utf32_unchecked(Span<char32_t>(c, len));
		}
		if (plc.quoted.is_empty()) {
			active_query_string.append_utf32_unchecked(plc.name);
		} else {
			active_query_string += plc.quoted;
		}
		c = plc.name.ptr() + plc.name.size();
	}
	const size_t remain_len = query_string.ptr() + query_string.length() - c;
	if (remain_len) {
		active_query_string.append_utf32_unchecked(Span<char32_t>(c, remain_len));
	}

	return true;
}

bool SQLStatement::bind_value(const Variant &p_param, const Variant &p_value) {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();

	Parameter parameter;
	if (p_param.is_num()) {
		parameter.index = p_param;
		ERR_FAIL_COND_V(parameter.index <= 0, false);
		--parameter.index; // Convert to zero-based index
	} else if (p_param.is_string()) {
		parameter.name = p_param;
		ERR_FAIL_COND_V(parameter.name.is_empty(), false);
	} else {
		ERR_FAIL_V_MSG(false, "Parameter identifier must be either an integer index or a string name.");
	}
	parameter.value = p_value;

	return _register_parameter(parameter);
}

bool SQLStatement::execute(const Variant &p_parameters) {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();

	if (p_parameters.get_type() != Variant::NIL) {
		parameters.clear();

		if (p_parameters.get_type() == Variant::DICTIONARY) {
			const Dictionary dict = p_parameters;
			for (const auto &it : dict) {
				Parameter parameter;
				parameter.name = it.key;
				ERR_FAIL_COND_V(parameter.name.is_empty(), false);
				parameter.value = it.value;
				if (!_register_parameter(parameter)) {
					return false;
				}
			}
		} else if (p_parameters.get_type() == Variant::ARRAY) {
			const Array arr = p_parameters;
			for (int i = 0; i < arr.size(); ++i) {
				Parameter parameter;
				parameter.index = i;
				parameter.value = arr[i];
				if (!_register_parameter(parameter)) {
					return false;
				}
			}
		} else {
			ERR_FAIL_V_MSG(false, "Parameters must be either an Array or a Dictionary.");
		}
	}

	return _execute();
}

bool SQLStatement::_execute() {
	if (impl->get_placeholder_type() == SQLDriverStatement::PLACEHOLDER_NONE) {
		if (!_parse_parameters()) {
			return false;
		}
	} else {
		for (auto &it : parameters) {
			if (!impl->handle_parameter_event(SQLDriverStatement::PARAMETER_EVENT_PRE_EXEC, parameters_map, it.value)) {
				return false;
			}
		}
	}

	ERR_FAIL_COND_V(active_query_string.is_empty(), false);
	if (!impl->execute(active_query_string, parameters)) {
		return false;
	}

	if (executed) {
		return true;
	}

	if (!impl->describe_columns(columns)) {
		return false;
	}
	switch (connection->columns_case_mode) {
		case CASE_UPPER:
			for (Column &column : columns) {
				column.name = column.name.to_upper();
			}
			break;
		case CASE_LOWER:
			for (Column &column : columns) {
				column.name = column.name.to_lower();
			}
			break;
		default:
			break;
	}

	executed = true;
	return true;
}

int64_t SQLStatement::get_row_count() const {
	ERR_FAIL_NULL_V(impl, 0);
	return impl->get_row_count();
}

int SQLStatement::get_column_count() const {
	ERR_FAIL_NULL_V(impl, 0);
	return (int)columns.size();
}

String SQLStatement::get_column_name(int p_column) const {
	ERR_FAIL_NULL_V(impl, String());
	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), String());
	return columns[p_column].name;
}

int SQLStatement::get_column_length(int p_column) const {
	ERR_FAIL_NULL_V(impl, -1);
	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), -1);
	return columns[p_column].length;
}

int SQLStatement::get_column_precision(int p_column) const {
	ERR_FAIL_NULL_V(impl, 0);
	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), 0);
	return columns[p_column].precision;
}

Variant::Type SQLStatement::get_column_type(int p_column) const {
	ERR_FAIL_NULL_V(impl, Variant::NIL);
	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), Variant::NIL);
	return columns[p_column].type;
}

Dictionary SQLStatement::get_column_meta(int p_column) const {
	ERR_FAIL_NULL_V(impl, Dictionary());
	ERR_FAIL_INDEX_V(p_column, (int)columns.size(), Dictionary());

	Dictionary meta;
	const Column &column = columns[p_column];
	meta["name"] = column.name;
	meta["len"] = column.length;
	meta["precision"] = column.precision;
	meta["type"] = column.type;
	if (!impl->get_column_meta(p_column, meta)) {
		return Dictionary();
	}
	return meta;
}

void SQLStatement::debug_dump_params() const {
	ERR_FAIL_NULL(impl);

	print_line("SQL: [", query_string.length(), "] ", query_string);
	if (!active_query_string.is_empty() && active_query_string != query_string) {
		print_line("Sent SQL: [", active_query_string.length(), "] ", active_query_string);
	}

	print_line("Params:  ", parameters.size());
	for (const auto &it : parameters) {
		if (it.key.is_string()) {
			const String name = it.key;
			print_line("Key: Name: [", name.length(), "] ", name);
		} else {
			print_line("Key: Position #", it.key, ":");
		}

		print_line("index=", it.value.index);
		print_line("name=[", it.value.name.length(), "] \"", it.value.name, "\"");
	}
}

String SQLStatement::get_error_code() const {
	return error.get_error_code();
}

Array SQLStatement::get_error_info() const {
	return error.to_array();
}

void SQLStatement::_push_error(const SQLError &p_error) {
	error = p_error;
	ERR_FAIL_COND(connection.is_null());
	connection->_handle_error(p_error);
}

void SQLStatement::_bind_methods() {
	BIND_ENUM_CONSTANT(ATTR_ERRMODE);
	BIND_ENUM_CONSTANT(ATTR_DEFAULT_FETCH_MODE);
	BIND_ENUM_CONSTANT(ATTR_EMULATE_PREPARES);
	BIND_ENUM_CONSTANT(ATTR_DISABLE_PREPARES);
	BIND_ENUM_CONSTANT(ATTR_CURSOR);
	BIND_ENUM_CONSTANT(ATTR_FETCH_NULLS);
	BIND_ENUM_CONSTANT(ATTR_COLUMNS_CASE);
	BIND_ENUM_CONSTANT(ATTR_CONNECT_TIMEOUT);
	BIND_ENUM_CONSTANT(ATTR_DRIVER_NAME);
	BIND_ENUM_CONSTANT(ATTR_CLIENT_VERSION);
	BIND_ENUM_CONSTANT(ATTR_SERVER_VERSION);
	BIND_ENUM_CONSTANT(ATTR_CONNECTION_STATUS);
	BIND_ENUM_CONSTANT(ATTR_SERVER_INFO);
	BIND_ENUM_CONSTANT(ATTR_RESULT_MEMORY_SIZE);

	BIND_ENUM_CONSTANT(ERRMODE_SILENT);
	BIND_ENUM_CONSTANT(ERRMODE_WARNING);
	BIND_ENUM_CONSTANT(ERRMODE_ERROR);

	BIND_ENUM_CONSTANT(FETCH_DEFAULT);
	BIND_ENUM_CONSTANT(FETCH_BOTH);
	BIND_ENUM_CONSTANT(FETCH_ASSOC);
	BIND_ENUM_CONSTANT(FETCH_NAMED);
	BIND_ENUM_CONSTANT(FETCH_ARRAY);
	BIND_ENUM_CONSTANT(FETCH_KEY_PAIR);
	BIND_ENUM_CONSTANT(FETCH_CLASS);
	BIND_ENUM_CONSTANT(FETCH_INTO);
	BIND_ENUM_CONSTANT(FETCH_COLUMN);
	BIND_ENUM_CONSTANT(FETCH_CALL);
	BIND_ENUM_CONSTANT(FETCH_GROUP);
	BIND_ENUM_CONSTANT(FETCH_UNIQUE);
	BIND_ENUM_CONSTANT(FETCH_CLASSNAME);

	BIND_ENUM_CONSTANT(CURSOR_FWDONLY);
	BIND_ENUM_CONSTANT(CURSOR_SCROLL);

	BIND_ENUM_CONSTANT(FETCH_ORI_NEXT);
	BIND_ENUM_CONSTANT(FETCH_ORI_PRIOR);
	BIND_ENUM_CONSTANT(FETCH_ORI_FIRST);
	BIND_ENUM_CONSTANT(FETCH_ORI_LAST);
	BIND_ENUM_CONSTANT(FETCH_ORI_ABS);
	BIND_ENUM_CONSTANT(FETCH_ORI_REL);

	BIND_ENUM_CONSTANT(NULL_NATURAL);
	BIND_ENUM_CONSTANT(NULL_EMPTY_STRING);
	BIND_ENUM_CONSTANT(NULL_TO_STRING);

	BIND_ENUM_CONSTANT(CASE_NATURAL);
	BIND_ENUM_CONSTANT(CASE_LOWER);
	BIND_ENUM_CONSTANT(CASE_UPPER);

	ClassDB::bind_method(D_METHOD("bind_value", "param", "value"), &SQLStatement::bind_value);
	ClassDB::bind_method(D_METHOD("execute", "parameters"), &SQLStatement::execute, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_fetch_mode", "mode", "param"), &SQLStatement::set_fetch_mode, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_attribute", "attribute", "value"), &SQLStatement::set_attribute);
	ClassDB::bind_method(D_METHOD("get_attribute", "attribute"), &SQLStatement::get_attribute);
	ClassDB::bind_method(D_METHOD("close_cursor"), &SQLStatement::close_cursor);
	ClassDB::bind_method(D_METHOD("next_rowset"), &SQLStatement::next_rowset);
	ClassDB::bind_method(D_METHOD("fetch_all", "mode", "param"), &SQLStatement::fetch_all, DEFVAL(FETCH_DEFAULT), DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("fetch", "mode", "orientation", "offset"), &SQLStatement::fetch, DEFVAL(FETCH_DEFAULT), DEFVAL(FETCH_ORI_NEXT), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("fetch_column", "column"), &SQLStatement::fetch_column, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("fetch_object", "class_name"), &SQLStatement::fetch_object);

	ClassDB::bind_method(D_METHOD("get_row_count"), &SQLStatement::get_row_count);
	ClassDB::bind_method(D_METHOD("get_column_count"), &SQLStatement::get_column_count);
	ClassDB::bind_method(D_METHOD("get_column_name", "column"), &SQLStatement::get_column_name);
	ClassDB::bind_method(D_METHOD("get_column_length", "column"), &SQLStatement::get_column_length);
	ClassDB::bind_method(D_METHOD("get_column_precision", "column"), &SQLStatement::get_column_precision);
	ClassDB::bind_method(D_METHOD("get_column_type", "column"), &SQLStatement::get_column_type);
	ClassDB::bind_method(D_METHOD("get_column_meta", "column"), &SQLStatement::get_column_meta);
	ClassDB::bind_method(D_METHOD("get_error_code"), &SQLStatement::get_error_code);
	ClassDB::bind_method(D_METHOD("get_error_info"), &SQLStatement::get_error_info);
	ClassDB::bind_method(D_METHOD("get_query_string"), &SQLStatement::get_query_string);
	ClassDB::bind_method(D_METHOD("get_fetch_mode"), &SQLStatement::get_fetch_mode);
	ClassDB::bind_method(D_METHOD("get_fetch_param"), &SQLStatement::get_fetch_param);
	ClassDB::bind_method(D_METHOD("debug_dump_params"), &SQLStatement::debug_dump_params);
}

//
// SQLConnection implementation
//

SQLConnection::SQLConnection() {
}

SQLConnection::~SQLConnection() {
	DEV_ASSERT(statement_count <= 0); // Should not happen due to reference counting
	close();
}

bool SQLConnection::set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) {
	ERR_FAIL_NULL_V(impl, false);
	error.clear();
	return _set_attribute(p_attribute, p_value);
}

bool SQLConnection::_set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) {
	switch (p_attribute) {
		case SQLStatement::ATTR_ERRMODE: {
			ERR_FAIL_COND_V_MSG(!Variant::can_convert(p_value.get_type(), Variant::INT), false, "Error mode must be an integer.");
			const SQLStatement::ErrorMode mode = p_value;
			ERR_FAIL_COND_V_MSG(mode < SQLStatement::ERRMODE_SILENT || mode >= SQLStatement::ERRMODE_MAX, false, "Invalid error mode value.");
			error_mode = mode;
			return true;
		}
		case SQLStatement::ATTR_DEFAULT_FETCH_MODE: {
			int fetch_mode = 0;
			if (Variant::can_convert(p_value.get_type(), Variant::INT)) {
				fetch_mode = p_value;
			} else if (Variant::can_convert(p_value.get_type(), Variant::PACKED_INT32_ARRAY)) {
				const PackedInt32Array fetch_modes = p_value;
				for (const int mode : fetch_modes) {
					fetch_mode |= mode;
				}
			} else if (Variant::can_convert(p_value.get_type(), Variant::PACKED_INT64_ARRAY)) {
				const PackedInt64Array fetch_modes = p_value;
				for (const int64_t mode : fetch_modes) {
					fetch_mode |= (int)mode;
				}
			} else {
				ERR_FAIL_V_MSG("Fetch mode must be an integer or integer array.", false);
			}
			if (!_validate_fetch_mode(fetch_mode, Variant(), false)) {
				return false;
			}
			default_fetch_mode = fetch_mode;
			return true;
		}
		case SQLStatement::ATTR_FETCH_NULLS: {
			ERR_FAIL_COND_V_MSG(!Variant::can_convert(p_value.get_type(), Variant::INT), false, "Nulls mode must be an integer.");
			const SQLStatement::NullMode mode = p_value;
			ERR_FAIL_COND_V_MSG(mode < SQLStatement::NULL_NATURAL || mode >= SQLStatement::NULL_MAX, false, "Invalid nulls mode value.");
			fetch_nulls_mode = mode;
			return true;
		}
		case SQLStatement::ATTR_COLUMNS_CASE: {
			ERR_FAIL_COND_V_MSG(!Variant::can_convert(p_value.get_type(), Variant::INT), false, "Columns case mode must be an integer.");
			const SQLStatement::CaseMode mode = p_value;
			ERR_FAIL_COND_V_MSG(mode < SQLStatement::CASE_NATURAL || mode >= SQLStatement::CASE_MAX, false, "Invalid columns case mode value.");
			columns_case_mode = mode;
			return true;
		}
		default:
			return impl->set_attribute(p_attribute, p_value);
	}
}

Variant SQLConnection::get_attribute(SQLStatement::Attribute p_attribute) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();

	switch (p_attribute) {
		case SQLStatement::ATTR_ERRMODE:
			return error_mode;
		case SQLStatement::ATTR_DEFAULT_FETCH_MODE:
			return default_fetch_mode;
		case SQLStatement::ATTR_DRIVER_NAME:
			return driver->get_name();
		case SQLStatement::ATTR_FETCH_NULLS:
			return fetch_nulls_mode;
		case SQLStatement::ATTR_COLUMNS_CASE:
			return columns_case_mode;
		default:
			return impl->get_attribute(p_attribute);
	}
}

Error SQLConnection::open(const String &p_data_source_name, const String &p_username, const String &p_password, const Dictionary &p_options) {
	ERR_FAIL_COND_V_MSG(statement_count > 0, ERR_BUSY, "Cannot open a SQL connection while there are open statements referencing it.");

	close();
	error.clear();

	HashMap<SQLStatement::Attribute, Variant> options;
	for (const KeyValue<Variant, Variant> &it : p_options) {
		ERR_FAIL_COND_V_MSG(!Variant::can_convert(it.key.get_type(), Variant::INT), ERR_INVALID_PARAMETER, "Attribute key must be an integer.");
		options.insert(it.key, it.value);
	}

	if (options.has(SQLStatement::ATTR_ERRMODE)) {
		ERR_FAIL_COND_V(!_set_attribute(SQLStatement::ATTR_ERRMODE, options[SQLStatement::ATTR_ERRMODE]), ERR_INVALID_PARAMETER);
	} else {
		error_mode = SQLStatement::ERRMODE_ERROR;
	}

	// TODO: Allow registering data sources in project settings and p_data_source_name is just the alias

	const int colon_pos(p_data_source_name.find_char(':'));
	ERR_FAIL_COND_V_MSG(colon_pos == -1, ERR_INVALID_PARAMETER, "SQL driver name is missing from the connection string.");

	const String driver_name(p_data_source_name.substr(0, colon_pos));
	const String data_source_name(p_data_source_name.substr(colon_pos + 1));

	driver = SQLDriver::get_driver_by_name(driver_name);
	ERR_FAIL_COND_V_MSG(driver.is_null(), ERR_UNAVAILABLE, vformat("SQL driver '%s' not found.", driver_name));

	impl = driver->create_connection();
	if (!impl) {
		close();
		return FAILED;
	}
	impl->connection = this;

	const Error err(impl->open(data_source_name, p_username, p_password, options));
	if (err != OK) {
		close();
		return err;
	}

	for (const auto &it : options) {
		if (it.key != SQLStatement::ATTR_ERRMODE && !_set_attribute(it.key, it.value)) {
			close();
			return ERR_INVALID_PARAMETER;
		}
	}
	return OK;
}

void SQLConnection::close() {
	ERR_FAIL_COND_MSG(statement_count > 0, "Cannot close a SQL connection while there are open statements referencing it.");

	if (impl) {
		memdelete(impl);
		impl = nullptr;
	}

	driver.unref();
	statement_count = 0;
	is_in_transaction = false;
	// Don't clear error so users can check why the connection failed
}

bool SQLConnection::is_open() const {
	return impl != nullptr;
}

Ref<SQLStatement> SQLConnection::_prepare(const String &p_query, const HashMap<SQLStatement::Attribute, Variant> &p_options) {
	ERR_FAIL_COND_V_MSG(p_query.is_empty(), Ref<SQLStatement>(), "Query string cannot be empty.");

	SQLDriverStatement *const stmt_impl(impl->create_statement());
	if (!stmt_impl) {
		return Ref<SQLStatement>();
	}

	Ref<SQLStatement> stmt;
	stmt.instantiate();
	++statement_count;
	stmt->connection = this;
	stmt->impl = stmt_impl;
	stmt->query_string = p_query;
	stmt_impl->statement = stmt.ptr();

	if (!stmt->impl->prepare(p_options) || !stmt->_parse_parameters()) {
		error = stmt->error;
		return Ref<SQLStatement>();
	}

	return stmt;
}

Ref<SQLStatement> SQLConnection::query(const String &p_query, int p_fetch_mode, const Variant &p_fetch_param) {
	ERR_FAIL_NULL_V(impl, Ref<SQLStatement>());
	error.clear();

	if (!_validate_fetch_mode(p_fetch_mode, p_fetch_param, true)) {
		return Ref<SQLStatement>();
	}

	Ref<SQLStatement> stmt = _prepare(p_query, {});
	if (stmt.is_null()) {
		return Ref<SQLStatement>();
	}

	if (!stmt->_execute()) {
		error = stmt->error;
		return Ref<SQLStatement>();
	}

	stmt->fetch_mode = p_fetch_mode;
	stmt->fetch_param = p_fetch_param;
	return stmt;
}

Ref<SQLStatement> SQLConnection::prepare(const String &p_query, const Dictionary &p_options) {
	ERR_FAIL_NULL_V(impl, Ref<SQLStatement>());
	error.clear();

	HashMap<SQLStatement::Attribute, Variant> options;
	for (const KeyValue<Variant, Variant> &it : p_options) {
		ERR_FAIL_COND_V_MSG(!Variant::can_convert(it.key.get_type(), Variant::INT), Ref<SQLStatement>(), "Attribute key must be an integer.");
		options.insert(it.key, it.value);
	}

	return _prepare(p_query, options);
}

int64_t SQLConnection::exec(const String &p_statement) {
	ERR_FAIL_NULL_V(impl, -1);
	error.clear();

	ERR_FAIL_COND_V_MSG(p_statement.is_empty(), -1, "Cannot execute an empty statement.");

	return impl->exec(p_statement);
}

bool SQLConnection::begin_transaction() {
	ERR_FAIL_NULL_V(impl, false);

	ERR_FAIL_COND_V_MSG(in_transaction(), false, "Cannot begin a new transaction when already in a transaction.");
	if (!driver->has_features(SQLDriver::FEAT_TRANSACTIONS)) {
		_push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), String("Driver does not support transactions.")));
		return false;
	}

	if (impl->begin_transaction()) {
		is_in_transaction = true;
		return true;
	}
	return false;
}

bool SQLConnection::commit() {
	ERR_FAIL_NULL_V(impl, false);

	ERR_FAIL_COND_V_MSG(!in_transaction(), false, "Cannot commit when not in a transaction.");

	if (impl->commit()) {
		is_in_transaction = false;
		return true;
	}
	return false;
}

bool SQLConnection::rollback() {
	ERR_FAIL_NULL_V(impl, false);

	ERR_FAIL_COND_V_MSG(!in_transaction(), false, "Cannot rollback when not in a transaction.");

	if (impl->rollback()) {
		is_in_transaction = false;
		return true;
	}
	return false;
}

bool SQLConnection::in_transaction() const {
	ERR_FAIL_NULL_V(impl, false);

	if (driver->has_features(SQLDriver::FEAT_IN_TRANSACTION)) {
		return impl->in_transaction();
	}
	return is_in_transaction;
}

Variant SQLConnection::get_last_insert_id(const String &p_name) {
	ERR_FAIL_NULL_V(impl, Variant());
	error.clear();

	if (!driver->has_features(SQLDriver::FEAT_LAST_INSERT_ID)) {
		_push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), String("Driver does not support retrieving last insert ID.")));
		return Variant();
	}

	return impl->get_last_insert_id(p_name);
}

String SQLConnection::quote(const Variant &p_value) {
	ERR_FAIL_NULL_V(impl, String());
	error.clear();

	if (!driver->has_features(SQLDriver::FEAT_QUOTE)) {
		_push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), String("Driver does not support quoting.")));
		return String();
	}

	return impl->quote(p_value);
}

String SQLConnection::get_error_code() const {
	return error.get_error_code();
}

Array SQLConnection::get_error_info() const {
	return error.to_array();
}

void SQLConnection::_push_error(const SQLError &p_error) {
	error = p_error;
	_handle_error(p_error);
}

void SQLConnection::_handle_error(const SQLError &p_error) const {
	if (error_mode == SQLStatement::ERRMODE_SILENT || p_error.message.is_empty()) {
		return;
	}

	String message(vformat("SQL Error %s: %s", p_error.get_error_code(), p_error.message.strip_edges()));
	if (!p_error.driver_code.is_empty()) {
		message += vformat(" (Driver code: %s)", p_error.driver_code);
	}
	const ErrorHandlerType type(error_mode == SQLStatement::ERRMODE_WARNING ? ERR_HANDLER_WARNING : ERR_HANDLER_ERROR);
	_err_print_error(p_error.function ? p_error.function : "", p_error.file ? p_error.file : "", p_error.line, message, false, type);
}

Ref<SQLConnection> SQLConnection::open_connection(const String &p_data_source_name, const String &p_username,
		const String &p_password, const Dictionary &p_options) {
	Ref<SQLConnection> connection;
	connection.instantiate();
	if (connection->open(p_data_source_name, p_username, p_password, p_options) != OK) {
		return Ref<SQLConnection>();
	}
	return connection;
}

PackedStringArray SQLConnection::get_available_drivers() {
	return SQLDriver::get_driver_names();
}

void SQLConnection::_bind_methods() {
	BIND_CONSTANT(DEFAULT_CONNECT_TIMEOUT);

	ClassDB::bind_method(D_METHOD("open", "data_source_name", "username", "password", "options"),
			&SQLConnection::open, DEFVAL(String()), DEFVAL(String()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("close"), &SQLConnection::close);
	ClassDB::bind_method(D_METHOD("exec", "statement"), &SQLConnection::exec);
	ClassDB::bind_method(D_METHOD("query", "query", "fetch_mode", "fetch_param"), &SQLConnection::query, DEFVAL(SQLStatement::FETCH_DEFAULT), DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("prepare", "query", "options"), &SQLConnection::prepare, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("begin_transaction"), &SQLConnection::begin_transaction);
	ClassDB::bind_method(D_METHOD("commit"), &SQLConnection::commit);
	ClassDB::bind_method(D_METHOD("rollback"), &SQLConnection::rollback);
	ClassDB::bind_method(D_METHOD("get_last_insert_id", "name"), &SQLConnection::get_last_insert_id, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("quote", "value"), &SQLConnection::quote);
	ClassDB::bind_method(D_METHOD("set_attribute", "attribute", "value"), &SQLConnection::set_attribute);
	ClassDB::bind_method(D_METHOD("get_attribute", "attribute"), &SQLConnection::get_attribute);

	ClassDB::bind_method(D_METHOD("is_open"), &SQLConnection::is_open);
	ClassDB::bind_method(D_METHOD("in_transaction"), &SQLConnection::in_transaction);
	ClassDB::bind_method(D_METHOD("get_error_code"), &SQLConnection::get_error_code);
	ClassDB::bind_method(D_METHOD("get_error_info"), &SQLConnection::get_error_info);

	ClassDB::bind_static_method("SQLConnection", D_METHOD("open_connection", "data_source_name", "username", "password", "options"),
			&SQLConnection::open_connection, DEFVAL(String()), DEFVAL(String()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("SQLConnection", D_METHOD("get_available_drivers"), &SQLConnection::get_available_drivers);
}

//
// SQLDriverStatement implementation
//

SQLDriverStatement::SQLDriverStatement() {
}

SQLDriverStatement::~SQLDriverStatement() {
}

bool SQLDriverStatement::get_column_meta(int p_column, Dictionary &r_meta) const {
	return true;
}

bool SQLDriverStatement::handle_parameter_event(ParameterEvent p_event, const HashMap<Variant, String> &p_parameters_map, SQLStatement::Parameter &r_parameter) {
	return true;
}

bool SQLDriverStatement::set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) {
	push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), vformat("Attribute %d is not supported by this driver.", p_attribute)));
	return false;
}

Variant SQLDriverStatement::get_attribute(SQLStatement::Attribute p_attribute) {
	push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), vformat("Attribute %d is not supported by this driver.", p_attribute)));
	return Variant();
}

int64_t SQLDriverStatement::get_row_count() const {
	return 0;
}

SQLDriverStatement::PlaceholderType SQLDriverStatement::get_placeholder_type() const {
	return PLACEHOLDER_NONE;
}

String SQLDriverStatement::get_named_rewrite_template() const {
	return String();
}

bool SQLDriverStatement::close_cursor() {
	return true;
}

bool SQLDriverStatement::do_next_rowset() {
	return true;
}

bool SQLDriverStatement::prepare(const HashMap<SQLStatement::Attribute, Variant> &p_options) {
	return true;
}

SQLDriverStatement::TokenType SQLDriverStatement::parse_query_token(const char32_t *&r_cur) const {
	// Double quoted string ("...")
	if (*r_cur == U'"') {
		++r_cur;
		while (*r_cur) {
			if (*r_cur == U'"') {
				// Escaped double quote ""
				if (r_cur[1] == U'"') {
					r_cur += 2;
					continue;
				}
				++r_cur;
				break;
			}
			++r_cur;
		}
		return TOKEN_TEXT;
	}

	// Single quoted string ('...')
	if (*r_cur == U'\'') {
		++r_cur;
		while (*r_cur) {
			if (*r_cur == U'\'') {
				// Escaped single quote ''
				if (r_cur[1] == U'\'') {
					r_cur += 2;
					continue;
				}
				++r_cur;
				break;
			}
			++r_cur;
		}
		return TOKEN_TEXT;
	}

	// Multiple colons (::, :::, etc.)
	if (r_cur[0] == U':' && r_cur[1] == U':') {
		const char32_t *p = r_cur + 2;
		while (*p == U':') {
			++p;
		}
		r_cur = p;
		return TOKEN_TEXT;
	}

	// Multiple question marks (??, ???, etc.)
	if (r_cur[0] == U'?' && r_cur[1] == U'?') {
		const char32_t *p = r_cur + 2;
		while (*p == U'?') {
			++p;
		}
		r_cur = p;
		return TOKEN_TEXT;
	}

	// Named parameter binding (:name)
	if (*r_cur == U':') {
		const char32_t *p = r_cur + 1;
		if (((*p >= U'a' && *p <= U'z') || (*p >= U'A' && *p <= U'Z') || (*p >= U'0' && *p <= U'9') || *p == U'_')) {
			++p;
			while (((*p >= U'a' && *p <= U'z') || (*p >= U'A' && *p <= U'Z') || (*p >= U'0' && *p <= U'9') || *p == U'_')) {
				++p;
			}
			r_cur = p;
			return TOKEN_NAMED_PARAMETER;
		}
	}

	// Positional parameter binding (?)
	if (*r_cur == U'?') {
		++r_cur;
		return TOKEN_POSITIONAL_PARAMETER;
	}

	// Comments: /* ... */ or -- ...
	if (*r_cur == U'/' && r_cur[1] == U'*') {
		const char32_t *p = r_cur + 2;
		while (*p && !(p[0] == U'*' && p[1] == U'/')) {
			++p;
		}
		if (*p) {
			p += 2;
		}
		r_cur = p;
		return TOKEN_TEXT;
	}
	if (*r_cur == U'-' && r_cur[1] == U'-') {
		const char32_t *p = r_cur + 2;
		while (*p && *p != U'\n') {
			++p;
		}
		r_cur = p;
		return TOKEN_TEXT;
	}

	// Treat each as a single-character text token
	if (*r_cur == U':' || *r_cur == U'?' || *r_cur == U'"' || *r_cur == U'\'' || *r_cur == U'/' || *r_cur == U'-') {
		++r_cur;
		return TOKEN_TEXT;
	}

	// Text until a special character is found
	const char32_t *p = r_cur;
	while (*p && *p != U':' && *p != U'?' && *p != U'"' && *p != U'\'' && *p != U'/' && *p != U'-') {
		++p;
	}
	if (p != r_cur) {
		r_cur = p;
		return TOKEN_TEXT;
	}

	// Skip one character as text
	++r_cur;
	return TOKEN_TEXT;
}

void SQLDriverStatement::push_error(const SQLError &p_error) {
	ERR_FAIL_NULL(statement);
	statement->_push_error(p_error);
}

//
// SQLDriverConnection implementation
//

SQLDriverConnection::SQLDriverConnection() {
}

SQLDriverConnection::~SQLDriverConnection() {
}

bool SQLDriverConnection::set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) {
	push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), vformat("Attribute %d is not supported by this driver.", p_attribute)));
	return false;
}

Variant SQLDriverConnection::get_attribute(SQLStatement::Attribute p_attribute) {
	push_error(SQL_ERROR(SQL_CODE_UNSUPPORTED, String(), vformat("Attribute %d is not supported by this driver.", p_attribute)));
	return Variant();
}

bool SQLDriverConnection::begin_transaction() {
	return false;
}

bool SQLDriverConnection::commit() {
	return false;
}

bool SQLDriverConnection::rollback() {
	return false;
}

bool SQLDriverConnection::in_transaction() const {
	return false;
}

Variant SQLDriverConnection::get_last_insert_id(const String &p_name) {
	return Variant();
}

String SQLDriverConnection::quote(const Variant &p_value) {
	return String();
}

void SQLDriverConnection::push_error(const SQLError &p_error) {
	ERR_FAIL_NULL(connection);
	connection->_push_error(p_error);
}
