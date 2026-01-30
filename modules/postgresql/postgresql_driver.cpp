/**************************************************************************/
/*  postgresql_driver.cpp                                                 */
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

#include "postgresql_driver.h"

#define PGSQL_RES_ERROR(res, status) SQL_ERROR(String::utf8(pq->resultErrorField(res, PG_DIAG_SQLSTATE)), itos(status), String::utf8(pq->resultErrorMessage(res)))
#define PGSQL_CONN_ERROR(conn) SQL_ERROR(String(), itos(PGRES_FATAL_ERROR), String::utf8(pq->errorMessage(conn)))

//
// PostgreSQLStatement implementation
//

PostgreSQLStatement::PostgreSQLStatement(PostgreSQLConnection &p_con) :
		con(p_con) {
}

PostgreSQLStatement::~PostgreSQLStatement() {
	if (res) {
		pq->clear(res);
		res = nullptr;
	}

	if (is_prepared && !name.is_empty()) {
		if (pq->closePrepared) {
			res = pq->closePrepared(con.get_conn(), name.ptr());
		} else {
			const CharString deallocate_statement(vformat("DEALLOCATE %s", String::utf8(name)).utf8());
			res = pq->exec(con.get_conn(), deallocate_statement.ptr());
		}
		if (res) {
			pq->clear(res);
			res = nullptr;
		}
		name = CharString();
	}

	if (!cursor_name.is_empty()) {
		const CharString close_statement(vformat("CLOSE %s", cursor_name).utf8());
		pq->clear(pq->exec(con.get_conn(), close_statement.ptr()));
		cursor_name.clear();
	}
}

bool PostgreSQLStatement::prepare(const HashMap<SQLStatement::Attribute, Variant> &p_options) {
	if (p_options.has(SQLStatement::ATTR_EMULATE_PREPARES)) {
		ERR_FAIL_COND_V(!Variant::can_convert(p_options[SQLStatement::ATTR_EMULATE_PREPARES].get_type(), Variant::BOOL), false);
		emulate = p_options[SQLStatement::ATTR_EMULATE_PREPARES];
	} else {
		emulate = con.has_emulate_prepares();
	}

	bool execute_only;
	if (p_options.has(SQLStatement::ATTR_DISABLE_PREPARES)) {
		ERR_FAIL_COND_V(!Variant::can_convert(p_options[SQLStatement::ATTR_DISABLE_PREPARES].get_type(), Variant::BOOL), false);
		execute_only = p_options[SQLStatement::ATTR_DISABLE_PREPARES];
	} else {
		execute_only = con.has_disable_prepares();
	}

	if (p_options.has(SQLStatement::ATTR_CURSOR)) {
		ERR_FAIL_COND_V(!Variant::can_convert(p_options[SQLStatement::ATTR_CURSOR].get_type(), Variant::INT), false);
		const SQLStatement::CursorType cursor_type = p_options[SQLStatement::ATTR_CURSOR];
		ERR_FAIL_COND_V(cursor_type < SQLStatement::CURSOR_FWDONLY || cursor_type >= SQLStatement::CURSOR_MAX, false);
		if (cursor_type == SQLStatement::CURSOR_SCROLL) {
			emulate = true;
			cursor_name = vformat("gd_crsr_%08x", con.increment_statement_counter());
		}
	}

	if (!emulate && !execute_only) {
		// Prepared query: set the query name and defer the actual prepare until the first execute call
		name = vformat("gd_stmt_%d", con.increment_statement_counter()).utf8();
	}

	return true;
}

