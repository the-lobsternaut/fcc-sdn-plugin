#include "fcc/types.h"
#include "fcc/fcc_client.h"
#include <cstdlib>
#include <cstring>
#include <emscripten/bind.h>

using namespace emscripten;

// C ABI exports for SDN plugin system
extern "C" {

void* wasm_malloc(size_t size) {
    return malloc(size);
}

void wasm_free(void* ptr) {
    free(ptr);
}

/**
 * parse() — Parse raw FCC API JSON response into internal representation
 *
 * @param input_ptr  Pointer to JSON input buffer
 * @param input_len  Length of input buffer
 * @param output_ptr Pointer to output buffer (caller-allocated)
 * @param output_len Pointer to receive output length
 * @return 0 on success, non-zero on error
 */
int parse(const uint8_t* input_ptr, size_t input_len,
          uint8_t* output_ptr, size_t* output_len) {
    if (!input_ptr || input_len == 0) return -1;

    // TODO: Parse JSON from FCC APIs into FlatBuffers
    // Input: raw JSON from ECFS, ULS, ELS, Equipment APIs
    // Output: FlatBuffers-encoded filing records

    *output_len = 0;
    return 0;
}

/**
 * convert() — Convert parsed filing data to FlatBuffers wire format
 *
 * @param input_ptr  Pointer to parsed data
 * @param input_len  Length of parsed data
 * @param output_ptr Pointer to output buffer
 * @param output_len Pointer to receive output length
 * @return 0 on success, non-zero on error
 */
int convert(const uint8_t* input_ptr, size_t input_len,
            uint8_t* output_ptr, size_t* output_len) {
    if (!input_ptr || input_len == 0) return -1;

    // TODO: Convert internal representation to SDN wire format
    *output_len = 0;
    return 0;
}

/**
 * poll_filings() — Poll FCC for new filings since last check
 *
 * @param api_key_ptr  Pointer to API key string (can be null for limited access)
 * @param api_key_len  Length of API key
 * @param hours        Number of hours to look back
 * @param output_ptr   Pointer to output buffer
 * @param output_len   Pointer to receive output length
 * @return Number of new filings found, or negative on error
 */
int poll_filings(const char* api_key_ptr, size_t api_key_len,
                 uint32_t hours,
                 uint8_t* output_ptr, size_t* output_len) {
    fcc::FCCClient client;
    if (api_key_ptr && api_key_len > 0) {
        client.set_api_key(std::string(api_key_ptr, api_key_len));
    }

    auto filings = client.get_recent_filings(hours, 100);
    // TODO: Serialize filings to FlatBuffers output
    *output_len = 0;
    return static_cast<int>(filings.size());
}

/**
 * check_alerts() — Check for significant filing events
 *
 * @param api_key_ptr  Pointer to API key string
 * @param api_key_len  Length of API key
 * @param hours        Number of hours to look back
 * @param output_ptr   Pointer to output buffer
 * @param output_len   Pointer to receive output length
 * @return Number of alerts, or negative on error
 */
int check_alerts(const char* api_key_ptr, size_t api_key_len,
                 uint32_t hours,
                 uint8_t* output_ptr, size_t* output_len) {
    fcc::FCCClient client;
    if (api_key_ptr && api_key_len > 0) {
        client.set_api_key(std::string(api_key_ptr, api_key_len));
    }

    auto alerts = client.check_for_alerts(hours);
    // TODO: Serialize alerts to FlatBuffers output
    *output_len = 0;
    return static_cast<int>(alerts.size());
}

}  // extern "C"
