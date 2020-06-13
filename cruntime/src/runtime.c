/*---------------------------------------------------------------------------
  Copyright 2020 Daan Leijen, Microsoft Corporation.

  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
---------------------------------------------------------------------------*/
#include "runtime.h"
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#endif

// ptr_null
static block_t ptr_null_block = { HEADER_STATIC(0,TAG_INVALID) };
ptr_t ptr_null = (ptr_t)(&ptr_null_block);

// identity function
static box_t _function_id(function_t self, box_t x, context_t* ctx) {
  drop_function_t(self,ctx);
  return x;
}
define_static_function(function_id, _function_id)

// null function
static box_t _function_null(function_t self, context_t* ctx) {
  drop_function_t(self, ctx);
  fatal_error(EFAULT, "null function is called");
  return box_null;
}
define_static_function(function_null, _function_null)

// empty vector
static struct vector_small_s _vector_empty
= { {{ HEADER_STATIC(0,TAG_VECTOR_SMALL) }}, { 0x02 } /* length = box_enum(0) */, {{0}} };
vector_t vector_empty = &_vector_empty._type;

// null function
void free_fun_null(void* p) {
  UNUSED(p);
}


string_t runtime_host(context_t* ctx) {
  UNUSED(ctx);
  define_string_literal(static, host, 5, "libc");
  return dup_string_t(host);
}

/*--------------------------------------------------------------------------------------------------
  Errors 
--------------------------------------------------------------------------------------------------*/
static void _strlcpy(char* dest, const char* src, size_t dest_size) {
  dest[0] = 0;
#pragma warning(suppress:4996)
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = 0;
}
/*
static void _strlcat(char* dest, const char* src, size_t dest_size) {
#pragma warning(suppress:4996)
  strncat(dest, src, dest_size - 1);
  dest[dest_size - 1] = 0;
}
*/

typedef enum log_level_e {
  LOG_FATAL,
  LOG_ERROR,
  LOG_WARNING,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE
} log_level_t;

static void log_message(log_level_t level, const char* msg, context_t* ctx) {
  UNUSED(ctx); UNUSED(level);
  fputs(msg,stderr); // TODO: use ctx->log
}

static void log_message_fmt(context_t* ctx, log_level_t level, const char* fmt, va_list args) {
  char buf[512];
  if (fmt==NULL) return;
  size_t prefix_len = 0;
  const char* prefix = NULL;
  if (level==LOG_FATAL) prefix = "fatal: ";
  else if (level==LOG_ERROR) prefix = "error: ";
  else if (level==LOG_WARNING) prefix = "warning: ";
  else if (level==LOG_INFO) prefix = "info: ";
  else if (level==LOG_DEBUG) prefix = "debug: ";
  else if (level==LOG_TRACE) prefix = "trace: ";
  if (prefix!=NULL) {
    prefix_len = strlen(prefix);
    _strlcpy(buf, prefix, sizeof(buf));
  }
  vsnprintf(buf + prefix_len, sizeof(buf) - 1 - prefix_len, fmt, args);
  log_message(level,buf,ctx);
}

void fatal_error(int err, const char* fmt, ...) {
  UNUSED(err);
  va_list args;
  va_start(args, fmt);
  log_message_fmt(runtime_context(), LOG_FATAL, fmt, args);
  va_end(args);
  abort();   // todo: call error handler
}

void warning_message(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message_fmt(runtime_context(), LOG_WARNING, fmt, args);
  va_end(args);
}

/*--------------------------------------------------------------------------------------------------
  Process init/done
--------------------------------------------------------------------------------------------------*/

static bool process_initialized; // = false

static void runtime_done(void) {
  if (!process_initialized) return;
  process_initialized = false;
}

static void runtime_init(void) {
  if (process_initialized) return;
  process_initialized = true;
#if defined(_WIN32) && defined(_CONSOLE)
  SetConsoleOutputCP(65001); // set the console to unicode instead of OEM page
#endif
  atexit(&runtime_done);
}

/*--------------------------------------------------------------------------------------------------
  Getting the initial context (per thread)
--------------------------------------------------------------------------------------------------*/

static decl_thread context_t* context;

context_t* runtime_context(void) {
  context_t* ctx = context;
  if (ctx!=NULL) return ctx;
  runtime_init();
  ctx = (context_t*)calloc(sizeof(context_t),1);
  ctx->evv = dup_vector_t(vector_empty);
  ctx->thread_id = (uintptr_t)(&context);
  ctx->unique = integer_one;
  return ctx;
}


/*--------------------------------------------------------------------------------------------------
  Platform specific initialization hooks
--------------------------------------------------------------------------------------------------*/

#if defined(__cplusplus)  // also used for _MSC_VER
// C++: use static initialization to detect process start
static bool process_init(void) {
  runtime_init();
  return true;
}
static bool _process_init = process_init();

#elif defined(__GNUC__) || defined(__clang__)
// GCC,Clang: use the constructor attribute
static void __attribute__((constructor)) process_init(void) {
  runtime_init();
}

#else
#pragma message("define a way to call runtime_init on your platform")
#endif