bool PostgreSQLStatement::handle_parameter_event(ParameterEvent p_event, const HashMap<Variant, String> &p_parameters_map, SQLStatement::Parameter &r_parameter) {
	switch (p_event) {
		case PARAMETER_EVENT_NORMALIZE:
			if (emulate) {
				// We need to manually convert to a pg native boolean value
				if (r_parameter.value.get_type() == Variant::BOOL) {
					r_parameter.value = r_parameter.value.booleanize() ? "t" : "f";
				}
			} else if (!r_parameter.name.is_empty()) {
				// Decode name from $1, $2 into 0, 1 etc.
				if (r_parameter.name[0] == '$') {
					r_parameter.index = r_parameter.name.substr(1).to_int();
				} else if (!p_parameters_map.has(r_parameter.name)) {
					push_error(SQL_ERROR("HY093", String(), r_parameter.name));
					return false;
				} else {
					r_parameter.index = p_parameters_map[r_parameter.name].substr(1).to_int() - 1;
				}
			}
			return true;
		case PARAMETER_EVENT_ADD:
			if (!p_parameters_map.is_empty() && !p_parameters_map.has(r_parameter.index)) {
				push_error(SQL_ERROR("HY093", String(), "Parameter was not defined."));
				return false;
			}
			return true;
		case PARAMETER_EVENT_PRE_EXEC:
			if (p_parameters_map.is_empty()) {
				return true;
			}
			if (parameter_pointers.is_empty()) {
				parameter_pointers.resize_initialized(p_parameters_map.size());
				parameter_values.resize(parameter_pointers.size());
				parameter_lengths.resize_initialized(parameter_pointers.size());
				parameter_formats.resize_initialized(parameter_pointers.size());
			}
			if (r_parameter.index >= 0) {
				switch (r_parameter.value.get_type()) {
					case Variant::NIL:
						parameter_pointers[r_parameter.index] = nullptr;
						break;
					case Variant::BOOL:
						parameter_pointers[r_parameter.index] = r_parameter.value.booleanize() ? "t" : "f";
						parameter_lengths[r_parameter.index] = 1;
						break;
					case Variant::PACKED_BYTE_ARRAY:
						parameter_values[r_parameter.index] = r_parameter.value;
						parameter_pointers[r_parameter.index] = (const char *)parameter_values[r_parameter.index].ptr();
						parameter_lengths[r_parameter.index] = (int)parameter_values[r_parameter.index].size();
						parameter_formats[r_parameter.index] = 1;
						break;
					default:
						parameter_values[r_parameter.index] = r_parameter.value.stringify().to_utf8_buffer();
						parameter_pointers[r_parameter.index] = (const char *)parameter_values[r_parameter.index].ptr();
						parameter_lengths[r_parameter.index] = (int)parameter_values[r_parameter.index].size();
						break;
				}
			}
			return true;
		default:
			return true;
	}
}

bool PostgreSQLStatement::execute(const String &p_statement, const HashMap<Variant, SQLStatement::Parameter> &p_parameters) {
	// Ensure that we free any previous unfetched results
	if (res) {
		pq->clear(res);
		res = nullptr;
	}
	current_row = 0;

	if (!cursor_name.is_empty()) {
		if (is_prepared) {
			const CharString close_statement(vformat("CLOSE %s", cursor_name).utf8());
			pq->clear(pq->exec(con.get_conn(), close_statement.ptr()));
		}

		const CharString declare_statement(vformat("DECLARE %s SCROLL CURSOR WITH HOLD FOR %s", cursor_name, p_statement).utf8());
		res = pq->exec(con.get_conn(), declare_statement.ptr());

		// Check if declare failed
		const ExecStatusType status = pq->resultStatus(res);
		if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
			push_error(PGSQL_RES_ERROR(res, status));
			return false;
		}
		pq->clear(res);

		// The cursor was declared correctly
		is_prepared = true;

		// Fetch to be able to get the number of tuples later, but don't advance the cursor pointer
		const CharString fetch_statement(vformat("FETCH FORWARD 0 FROM %s", cursor_name).utf8());
		res = pq->exec(con.get_conn(), fetch_statement.ptr());
	} else {
		const CharString utf8_statement(p_statement.utf8());

		if (name.is_empty()) {
			if (emulate) {
				// Execute plain query (with embedded parameters)
				res = pq->exec(con.get_conn(), utf8_statement.ptr());
			} else {
				// Execute query with parameters
				res = pq->execParams(con.get_conn(), utf8_statement.ptr(), (int)p_parameters.size(), nullptr,
						parameter_pointers.ptr(), parameter_lengths.ptr(), parameter_formats.ptr(), 0);
			}
		} else {
			// Using a prepared statement
			if (!is_prepared) {
				res = pq->prepare(con.get_conn(), name.ptr(), utf8_statement.ptr(), (int)p_parameters.size(), nullptr);
				ExecStatusType status = pq->resultStatus(res);
				if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
					const char *const state = pq->resultErrorField(res, PG_DIAG_SQLSTATE);
					if (!state || strcmp(state, "42P05") != 0) {
						push_error(PGSQL_RES_ERROR(res, status));
						return false;
					}

					// Retry once if the prepared statement already existed
					pq->clear(res);

					if (pq->closePrepared) {
						res = pq->closePrepared(con.get_conn(), name.ptr());
					} else {
						const CharString deallocate_statement(vformat("DEALLOCATE %s", String::utf8(name)).utf8());
						res = pq->exec(con.get_conn(), deallocate_statement.ptr());
					}
					pq->clear(res);

					res = pq->prepare(con.get_conn(), name.ptr(), utf8_statement.ptr(), (int)p_parameters.size(), nullptr);
					status = pq->resultStatus(res);
					if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
						push_error(PGSQL_RES_ERROR(res, status));
						return false;
					}
				}

				pq->clear(res);
				res = nullptr;
				is_prepared = true;
			}

			res = pq->execPrepared(con.get_conn(), name.ptr(), (int)p_parameters.size(),
					parameter_pointers.ptr(), parameter_lengths.ptr(), parameter_formats.ptr(), 0);
		}
	}

	const ExecStatusType status(pq->resultStatus(res));
	if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK && status != PGRES_SINGLE_TUPLE) {
		push_error(PGSQL_RES_ERROR(res, status));
		return false;
	}

	if (status == PGRES_COMMAND_OK) {
		row_count = (int)String::utf8(pq->cmdTuples(res)).to_int();
	} else {
		row_count = pq->ntuples(res);
	}
	return true;
}

