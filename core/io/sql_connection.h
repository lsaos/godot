/**************************************************************************/
/*  sql_connection.h                                                      */
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

#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

//
// TODO:
// -Better error messages
//

class SQLConnection;
class SQLDriverStatement;
class SQLDriverConnection;

// SQL error state
struct SQLError {
	const char *function = nullptr;
	const char *file = nullptr;
	int line = 0;
	String standard_code;
	String driver_code;
	String message;

	SQLError();
	SQLError(const char *p_function, const char *p_file, int p_line, const String &p_standard_code, const String &p_driver_code, const String &p_message);
	void clear();
	Array to_array() const;
	String get_error_code() const;
};

// Helper macro to create an SQLError with function, file and line info
#define SQL_ERROR(standard_code, driver_code, message) SQLError(FUNCTION_STR, __FILE__, __LINE__, standard_code, driver_code, message)

// SQL driver interface
class SQLDriver : public RefCounted {
	GDSOFTCLASS(SQLDriver, RefCounted);

public:
	virtual String get_name() const = 0;
	virtual SQLDriverConnection *create_connection() = 0;

	enum Feature {
		FEAT_QUOTE = 1 << 0, // Support quoting strings
		FEAT_TRANSACTIONS = 1 << 1, // Support transactions
		FEAT_IN_TRANSACTION = 1 << 2, // Support in_transaction method
		FEAT_LAST_INSERT_ID = 1 << 3, // Support last insert ID retrieval
		FEAT_NEXT_ROWSET = 1 << 4, // Support next rowset on statements
		FEAT_CLOSE_CURSOR = 1 << 5, // Support close cursor on statements
	};
	_FORCE_INLINE_ bool has_features(int p_features) const { return (features & p_features) == p_features; }

	static void add_driver(Ref<SQLDriver> p_driver);
	static void remove_driver(Ref<SQLDriver> p_driver);
	static Ref<SQLDriver> get_driver_by_name(const String &p_name);
	static PackedStringArray get_driver_names();

protected:
	SQLDriver(int p_features);

private:
	const int features;
	static Vector<Ref<SQLDriver>> drivers;
};

// Prepared SQL statement
class SQLStatement : public RefCounted {
	GDCLASS(SQLStatement, RefCounted);

public:
	enum Attribute {
		ATTR_ERRMODE, // Error reporting mode (ErrorMode)
		ATTR_DEFAULT_FETCH_MODE, // Default fetch mode for statements (FetchMode)
		ATTR_EMULATE_PREPARES, // Emulate prepared statements (bool)
		ATTR_DISABLE_PREPARES, // Disable prepared statements (bool)
		ATTR_CURSOR, // Selects the cursor type (CursorType)
		ATTR_FETCH_NULLS, // Conversion of NULLs and strings (NullMode)
		ATTR_COLUMNS_CASE, // Force column names to a specific case (CaseMode)
		ATTR_CONNECT_TIMEOUT, // Connect timeout in seconds (int)

		ATTR_DRIVER_NAME, // Driver name (String, read-only)
		ATTR_CLIENT_VERSION, // Client library version (String, read-only)
		ATTR_SERVER_VERSION, // Server version (String, read-only)
		ATTR_CONNECTION_STATUS, // Connection status (String, read-only)
		ATTR_SERVER_INFO, // Server info (String, read-only)
		ATTR_RESULT_MEMORY_SIZE, // Result memory size (int, read-only)
	};
	enum FetchMode {
		// Fetch modes
		FETCH_DEFAULT, // Use connection default or FETCH_BOTH if not set
		FETCH_BOTH, // Dictionary with both column names and indexes
		FETCH_ASSOC, // Dictionary with column names
		FETCH_NAMED, // Same as FETCH_ASSOC but keeps same-named columns
		FETCH_ARRAY, // Indexed array
		FETCH_KEY_PAIR, // Key-pair dictionary (result must have exactly two columns)
		FETCH_CLASS, // Fetch into class (param is the class name)
		FETCH_INTO, // Fetch into existing object (param is the object)
		FETCH_COLUMN, // Fetch a single column (param is the column index, defaults to first column)
		FETCH_CALL, // Fetch into callable parameters (param is the callable)

