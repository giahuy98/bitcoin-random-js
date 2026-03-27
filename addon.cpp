// Copyright (c) 2026 The bitcoin-random contributors
// Distributed under the MIT software license.

#include "random.h"

#include <node_api.h>

#include <cstdint>
#include <limits>
#include <span>

namespace {

#define NAPI_CALL_RETURN_NULL(env, call)                                                \
    do {                                                                                \
        const napi_status status = (call);                                              \
        if (status != napi_ok) {                                                        \
            const napi_extended_error_info* error_info = nullptr;                       \
            napi_get_last_error_info((env), &error_info);                               \
            const char* message = (error_info != nullptr && error_info->error_message)  \
                ? error_info->error_message                                              \
                : "N-API call failed";                                                  \
            napi_throw_error((env), nullptr, message);                                  \
            return nullptr;                                                             \
        }                                                                               \
    } while (0)

#define NAPI_CALL_RETURN_FALSE(env, call)                                               \
    do {                                                                                \
        const napi_status status = (call);                                              \
        if (status != napi_ok) {                                                        \
            const napi_extended_error_info* error_info = nullptr;                       \
            napi_get_last_error_info((env), &error_info);                               \
            const char* message = (error_info != nullptr && error_info->error_message)  \
                ? error_info->error_message                                              \
                : "N-API call failed";                                                  \
            napi_throw_error((env), nullptr, message);                                  \
            return false;                                                               \
        }                                                                               \
    } while (0)

bool GetLengthArg(napi_env env, napi_callback_info info, size_t& length)
{
    size_t argc = 1;
    napi_value args[1];
    NAPI_CALL_RETURN_FALSE(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
    if (argc != 1) {
        napi_throw_type_error(env, nullptr, "Expected one length argument");
        return false;
    }

    int64_t signed_length = 0;
    const napi_status status = napi_get_value_int64(env, args[0], &signed_length);
    if (status != napi_ok) {
        napi_throw_type_error(env, nullptr, "Length must be an integer");
        return false;
    }
    if (signed_length < 0) {
        napi_throw_range_error(env, nullptr, "Length must be non-negative");
        return false;
    }

    const uint64_t unsigned_length = static_cast<uint64_t>(signed_length);
    if (unsigned_length > std::numeric_limits<size_t>::max()) {
        napi_throw_range_error(env, nullptr, "Length is too large");
        return false;
    }

    length = static_cast<size_t>(unsigned_length);
    return true;
}

bool GetUint32Arg(napi_env env, napi_callback_info info, uint32_t& value)
{
    size_t argc = 1;
    napi_value args[1];
    NAPI_CALL_RETURN_FALSE(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
    if (argc != 1) {
        napi_throw_type_error(env, nullptr, "Expected one uint32 argument");
        return false;
    }

    const napi_status status = napi_get_value_uint32(env, args[0], &value);
    if (status != napi_ok) {
        napi_throw_type_error(env, nullptr, "Argument must be a uint32");
        return false;
    }
    return true;
}

napi_value CreateBufferWithRandomBytes(napi_env env, size_t length, bool strong)
{
    napi_value buffer;
    void* data = nullptr;
    NAPI_CALL_RETURN_NULL(env, napi_create_buffer(env, length, &data, &buffer));

    if (length != 0) {
        auto span = std::span<unsigned char>(reinterpret_cast<unsigned char*>(data), length);
        if (strong) {
            bitcoin_random::GetStrongRandBytes(span);
        } else {
            bitcoin_random::GetRandBytes(span);
        }
    }

    return buffer;
}

napi_value GetRandBytesWrapped(napi_env env, napi_callback_info info)
{
    size_t length = 0;
    if (!GetLengthArg(env, info, length)) return nullptr;
    return CreateBufferWithRandomBytes(env, length, false);
}

napi_value GetStrongRandBytesWrapped(napi_env env, napi_callback_info info)
{
    size_t length = 0;
    if (!GetLengthArg(env, info, length)) return nullptr;
    return CreateBufferWithRandomBytes(env, length, true);
}

napi_value GetRandHashWrapped(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    NAPI_CALL_RETURN_NULL(env, napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr));
    if (argc != 0) {
        napi_throw_type_error(env, nullptr, "getRandHash does not take arguments");
        return nullptr;
    }
    return CreateBufferWithRandomBytes(env, 32, false);
}

napi_value RandomInitWrapped(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    NAPI_CALL_RETURN_NULL(env, napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr));
    if (argc != 0) {
        napi_throw_type_error(env, nullptr, "randomInit does not take arguments");
        return nullptr;
    }

    bitcoin_random::RandomInit();
    napi_value result;
    NAPI_CALL_RETURN_NULL(env, napi_get_undefined(env, &result));
    return result;
}

napi_value RandAddPeriodicWrapped(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    NAPI_CALL_RETURN_NULL(env, napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr));
    if (argc != 0) {
        napi_throw_type_error(env, nullptr, "randAddPeriodic does not take arguments");
        return nullptr;
    }

    bitcoin_random::RandAddPeriodic();
    napi_value result;
    NAPI_CALL_RETURN_NULL(env, napi_get_undefined(env, &result));
    return result;
}

napi_value RandAddEventWrapped(napi_env env, napi_callback_info info)
{
    uint32_t event_info = 0;
    if (!GetUint32Arg(env, info, event_info)) return nullptr;

    bitcoin_random::RandAddEvent(event_info);
    napi_value result;
    NAPI_CALL_RETURN_NULL(env, napi_get_undefined(env, &result));
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    const napi_property_descriptor properties[] = {
        {"getRandBytes", nullptr, GetRandBytesWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getStrongRandBytes", nullptr, GetStrongRandBytesWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getRandHash", nullptr, GetRandHashWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"randomInit", nullptr, RandomInitWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"randAddPeriodic", nullptr, RandAddPeriodicWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"randAddEvent", nullptr, RandAddEventWrapped, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    NAPI_CALL_RETURN_NULL(env, napi_define_properties(env, exports, std::size(properties), properties));
    return exports;
}

} // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