bool PostgreSQLStatement::fetch(SQLStatement::FetchOrientation p_orientation, int64_t p_offset) {
	if (!cursor_name.is_empty()) {
		String ori_str;
		switch (p_orientation) {
			case SQLStatement::FETCH_ORI_NEXT:
				ori_str = "NEXT";
				break;
			case SQLStatement::FETCH_ORI_PRIOR:
				ori_str = "BACKWARD";
				break;
			case SQLStatement::FETCH_ORI_FIRST:
				ori_str = "FIRST";
				break;
			case SQLStatement::FETCH_ORI_LAST:
				ori_str = "LAST";
				break;
			case SQLStatement::FETCH_ORI_ABS:
				ori_str = vformat("ABSOLUTE %d", p_offset);
				break;
			case SQLStatement::FETCH_ORI_REL:
				ori_str = vformat("RELATIVE %d", p_offset);
				break;
			default:
				return false;
		}

		if (res) {
			pq->clear(res);
			res = nullptr;
		}

		const CharString utf8_statement(vformat("FETCH %s FROM %s", ori_str, cursor_name).utf8());
		res = pq->exec(con.get_conn(), utf8_statement.ptr());

		const ExecStatusType status = pq->resultStatus(res);
		if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
			push_error(PGSQL_RES_ERROR(res, status));
			return false;
		}

		if (pq->ntuples(res) != 0) {
			current_row = 1;
			return true;
		} else {
			return false;
		}
	} else if (current_row < row_count) {
		++current_row;
		return true;
	} else {
		return false;
	}
}

static Variant _get_value(const PGresult *p_res, const Oid p_type, int p_row, int p_col) {
	if (pq->getisnull(p_res, p_row, p_col) != 0) {
		return Variant();
	}

	const char *const value_data = pq->getvalue(p_res, p_row, p_col);
	if (p_type == BYTEAOID) {
		size_t length;
		unsigned char *const data = pq->unescapeBytea((const unsigned char *)value_data, &length);
		ERR_FAIL_NULL_V(data, PackedByteArray());

		PackedByteArray byte_array;
		if (length > 0) {
			byte_array.resize_uninitialized(length);
			std::memcpy(byte_array.ptrw(), data, length);
		}
		pq->freemem(data);
		return byte_array;
	}

	const String value = String::utf8(value_data, pq->getlength(p_res, p_row, p_col));
	switch (p_type) {
		case BOOLOID:
			return value[0] == 't';
		case INT8OID:
		case INT2OID:
		case INT4OID:
			return value.to_int();
		case FLOAT4OID:
		case FLOAT8OID:
			if (value == "Infinity") {
				return Math::INF;
			} else if (value == "-Infinity") {
				return -Math::INF;
			} else if (value == "NaN") {
				return Math::NaN;
			}
			return value.to_float();
		default:
			return value;
	}
}

