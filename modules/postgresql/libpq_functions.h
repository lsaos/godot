/**************************************************************************/
/*  libpq_functions.h                                                     */
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

#pragma once

#include "core/error/error_list.h"
#include "thirdparty/libpq/libpq-fe.h"

// Workaround for postgres defining their OIDs in a private header file
constexpr Oid BOOLOID(16);
constexpr Oid INT8OID(20);
constexpr Oid INT2OID(21);
constexpr Oid INT4OID(23);
constexpr Oid NUMERICOID(1700);
constexpr Oid FLOAT4OID(700);
constexpr Oid FLOAT8OID(701);
constexpr Oid TIMESTAMPOID(1114);
constexpr Oid TIMESTAMPTZOID(1184);
constexpr Oid BYTEAOID(17);
constexpr Oid UUIDOID(2950);
constexpr Oid TEXTOID(25);
constexpr Oid VARCHAROID(1043);
constexpr Oid DATEOID(1082);
constexpr Oid OIDOID(26);

struct LibPQFunctions {
	Error load_error;
	void *library_handle;
	PGconn *(*connectdb)(const char *conninfo);
	void (*finish)(PGconn *conn);
	PGresult *(*exec)(PGconn *conn, const char *query);
	ExecStatusType (*resultStatus)(const PGresult *res);
	char *(*cmdTuples)(PGresult *res);
	void (*clear)(PGresult *res);
	ConnStatusType (*status)(const PGconn *conn);
	PGTransactionStatusType (*transactionStatus)(const PGconn *conn);
	int (*ntuples)(const PGresult *res);
	int (*nfields)(const PGresult *res);
	char *(*fname)(const PGresult *res, int field_num);
	Oid (*ftable)(const PGresult *res, int field_num);
	Oid (*ftype)(const PGresult *res, int field_num);
	int (*fsize)(const PGresult *res, int field_num);
	int (*fmod)(const PGresult *res, int field_num);
	int (*getisnull)(const PGresult *res, int tup_num, int field_num);
	char *(*getvalue)(const PGresult *res, int tup_num, int field_num);
	int (*getlength)(const PGresult *res, int tup_num, int field_num);
	PGresult *(*execParams)(PGconn *conn, const char *command, int nParams, const Oid *paramTypes,
			const char *const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat);
	char *(*errorMessage)(const PGconn *conn);
	size_t (*escapeStringConn)(PGconn *conn, char *to, const char *from, size_t length, int *error);
	char *(*resultErrorField)(const PGresult *res, int fieldcode);
	char *(*resultErrorMessage)(const PGresult *res);
	size_t (*resultMemorySize)(const PGresult *res);
	int (*libVersion)();
	const char *(*parameterStatus)(const PGconn *conn, const char *paramName);
	int (*backendPID)(const PGconn *conn);
	PGresult *(*prepare)(PGconn *conn, const char *stmtName, const char *query, int nParams, const Oid *paramTypes);
	PGresult *(*execPrepared)(PGconn *conn, const char *stmtName, int nParams, const char *const *paramValues,
			const int *paramLengths, const int *paramFormats, int resultFormat);
	PGresult *(*closePrepared)(PGconn *conn, const char *stmt);
	int (*sendQuery)(PGconn *conn, const char *query);
	int (*sendQueryParams)(PGconn *conn, const char *command, int nParams, const Oid *paramTypes,
			const char *const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat);
	int (*sendQueryPrepared)(PGconn *conn, const char *stmtName, int nParams, const char *const *paramValues,
			const int *paramLengths, const int *paramFormats, int resultFormat);
	int (*setSingleRowMode)(PGconn *conn);
	PGresult *(*getResult)(PGconn *conn);
	PGcancel *(*getCancel)(PGconn *conn);
	void (*freeCancel)(PGcancel *cancel);
	int (*cancel)(PGcancel *cancel, char *errbuf, int errbufsize);
	PQnoticeProcessor (*setNoticeProcessor)(PGconn *conn, PQnoticeProcessor proc, void *arg);
};

extern LibPQFunctions *pq;

Error load_libpq_functions();
void unload_libpq_functions();