		// Additional flags
		FETCH_GROUP = 1 << 5, // Group by first column (only with fetch_all)
		FETCH_UNIQUE = 1 << 6, // Unique by first column (only with fetch_all)
		FETCH_CLASSNAME = 1 << 7, // Use first column as class name (only with FETCH_CLASS)
		FETCH_FLAGS = 0xfffffff0
	};
	enum ErrorMode {
		ERRMODE_SILENT, // Only set error codes
		ERRMODE_WARNING, // Write a warning
		ERRMODE_ERROR, // Write an error
		ERRMODE_MAX
	};
	enum CursorType {
		CURSOR_FWDONLY, // Forward only
		CURSOR_SCROLL, // Scrollable cursor
		CURSOR_MAX
	};
	enum FetchOrientation {
		FETCH_ORI_NEXT, // Fetch the next row in the result set
		FETCH_ORI_PRIOR, // Fetch the previous row in the result set
		FETCH_ORI_FIRST, // Fetch the first row in the result set
		FETCH_ORI_LAST, // Fetch the last row in the result set
		FETCH_ORI_ABS, // Fetch the requested row by row number from the result set
		FETCH_ORI_REL, // Fetch the requested row by relative position from the current position of the cursor in the result set
	};
	enum NullMode {
		NULL_NATURAL, // Leave NULLs and strings as-is
		NULL_EMPTY_STRING, // Convert empty strings to NULLs
		NULL_TO_STRING, // Convert NULLs to empty strings
		NULL_MAX
	};
	enum CaseMode {
		CASE_NATURAL, // Leave column names as-is
		CASE_LOWER, // Force column names to lower case
		CASE_UPPER, // Force column names to upper case
		CASE_MAX
	};

	SQLStatement();
	~SQLStatement() override;

	bool bind_value(const Variant &p_param, const Variant &p_value);
	bool execute(const Variant &p_parameters = Variant());
	bool set_fetch_mode(int p_mode, const Variant &p_param = Variant());
	bool set_attribute(Attribute p_attribute, const Variant &p_value);
	Variant get_attribute(Attribute p_attribute);
	bool close_cursor();
	bool next_rowset();
	Variant fetch_all(int p_mode = FETCH_DEFAULT, const Variant &p_param = Variant());
	Variant fetch(int p_mode = FETCH_DEFAULT, FetchOrientation p_orientation = FETCH_ORI_NEXT, int64_t p_offset = 0);
	Variant fetch_column(int p_column = 0);
	Object *fetch_object(const StringName &p_class_name);

	int64_t get_row_count() const;
	int get_column_count() const;
	String get_column_name(int p_column) const;
	int get_column_length(int p_column) const;
	int get_column_precision(int p_column) const;
	Variant::Type get_column_type(int p_column) const;
	Dictionary get_column_meta(int p_column) const;
	String get_error_code() const;
	Array get_error_info() const;
	String get_query_string() const;
	int get_fetch_mode() const;
	Variant get_fetch_param() const;
	void debug_dump_params() const;

	struct Parameter {
		Variant value;
		String name;
		int index = -1;
	};
	struct Column {
		String name;
		StringName string_name;
		int length = 0;
		int precision = 0;
		Variant::Type type = Variant::NIL;
	};

protected:
	static void _bind_methods();

private:
	void _push_error(const SQLError &p_error);
	bool _parse_parameters();
	bool _rewrite_name_to_position(Parameter &r_parameter);
	bool _register_parameter(Parameter &r_parameter);
	bool _fetch_next_row(FetchOrientation p_orientation, int64_t p_offset);
	Variant _fetch_row(int p_mode, const Variant &p_param, int p_start_column, int p_end_column);
	Variant _fetch_value(int p_column);
	Object *_fetch_object(const StringName &p_class_name, int p_start_column, int p_end_column);
	bool _get_fetch_mode(int &r_mode, Variant &r_param, bool p_fetch_all);
	bool _execute();

private:
	SQLError error;
	HashMap<Variant, Parameter> parameters;
	HashMap<Variant, String> parameters_map;
	Variant fetch_param;
	LocalVector<Column> columns;
	Ref<SQLConnection> connection;
	String query_string;
	String active_query_string;
	SQLDriverStatement *impl = nullptr;
	int fetch_mode = FETCH_DEFAULT;
	bool executed = false;

	friend class SQLConnection;
	friend class SQLDriverStatement;
};

VARIANT_ENUM_CAST(SQLStatement::Attribute);
VARIANT_ENUM_CAST(SQLStatement::ErrorMode);
VARIANT_ENUM_CAST(SQLStatement::FetchMode);
VARIANT_ENUM_CAST(SQLStatement::CursorType);
VARIANT_ENUM_CAST(SQLStatement::FetchOrientation);
VARIANT_ENUM_CAST(SQLStatement::NullMode);
VARIANT_ENUM_CAST(SQLStatement::CaseMode);

// Connection to an SQL database
class SQLConnection : public RefCounted {
	GDCLASS(SQLConnection, RefCounted);

public:
	static constexpr int64_t DEFAULT_CONNECT_TIMEOUT = 30;

	SQLConnection();
	~SQLConnection() override;