bool PostgreSQLStatement::get_value(int p_column, Variant &o_value) {
	o_value = _get_value(res, column_types[p_column], current_row - 1, p_column);
	return true;
}

bool PostgreSQLStatement::describe_columns(LocalVector<SQLStatement::Column> &o_columns) {
	const uint32_t column_count((uint32_t)pq->nfields(res));
	o_columns.resize(column_count);
	column_types.resize_initialized(column_count);

	for (uint32_t i = 0; i < column_count; ++i) {
		const Oid type = pq->ftype(res, i);
		column_types[i] = type;

		SQLStatement::Column &col(o_columns[i]);
		col.name = String::utf8(pq->fname(res, i));
		col.length = pq->fsize(res, i);
		col.precision = pq->fmod(res, i);
		if (col.precision == -1) {
			col.precision = 0;
		}
		switch (type) {
			case BOOLOID:
				col.type = Variant::BOOL;
				break;
			case INT2OID:
			case INT4OID:
			case INT8OID:
				col.type = Variant::INT;
				break;
			case FLOAT4OID:
			case FLOAT8OID:
				col.type = Variant::FLOAT;
				break;
			case BYTEAOID:
				col.type = Variant::PACKED_BYTE_ARRAY;
				break;
			default:
				col.type = Variant::STRING;
				break;
		}
	}
	return true;
}

int64_t PostgreSQLStatement::get_row_count() const {
	return row_count;
}

bool PostgreSQLStatement::get_column_meta(int p_column, Dictionary &r_meta) const {
	const Oid type = column_types[p_column];
	r_meta["pgsql:oid"] = type;

	const Oid table_oid(pq->ftable(res, p_column));
	r_meta["pgsql:table_oid"] = table_oid;

	CharString utf8_query(("SELECT RELNAME FROM PG_CLASS WHERE OID=" + itos(table_oid)).utf8());
	PGresult *temp_res(pq->exec(con.get_conn(), utf8_query.get_data()));
	if (pq->resultStatus(temp_res) == PGRES_TUPLES_OK && pq->getisnull(temp_res, 0, 0) == 0) {
		r_meta["table"] = String::utf8(pq->getvalue(temp_res, 0, 0));
	}
	pq->clear(temp_res);

	String native_type;
	switch (type) {
		case BOOLOID:
			native_type = "bool";
			break;
		case BYTEAOID:
			native_type = "bytea";
			break;
		case INT8OID:
			native_type = "int8";
			break;
		case INT2OID:
			native_type = "int2";
			break;
		case INT4OID:
			native_type = "int4";
			break;
		case FLOAT4OID:
			native_type = "float4";
			break;
		case FLOAT8OID:
			native_type = "float8";
			break;
		case TEXTOID:
			native_type = "text";
			break;
		case VARCHAROID:
			native_type = "varchar";
			break;
		case DATEOID:
			native_type = "date";
			break;
		case TIMESTAMPOID:
			native_type = "timestamp";
			break;
		case TIMESTAMPTZOID:
			native_type = "timestamptz";
			break;
		case UUIDOID:
			native_type = "uuid";
			break;
		default:
			// Fetch metadata from Postgres system catalogue
			utf8_query = ("SELECT TYPNAME FROM PG_TYPE WHERE OID=" + itos(type)).utf8();
			temp_res = pq->exec(con.get_conn(), utf8_query.get_data());
			if (pq->resultStatus(temp_res) == PGRES_TUPLES_OK && pq->ntuples(temp_res) == 1) {
				native_type = String::utf8(pq->getvalue(temp_res, 0, 0));
			}
			pq->clear(temp_res);
			break;
	}
	if (!native_type.is_empty()) {
		r_meta["native_type"] = native_type;
	}
	return true;
}

