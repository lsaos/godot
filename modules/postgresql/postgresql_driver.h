/**************************************************************************/
/*  postgresql_driver.h                                                   */
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

#pragma once

#include "core/io/sql_connection.h"
#include "libpq_functions.h"

class PostgreSQLConnection;

class PostgreSQLStatement : public SQLDriverStatement {
public:
	PostgreSQLStatement(PostgreSQLConnection &p_con);
	~PostgreSQLStatement() override;
	bool execute(const String &p_statement, const HashMap<Variant, SQLStatement::Parameter> &p_parameters) override;
	bool fetch(SQLStatement::FetchOrientation p_orientation, int64_t p_offset) override;
	bool get_value(int p_column, Variant &o_value) override;
	bool describe_columns(LocalVector<SQLStatement::Column> &o_columns) override;
	bool handle_parameter_event(ParameterEvent p_event, const HashMap<Variant, String> &p_parameters_map, SQLStatement::Parameter &r_parameter) override;
	bool prepare(const HashMap<SQLStatement::Attribute, Variant> &p_options) override;
	int64_t get_row_count() const override;
	bool get_column_meta(int p_column, Dictionary &r_meta) const override;
	Variant get_attribute(SQLStatement::Attribute p_attribute) override;
	TokenType parse_query_token(const char32_t *&r_cur) const override;
	PlaceholderType get_placeholder_type() const override;
	String get_named_rewrite_template() const override;

private:
	LocalVector<Oid> column_types;
	LocalVector<PackedByteArray> parameter_values;
	LocalVector<const char *> parameter_pointers;
	LocalVector<int> parameter_lengths;
	LocalVector<int> parameter_formats;
	CharString name;
	String cursor_name;
	PostgreSQLConnection &con;
	int row_count = 0;
	int current_row = 0;
	PGresult *res = nullptr;
	bool emulate = false;
	bool is_prepared = false;
};

class PostgreSQLConnection : public SQLDriverConnection {
public:
	PostgreSQLConnection();
	~PostgreSQLConnection() override;
	bool set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value) override;
	Variant get_attribute(SQLStatement::Attribute p_attribute) override;
	Error open(const String &p_data_source_name, const String &p_username, const String &p_password,
			const HashMap<SQLStatement::Attribute, Variant> &p_options) override;
	int64_t exec(const String &p_statement) override;
	bool begin_transaction() override;
	bool commit() override;
	bool rollback() override;
	Variant get_last_insert_id(const String &p_name) override;
	String quote(const Variant &p_value) override;
	PostgreSQLStatement *create_statement() override;
	bool in_transaction() const override;

	_FORCE_INLINE_ PGconn *get_conn() const { return conn; }
	_FORCE_INLINE_ bool has_emulate_prepares() const { return emulate_prepares; }
	_FORCE_INLINE_ bool has_disable_prepares() const { return disable_prepares; }
	_FORCE_INLINE_ int increment_statement_counter() { return ++statement_counter; }

private:
	bool _exec_command(const String &p_statement);

private:
	PGconn *conn = nullptr;
	bool emulate_prepares = false;
	bool disable_prepares = false;
	int statement_counter = 0;
};

class PostgreSQLDriver : public SQLDriver {
	GDSOFTCLASS(PostgreSQLDriver, SQLDriver);

public:
	PostgreSQLDriver();
	~PostgreSQLDriver() override;
	String get_name() const override;
	SQLDriverConnection *create_connection() override;
};
