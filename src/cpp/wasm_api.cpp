#include "fcc/types.h"
#include "fcc/fcc_client.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
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

    // Parse JSON from FCC APIs into a compact binary representation.
    // Input: raw JSON from ECFS, ULS, ELS, or Equipment APIs
    // Output: Binary record(s) — header + array of fixed-size filing records
    //
    // Record format (per filing):
    //   [type:1B][id_len:2B][id:varB][docket_len:2B][docket:varB]
    //   [date_len:2B][date:varB][flags:1B]
    //
    // The FCCClient JSON parsers are used internally; this function
    // bridges raw API responses to the binary wire format.
    // Reference: FCC Public API docs — https://www.fcc.gov/developers

    std::string json(reinterpret_cast<const char*>(input_ptr), input_len);

    fcc::FCCClient client;

    // Detect which API response this is based on content
    // and parse appropriately
    std::vector<fcc::Filing> filings;

    if (json.find("\"filings\"") != std::string::npos ||
        json.find("\"id_filing\"") != std::string::npos) {
        // ECFS response — parse as filing
        fcc::ECFSQuery q;
        // Re-use the internal query_filings path by parsing the response directly
        // For a single record:
        fcc::Filing f = client.parse_ecfs_filing(json);
        if (!f.filing_id.empty()) filings.push_back(f);
    }

    // Serialize filings to binary output
    size_t offset = 0;
    auto write_u8 = [&](uint8_t v) {
        if (offset < input_len) output_ptr[offset] = v;
        offset++;
    };
    auto write_u16 = [&](uint16_t v) {
        if (offset + 1 < input_len) {
            output_ptr[offset] = static_cast<uint8_t>(v & 0xFF);
            output_ptr[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        }
        offset += 2;
    };
    auto write_str = [&](const std::string& s) {
        uint16_t len = static_cast<uint16_t>(std::min(s.size(), (size_t)65535));
        write_u16(len);
        if (offset + len <= input_len) {
            std::memcpy(output_ptr + offset, s.c_str(), len);
        }
        offset += len;
    };

    // Header: magic "FCC\0" + count
    if (offset + 8 <= input_len) {
        output_ptr[0] = 'F'; output_ptr[1] = 'C';
        output_ptr[2] = 'C'; output_ptr[3] = 0;
    }
    offset = 4;
    uint32_t count = static_cast<uint32_t>(filings.size());
    if (offset + 4 <= input_len) std::memcpy(output_ptr + offset, &count, 4);
    offset += 4;

    for (const auto& f : filings) {
        write_u8(static_cast<uint8_t>(f.type));
        write_str(f.filing_id);
        write_str(f.docket_number);
        write_str(f.date_filed);
        uint8_t flags = 0;
        if (f.is_satellite_related) flags |= 0x01;
        if (f.is_spectrum_related) flags |= 0x02;
        write_u8(flags);
    }

    *output_len = offset;
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

    // Convert parsed binary records (from parse()) to SDN wire format.
    // SDN wire format is a simple envelope: [magic:4B][version:4B][payload]
    // The payload is the binary record data from parse() verbatim.
    //
    // This two-stage pipeline (parse → convert) allows the host to inspect
    // parsed records before committing to the wire format.

    constexpr uint32_t SDN_MAGIC   = 0x53444E46;  // "SDNF"
    constexpr uint32_t SDN_VERSION = 1;
    constexpr size_t HEADER_SIZE = 8;

    size_t total = HEADER_SIZE + input_len;
    if (output_ptr && total <= input_len + HEADER_SIZE) {
        // Write SDN envelope header
        std::memcpy(output_ptr, &SDN_MAGIC, 4);
        std::memcpy(output_ptr + 4, &SDN_VERSION, 4);
        // Copy payload
        std::memcpy(output_ptr + HEADER_SIZE, input_ptr, input_len);
    }

    *output_len = total;
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

    // Serialize filings to JSON array for JS consumption
    // (binary format via parse()+convert() is also available)
    std::string json = "[";
    for (size_t i = 0; i < filings.size(); i++) {
        if (i > 0) json += ",";
        const auto& f = filings[i];
        json += "{\"id\":\"" + f.filing_id + "\","
                "\"docket\":\"" + f.docket_number + "\","
                "\"filer\":\"" + f.filer_name + "\","
                "\"date\":\"" + f.date_filed + "\","
                "\"bureau\":\"" + f.bureau + "\","
                "\"satellite\":" + (f.is_satellite_related ? "true" : "false") + ","
                "\"spectrum\":" + (f.is_spectrum_related ? "true" : "false") + "}";
    }
    json += "]";

    size_t len = std::min(json.size(), (size_t)(output_ptr ? 65536 : 0));
    if (output_ptr && len > 0) {
        std::memcpy(output_ptr, json.c_str(), len);
    }
    *output_len = json.size();
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

    // Serialize alerts to JSON array for JS consumption
    std::string json = "[";
    for (size_t i = 0; i < alerts.size(); i++) {
        if (i > 0) json += ",";
        const auto& a = alerts[i];
        json += "{\"id\":\"" + a.filing_id + "\","
                "\"severity\":\"" + a.severity + "\","
                "\"category\":\"" + a.category + "\","
                "\"title\":\"" + a.title + "\","
                "\"docket\":\"" + a.docket_number + "\","
                "\"timestamp\":\"" + a.timestamp + "\"}";
    }
    json += "]";

    size_t len = std::min(json.size(), (size_t)(output_ptr ? 65536 : 0));
    if (output_ptr && len > 0) {
        std::memcpy(output_ptr, json.c_str(), len);
    }
    *output_len = json.size();
    return static_cast<int>(alerts.size());
}

}  // extern "C"
