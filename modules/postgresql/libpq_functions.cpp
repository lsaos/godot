/**************************************************************************/
/*  libpq_functions.cpp                                                   */
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

#include "libpq_functions.h"
#include "core/os/os.h"

#ifndef LIBPQ_LINK_STATIC

#include "core/io/file_access.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif // WINDOWS_ENABLED

static String _find_libpq_library(OS::GDExtensionData &r_ext_data) {
#ifdef WINDOWS_ENABLED
	// Allow user to bring its own DLL in priority
	String libpq_path = OS::get_singleton()->get_executable_path().get_base_dir().path_join("libpq.dll");
	if (FileAccess::exists(libpq_path)) {
		return libpq_path;
	}

	// Fallback to default directory
	libpq_path = "libpq.dll";

	// Try to find PostgreSQL install path in the registry
	const wchar_t *const sub_key = L"SOFTWARE\\PostgreSQL\\Installations";
	HKEY key;
	LONG result(RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub_key, 0, KEY_READ, &key));
	if (result == ERROR_SUCCESS) {
		wchar_t instance_key_name[256]{};
		DWORD instance_key_name_len = 256;
		result = RegEnumKeyExW(key, 0, instance_key_name, &instance_key_name_len, nullptr, nullptr, nullptr, nullptr);
		if (result == ERROR_SUCCESS) {
			const Char16String instance_sub_key = (String(sub_key) + "\\" + String(instance_key_name)).utf16();
			HKEY instance_key;
			result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, (const wchar_t *)instance_sub_key.ptr(), 0, KEY_READ, &instance_key);
			if (result == ERROR_SUCCESS) {
				wchar_t base_dir[512]{};
				DWORD base_dir_len = sizeof(base_dir);
				result = RegQueryValueExW(instance_key, L"Base Directory", nullptr, nullptr, (LPBYTE)base_dir, &base_dir_len);
				if (result == ERROR_SUCCESS) {
					libpq_path = String(base_dir) + "\\bin\\libpq.dll";
					r_ext_data.also_set_library_path = true; // This is required to correctly load dependencies
				}
				RegCloseKey(instance_key);
			}
		}
		RegCloseKey(key);
	}

	return libpq_path;
#elif defined(APPLE_ENABLED)
	// TODO: find a better way to locate the library on macOS
	return "libpq.dylib";
#elif defined(UNIX_ENABLED)
	// TODO: find a better way to locate the library on Linux/Unix
	return "libpq.so";
#else // Other platforms
	ERR_FAIL_V_MSG(String(), "Unsupported platform for libpq dynamic library loading.");
#endif // Other platforms
}

#endif // LIBPQ_LINK_STATIC

LibPQFunctions *pq = nullptr;

static BinaryMutex pq_mutex;

Error load_libpq_functions() {
	MutexLock lock(pq_mutex);
	if (pq) {
		return pq->load_error;
	}

	pq = memnew(LibPQFunctions);

#ifdef LIBPQ_LINK_STATIC

	pq->load_error = OK;

#define LOAD_PQ_FUNCTION(func_name) pq->func_name = PQ##func_name

#else // LIBPQ_LINK_STATIC

	OS::GDExtensionData gdext_data;
	const String library_path = _find_libpq_library(gdext_data);

	pq->load_error = OS::get_singleton()->open_dynamic_library(library_path, pq->library_handle, &gdext_data);
	ERR_FAIL_COND_V_MSG(pq->load_error != OK, pq->load_error, vformat("Can't load the libpq library '%s'.", library_path));

#define LOAD_PQ_FUNCTION(func_name)                                                                                                              \
	pq->load_error = OS::get_singleton()->get_dynamic_library_symbol_handle(pq->library_handle, "PQ" _MKSTR(func_name), (void *&)pq->func_name); \
	if (pq->load_error != OK) {                                                                                                                  \
		OS::get_singleton()->close_dynamic_library(pq->library_handle);                                                                          \
		pq->library_handle = nullptr;                                                                                                            \
		return pq->load_error;                                                                                                                   \
	}

#endif // LIBPQ_LINK_STATIC

	LOAD_PQ_FUNCTION(connectdb);
	LOAD_PQ_FUNCTION(finish);
	LOAD_PQ_FUNCTION(exec);
	LOAD_PQ_FUNCTION(resultStatus);
	LOAD_PQ_FUNCTION(cmdTuples);
	LOAD_PQ_FUNCTION(clear);
	LOAD_PQ_FUNCTION(status);
	LOAD_PQ_FUNCTION(transactionStatus);
	LOAD_PQ_FUNCTION(ntuples);
	LOAD_PQ_FUNCTION(nfields);
	LOAD_PQ_FUNCTION(fname);
	LOAD_PQ_FUNCTION(ftable);
	LOAD_PQ_FUNCTION(ftype);
	LOAD_PQ_FUNCTION(fsize);
	LOAD_PQ_FUNCTION(fmod);
	LOAD_PQ_FUNCTION(getisnull);
	LOAD_PQ_FUNCTION(getvalue);
	LOAD_PQ_FUNCTION(getlength);
	LOAD_PQ_FUNCTION(execParams);
	LOAD_PQ_FUNCTION(errorMessage);
	LOAD_PQ_FUNCTION(escapeStringConn);
	LOAD_PQ_FUNCTION(resultErrorField);
	LOAD_PQ_FUNCTION(resultErrorMessage);
	LOAD_PQ_FUNCTION(resultMemorySize);
	LOAD_PQ_FUNCTION(libVersion);
	LOAD_PQ_FUNCTION(parameterStatus);
	LOAD_PQ_FUNCTION(backendPID);
	LOAD_PQ_FUNCTION(prepare);
	LOAD_PQ_FUNCTION(execPrepared);
	LOAD_PQ_FUNCTION(sendQuery);
	LOAD_PQ_FUNCTION(sendQueryParams);
	LOAD_PQ_FUNCTION(sendQueryPrepared);
	LOAD_PQ_FUNCTION(setSingleRowMode);
	LOAD_PQ_FUNCTION(getResult);
	LOAD_PQ_FUNCTION(getCancel);
	LOAD_PQ_FUNCTION(freeCancel);
	LOAD_PQ_FUNCTION(cancel);
	LOAD_PQ_FUNCTION(setNoticeProcessor);
	LOAD_PQ_FUNCTION(fformat);
	LOAD_PQ_FUNCTION(unescapeBytea);
	LOAD_PQ_FUNCTION(freemem);
	LOAD_PQ_FUNCTION(escapeByteaConn);
#ifdef LIBPQ_HAS_CLOSE_PREPARED
	LOAD_PQ_FUNCTION(closePrepared);
#else // LIBPQ_HAS_CLOSE_PREPARED
	pq->closePrepared = nullptr;
#endif // LIBPQ_HAS_CLOSE_PREPARED

#undef LOAD_PQ_FUNCTION

	return pq->load_error;
}

void unload_libpq_functions() {
	MutexLock lock(pq_mutex);
	if (!pq) {
		return;
	}

#ifndef LIBPQ_LINK_STATIC
	if (pq->library_handle) {
		OS::get_singleton()->close_dynamic_library(pq->library_handle);
	}
#endif // LIBPQ_LINK_STATIC

	memdelete(pq);
	pq = nullptr;
}