Variant PostgreSQLStatement::get_attribute(SQLStatement::Attribute p_attribute) {
	switch (p_attribute) {
		case SQLStatement::ATTR_RESULT_MEMORY_SIZE:
			if (!res) {
				push_error(SQL_ERROR(String(), String(), "Cannot query the result memory size because the statement is not executed yet."));
				return Variant();
			}
			return pq->resultMemorySize(res);
		default:
			return PostgreSQLStatement::get_attribute(p_attribute);
	}
}

SQLDriverStatement::PlaceholderType PostgreSQLStatement::get_placeholder_type() const {
	if (emulate) {
		return PLACEHOLDER_NONE;
	} else {
		return PLACEHOLDER_NAMED;
	}
}

String PostgreSQLStatement::get_named_rewrite_template() const {
	if (emulate) {
		return String();
	} else {
		return "$%d";
	}
}

SQLDriverStatement::TokenType PostgreSQLStatement::parse_query_token(const char32_t *&r_cur) const {
	// C-style escaped string
	if ((r_cur[0] == U'e' || r_cur[0] == U'E') && r_cur[1] == U'\'') {
		r_cur += 2;
		while (*r_cur) {
			if (*r_cur == U'\'') {
				if (r_cur[1] == U'\'') {
					r_cur += 2; // Escaped quote
					continue;
				}
				++r_cur;
				break;
			} else if (*r_cur == U'\\' && r_cur[1]) {
				r_cur += 2;
			} else {
				++r_cur;
			}
		}
		return TOKEN_TEXT;
	}

	// Double quoted string ("...")
	if (*r_cur == U'"') {
		++r_cur;
		while (*r_cur) {
			if (*r_cur == U'"') {
				if (r_cur[1] == U'"') {
					r_cur += 2; // Escaped quote
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
				if (r_cur[1] == U'\'') {
					r_cur += 2; // Escaped quote
					continue;
				}
				++r_cur;
				break;
			}
			++r_cur;
		}
		return TOKEN_TEXT;
	}

	// Custom dollar-quoted strings start and end with $tag$, where tag is an optional
	if (*r_cur == U'$') {
		const char32_t *tag_start = r_cur + 1;
		const char32_t *tag_end = tag_start;
		if (((*tag_end >= U'A' && *tag_end <= U'Z') || (*tag_end >= U'a' && *tag_end <= U'z') ||
					(*tag_end >= 0x80 && *tag_end <= 0xFF) || *tag_end == U'_')) {
			++tag_end;
			while ((*tag_end >= U'A' && *tag_end <= U'Z') || (*tag_end >= U'a' && *tag_end <= U'z') ||
					(*tag_end >= U'0' && *tag_end <= U'9') || (*tag_end >= 0x80 && *tag_end <= 0xFF) || *tag_end == U'_') {
				++tag_end;
			}
		}
		if (*tag_end == U'$') {
			// Found $tag$
			r_cur = tag_end + 1;
			const size_t tag_len = tag_end - tag_start;
			while (*r_cur) {
				if (*r_cur == U'$') {
					const char32_t *p = r_cur + 1;
					size_t i = 0;
					for (; i < tag_len && tag_start[i] == p[i]; ++i) {
					}
					if (i == tag_len && p[i] == U'$') {
						r_cur = p + 1;
						break;
					}
				}
				++r_cur;
			}
			return TOKEN_CUSTOM_QUOTE;
		}
	}

	// Multiple colons (::, :::, etc.)
	if (*r_cur == U':' && r_cur[1] == U':') {
		const char32_t *p = r_cur + 2;
		while (*p == U':') {
			++p;
		}
		r_cur = p;
		return TOKEN_TEXT;
	}

	// Escaped question mark (??)
	if (r_cur[0] == U'?' && r_cur[1] == U'?') {
		r_cur += 2;
		return TOKEN_ESCAPED_QUESTION;
	}

	// Named parameter binding (:name)
	if (*r_cur == U':') {
		const char32_t *p = r_cur + 1;
		if (((*p >= U'a' && *p <= U'z') || (*p >= U'A' && *p <= U'Z') || (*p >= U'0' && *p <= U'9') || *p == U'_')) {
			++p;
			while ((*p >= U'a' && *p <= U'z') || (*p >= U'A' && *p <= U'Z') ||
					(*p >= U'0' && *p <= U'9') || *p == U'_') {
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

	// Treat each as a single-character text token
	if (*r_cur == U'$' || *r_cur == U'e' || *r_cur == U'E' || *r_cur == U':' || *r_cur == U'?' ||
			*r_cur == U'"' || *r_cur == U'\'' || *r_cur == U'/' || *r_cur == U'-') {
		++r_cur;
		return TOKEN_TEXT;
	}

	// Comments: /* ... */ or -- ...
	if (*r_cur == U'/' && r_cur[1] == U'*') {
		r_cur += 2;
		while (*r_cur && !(r_cur[0] == U'*' && r_cur[1] == U'/')) {
			++r_cur;
		}
		if (*r_cur) {
			r_cur += 2;
		}
		return TOKEN_TEXT;
	}
	if (*r_cur == U'-' && r_cur[1] == U'-') {
		r_cur += 2;
		while (*r_cur && *r_cur != U'\n') {
			++r_cur;
		}
		return TOKEN_TEXT;
	}

	// Text until a special character is found
	const char32_t *p = r_cur;
	while (*p && *p != U'$' && *p != U'e' && *p != U'E' && *p != U':' && *p != U'?' && *p != U'"' && *p != U'\'' && *p != U'/' && *p != U'-') {
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

//
// PostgreSQLConnection implementation
//

PostgreSQLConnection::PostgreSQLConnection() {
}

PostgreSQLConnection::~PostgreSQLConnection() {
	if (conn) {
		pq->finish(conn);
	}
}

bool PostgreSQLConnection::set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) {
	switch (p_attribute) {
		case SQLStatement::ATTR_EMULATE_PREPARES:
			ERR_FAIL_COND_V(!Variant::can_convert(p_value.get_type(), Variant::BOOL), false);
			emulate_prepares = p_value;
			return true;
		case SQLStatement::ATTR_DISABLE_PREPARES:
			ERR_FAIL_COND_V(!Variant::can_convert(p_value.get_type(), Variant::BOOL), false);
			disable_prepares = p_value;
			return true;
		case SQLStatement::ATTR_CONNECT_TIMEOUT:
			return true;
		default:
			return SQLDriverConnection::set_attribute(p_attribute, p_value);
	}
}

Variant PostgreSQLConnection::get_attribute(SQLStatement::Attribute p_attribute) {
	switch (p_attribute) {
		case SQLStatement::ATTR_EMULATE_PREPARES:
			return emulate_prepares;
		case SQLStatement::ATTR_DISABLE_PREPARES:
			return disable_prepares;
		case SQLStatement::ATTR_CLIENT_VERSION: {
			const int version = pq->libVersion();
			const int major = version / 10000;
			if (major >= 10) {
				const int minor = version % 10000;
				return vformat("%d.%d", major, minor);
			} else {
				const int minor = version / 100 % 100;
				const int revision = version % 100;
				return vformat("%d.%d.%d", major, minor, revision);
			}
		}
		case SQLStatement::ATTR_SERVER_VERSION:
			return pq->parameterStatus(conn, "server_version");
		case SQLStatement::ATTR_CONNECTION_STATUS:
			switch (pq->status(conn)) {
				case CONNECTION_STARTED:
					return "Waiting for connection to be made.";
				case CONNECTION_MADE:
				case CONNECTION_OK:
					return "Connection OK; waiting to send.";
				case CONNECTION_AWAITING_RESPONSE:
					return "Waiting for a response from the server.";
				case CONNECTION_AUTH_OK:
					return "Received authentication; waiting for backend start-up to finish.";
				case CONNECTION_SETENV:
					return "Negotiating environment-driven parameter settings.";
#ifdef CONNECTION_SSL_STARTUP
				case CONNECTION_SSL_STARTUP:
					return "Negotiating SSL encryption.";
#endif // CONNECTION_SSL_STARTUP
#ifdef CONNECTION_CONSUME
				case CONNECTION_CONSUME:
					return "Flushing send queue/consuming extra data.";
#endif // CONNECTION_CONSUME
#ifdef CONNECTION_GSS_STARTUP
				case CONNECTION_GSS_STARTUP:
					return "Negotiating GSSAPI.";
#endif // CONNECTION_GSS_STARTUP
#ifdef CONNECTION_CHECK_TARGET
				case CONNECTION_CHECK_TARGET:
					return "Connection OK; checking target server properties.";
#endif // CONNECTION_CHECK_TARGET
#ifdef CONNECTION_CHECK_STANDBY
				case CONNECTION_CHECK_STANDBY:
					return "Connection OK; checking if server in standby.";
#endif // CONNECTION_CHECK_STANDBY
				case CONNECTION_BAD:
				default:
					return "Bad connection.";
			}
		case SQLStatement::ATTR_SERVER_INFO:
			return vformat("PID: %d; Client Encoding: %s; Is Superuser: %s; Session Authorization: %s; Date Style: %s",
					pq->backendPID(conn),
					pq->parameterStatus(conn, "client_encoding"),
					pq->parameterStatus(conn, "is_superuser"),
					pq->parameterStatus(conn, "session_authorization"),
					pq->parameterStatus(conn, "DateStyle"));
		default:
			return SQLDriverConnection::get_attribute(p_attribute);
	}
}

static void _print_notice(void *p_context, const char *p_message) {
	PostgreSQLConnection *const connection = (PostgreSQLConnection *)p_context;
	if (is_print_verbose_enabled()) {
		print_line("[PostgreSQL Notice] ", String::utf8(p_message));
	}
}

static String _escape_credentials(const String &p_credential) {
	return p_credential.replace("\\", "\\\\").replace("'", "\\'");
}

Error PostgreSQLConnection::open(const String &p_data_source_name, const String &p_username, const String &p_password,
		const HashMap<SQLStatement::Attribute, Variant> &p_options) {
	const Error library_error(load_libpq_functions());
	if (library_error != OK) {
		return library_error;
	}

	int64_t connect_timeout = SQLConnection::DEFAULT_CONNECT_TIMEOUT;
	if (p_options.has(SQLStatement::ATTR_CONNECT_TIMEOUT)) {
		ERR_FAIL_COND_V_MSG(!Variant::can_convert(p_options[SQLStatement::ATTR_CONNECT_TIMEOUT].get_type(), Variant::INT),
				ERR_INVALID_PARAMETER, "Connect timeout must be an integer.");
		connect_timeout = p_options[SQLStatement::ATTR_CONNECT_TIMEOUT];
		ERR_FAIL_COND_V(connect_timeout < 0, ERR_INVALID_PARAMETER);
	}

	String connection_string = p_data_source_name.replace_char(';', ' ');
	if (!p_data_source_name.contains("connect_timeout=")) {
		connection_string += vformat(" connect_timeout=%d", connect_timeout);
	}
	if (!p_username.is_empty() && !p_data_source_name.contains("user=")) {
		connection_string += vformat(" user='%s'", _escape_credentials(p_username));
	}
	if (!p_password.is_empty() && !p_data_source_name.contains("password=")) {
		connection_string += vformat(" password='%s'", _escape_credentials(p_password));
	}

	const CharString utf8_connection_string(connection_string.utf8());
	conn = pq->connectdb(utf8_connection_string.ptr());

	if (pq->status(conn) != CONNECTION_OK) {
		push_error(SQL_ERROR("08006", itos(PGRES_FATAL_ERROR), String::utf8(pq->errorMessage(conn))));
	}

	pq->setNoticeProcessor(conn, _print_notice, this);

	// Godot is using Unicode so force the client encoding to UTF8
	if (!_exec_command("SET NAMES 'UTF8'")) {
		return FAILED;
	}

	return OK;
}

bool PostgreSQLConnection::_exec_command(const String &p_statement) {
	const CharString utf8_statement(p_statement.utf8());
	PGresult *const res(pq->exec(conn, utf8_statement.ptr()));

	const ExecStatusType status(pq->resultStatus(res));
	const bool ret(status == PGRES_COMMAND_OK);
	if (!ret) {
		push_error(PGSQL_RES_ERROR(res, status));
	}

	pq->clear(res);
	return ret;
}

int64_t PostgreSQLConnection::exec(const String &p_statement) {
	const CharString utf8_statement(p_statement.utf8());
	PGresult *const res(pq->exec(conn, utf8_statement.ptr()));

	int64_t ret;
	const ExecStatusType status(pq->resultStatus(res));
	if (status == PGRES_COMMAND_OK) {
		ret = String::utf8(pq->cmdTuples(res)).to_int();
	} else if (status == PGRES_TUPLES_OK) {
		ret = 0;
	} else {
		ret = -1;
		push_error(PGSQL_RES_ERROR(res, status));
	}

	pq->clear(res);
	return ret;
}

bool PostgreSQLConnection::begin_transaction() {
	return _exec_command("BEGIN");
}

bool PostgreSQLConnection::commit() {
	return _exec_command("COMMIT");
}

bool PostgreSQLConnection::rollback() {
	return _exec_command("ROLLBACK");
}

bool PostgreSQLConnection::in_transaction() const {
	return pq->transactionStatus(conn) > PQTRANS_IDLE;
}

Variant PostgreSQLConnection::get_last_insert_id(const String &p_name) {
	PGresult *res;
	if (p_name.is_empty()) {
		res = pq->exec(conn, "SELECT LASTVAL()");
	} else {
		const CharString utf8_name(p_name.utf8());
		const char *const params_ptr[1]{ utf8_name.ptr() };
		res = pq->execParams(conn, "SELECT CURRVAL($1)", 1, nullptr, params_ptr, nullptr, nullptr, 0);
	}

	Variant ret;
	const ExecStatusType status(pq->resultStatus(res));
	if (pq->resultStatus(res) == PGRES_TUPLES_OK) {
		ret = _get_value(res, pq->ftype(res, 0), 0, 0);
	} else {
		push_error(PGSQL_RES_ERROR(res, status));
	}

	pq->clear(res);
	return ret;
}

String PostgreSQLConnection::quote(const Variant &p_value) {
	if (p_value.get_type() == Variant::PACKED_BYTE_ARRAY) {
		const PackedByteArray byte_array = p_value;

		size_t length;
		unsigned char *const data = pq->escapeByteaConn(conn, byte_array.ptr(), (size_t)byte_array.size(), &length);
		if (!data) {
			push_error(PGSQL_CONN_ERROR(conn));
			return String();
		}

		String string = "'";
		if (length > 1) {
			string.append_ascii(Span<char>((const char *)data, length - 1));
		}
		string += '\'';
		pq->freemem(data);
		return string;
	} else {
		const CharString string = p_value.stringify().utf8();

		LocalVector<char> escaped;
		escaped.resize_uninitialized(string.length() * 2 + 3);

		int err(0);
		const size_t len = pq->escapeStringConn(conn, escaped.ptr() + 1, string.ptr(), string.length(), &err);
		if (err != 0) {
			push_error(PGSQL_CONN_ERROR(conn));
			return String();
		}

		escaped[0] = '\'';
		escaped[len + 1] = '\'';
		return String::utf8(escaped.ptr(), (int)len + 2);
	}
}

PostgreSQLStatement *PostgreSQLConnection::create_statement() {
	return memnew(PostgreSQLStatement(*this));
}

//
// PostgreSQLDriver implementation
//

PostgreSQLDriver::PostgreSQLDriver() :
		SQLDriver(FEAT_QUOTE | FEAT_TRANSACTIONS | FEAT_IN_TRANSACTION | FEAT_LAST_INSERT_ID | FEAT_CLOSE_CURSOR) {
}

PostgreSQLDriver::~PostgreSQLDriver() {
	unload_libpq_functions();
}

String PostgreSQLDriver::get_name() const {
	return "pgsql";
}

SQLDriverConnection *PostgreSQLDriver::create_connection() {
	return memnew(PostgreSQLConnection);
}