	Error open(const String &p_data_source_name, const String &p_username = String(),
			const String &p_password = String(), const Dictionary &p_options = Dictionary());
	void close();
	int64_t exec(const String &p_statement);
	Ref<SQLStatement> query(const String &p_query, int p_fetch_mode = SQLStatement::FETCH_DEFAULT, const Variant &p_fetch_param = Variant());
	Ref<SQLStatement> prepare(const String &p_query, const Dictionary &p_options = Dictionary());
	bool begin_transaction();
	bool commit();
	bool rollback();
	Variant get_last_insert_id(const String &p_name = String());
	String quote(const Variant &p_value);
	bool set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value);
	Variant get_attribute(SQLStatement::Attribute p_attribute);

	bool is_open() const;
	bool in_transaction() const;
	String get_error_code() const;
	Array get_error_info() const;

	static Ref<SQLConnection> open_connection(const String &p_data_source_name, const String &p_username = String(),
			const String &p_password = String(), const Dictionary &p_options = Dictionary());
	static PackedStringArray get_available_drivers();

protected:
	static void _bind_methods();

private:
	void _push_error(const SQLError &p_error);
	Ref<SQLStatement> _prepare(const String &p_query, const HashMap<SQLStatement::Attribute, Variant> &p_options);
	bool _set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value);
	void _handle_error(const SQLError &p_error) const;

private:
	SQLError error;
	Ref<SQLDriver> driver;
	SQLDriverConnection *impl = nullptr;
	int default_fetch_mode = SQLStatement::FETCH_BOTH;
	SQLStatement::ErrorMode error_mode = SQLStatement::ERRMODE_ERROR;
	SQLStatement::NullMode fetch_nulls_mode = SQLStatement::NULL_NATURAL;
	SQLStatement::CaseMode columns_case_mode = SQLStatement::CASE_NATURAL;
	int statement_count = 0;
	bool is_in_transaction = false;

	friend class SQLStatement;
	friend class SQLDriverConnection;
};

// Driver-specific SQL statement implementation
class SQLDriverStatement {
public:
	enum PlaceholderType {
		PLACEHOLDER_NONE, // No placeholder support
		PLACEHOLDER_NAMED, // :name style placeholders
		PLACEHOLDER_POSITIONAL // ? style placeholders
	};
	enum ParameterEvent {
		PARAMETER_EVENT_NORMALIZE, // Normalize parameter before binding
		PARAMETER_EVENT_ADD, // Parameter is being added
		PARAMETER_EVENT_PRE_EXEC, // Before execution of the statement
	};
	enum TokenType {
		TOKEN_TEXT, // Text to be used as-is
		TOKEN_NAMED_PARAMETER, // :name style parameter
		TOKEN_POSITIONAL_PARAMETER, // ? style parameter
		TOKEN_ESCAPED_QUESTION, // Escaped question mark
		TOKEN_CUSTOM_QUOTE // Custom quote sequence
	};

	virtual ~SQLDriverStatement();
	virtual bool execute(const String &p_statement, const HashMap<Variant, SQLStatement::Parameter> &p_parameters) = 0;
	virtual bool fetch(SQLStatement::FetchOrientation p_orientation, int64_t p_offset) = 0;
	virtual bool get_value(int p_column, Variant &o_value) = 0;
	virtual bool describe_columns(LocalVector<SQLStatement::Column> &o_columns) = 0;
	virtual bool set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value);
	virtual Variant get_attribute(SQLStatement::Attribute p_attribute);
	virtual bool prepare(const HashMap<SQLStatement::Attribute, Variant> &p_options);
	virtual bool handle_parameter_event(ParameterEvent p_event, const HashMap<Variant, String> &p_parameters_map, SQLStatement::Parameter &r_parameter);
	virtual bool close_cursor();
	virtual bool do_next_rowset();
	virtual int64_t get_row_count() const;
	virtual bool get_column_meta(int p_column, Dictionary &r_meta) const;
	virtual TokenType parse_query_token(const char32_t *&r_cur) const;
	virtual PlaceholderType get_placeholder_type() const;
	virtual String get_named_rewrite_template() const;

protected:
	SQLDriverStatement();
	void push_error(const SQLError &p_error);

private:
	SQLStatement *statement = nullptr;

	friend class SQLConnection;
};

// Driver-specific SQL connection implementation
class SQLDriverConnection {
public:
	virtual ~SQLDriverConnection();
	virtual Error open(const String &p_data_source_name, const String &p_username, const String &p_password,
			const HashMap<SQLStatement::Attribute, Variant> &p_options) = 0;
	virtual int64_t exec(const String &p_statement) = 0;
	virtual SQLDriverStatement *create_statement() = 0;
	virtual bool set_attribute(SQLStatement::Attribute p_attribute, const Variant &p_value);
	virtual Variant get_attribute(SQLStatement::Attribute p_attribute);
	virtual bool begin_transaction();
	virtual bool commit();
	virtual bool rollback();
	virtual Variant get_last_insert_id(const String &p_name);
	virtual String quote(const Variant &p_value);
	virtual bool in_transaction() const;

protected:
	SQLDriverConnection();
	void push_error(const SQLError &p_error);

private:
	SQLConnection *connection = nullptr;

	friend class SQLConnection;
};
