#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fcc {

// FCC filing/docket types
enum class FilingType : uint8_t {
    ECFS_COMMENT = 0,       // Electronic Comment Filing System
    ECFS_REPLY,             // Reply comment
    ECFS_EX_PARTE,          // Ex parte notice
    SATELLITE_LICENSE,      // Part 25 satellite license
    EARTH_STATION,          // Earth station authorization
    EXPERIMENTAL_LICENSE,   // Part 5 experimental authorization
    EQUIPMENT_CERT,         // Equipment authorization/certification
    SPECTRUM_ALLOCATION,    // Spectrum allocation/reallocation
    ULS_LICENSE,            // Universal Licensing System record
    AUCTION_RESULT,         // Spectrum auction result
    NPRM,                   // Notice of Proposed Rulemaking
    ORDER,                  // FCC Order
    PUBLIC_NOTICE,          // Public notice
    REPORT,                 // FCC Report
    UNKNOWN
};

// Satellite-specific filing subtypes
enum class SatelliteFilingType : uint8_t {
    NGSO_CONSTELLATION,     // Non-geostationary constellation (Starlink, Kuiper, etc.)
    GSO_APPLICATION,        // Geostationary satellite application
    MODIFICATION,           // License modification
    SPECIAL_TEMPORARY,      // Special Temporary Authority
    MARKET_ACCESS,          // Market access petition (foreign operators)
    EARTH_STATION_BLANKET,  // Blanket earth station license
    SPACE_STATION,          // Space station application
    SPECTRUM_SHARING,       // Spectrum sharing arrangement
    UNKNOWN
};

// Frequency band classification
enum class SpectrumBand : uint8_t {
    VLF = 0,    // 3-30 kHz
    LF,         // 30-300 kHz
    MF,         // 300-3000 kHz
    HF,         // 3-30 MHz
    VHF,        // 30-300 MHz
    UHF,        // 300-3000 MHz
    SHF,        // 3-30 GHz (most satcom)
    EHF,        // 30-300 GHz
    UNKNOWN
};

// Core filing record
struct Filing {
    std::string filing_id;          // FCC-assigned ID
    std::string docket_number;      // e.g., "22-271"
    std::string proceeding_name;    // Human-readable proceeding title
    FilingType type;
    std::string filer_name;         // Entity filing
    std::string filer_type;         // "individual", "organization", "government"
    std::string date_filed;         // ISO 8601
    std::string date_received;      // ISO 8601
    std::string description;        // Filing description/summary
    std::string bureau;             // FCC bureau (Space, Wireless, Media, etc.)
    std::vector<std::string> document_urls;  // Links to filed documents
    bool is_satellite_related;
    bool is_spectrum_related;
};

// Satellite license record
struct SatelliteLicense {
    std::string call_sign;          // e.g., "S3060"
    std::string licensee;           // Operating entity
    std::string satellite_name;     // Satellite/constellation name
    SatelliteFilingType subtype;
    std::string orbit_type;         // "NGSO", "GSO"
    double orbital_altitude_km;     // For NGSO
    double orbital_longitude_deg;   // For GSO
    double inclination_deg;
    uint32_t num_satellites;        // Constellation size
    std::vector<std::string> frequency_bands;  // e.g., "Ku", "Ka", "V"
    double freq_lower_mhz;
    double freq_upper_mhz;
    std::string status;             // "pending", "granted", "denied", "dismissed"
    std::string grant_date;
    std::string expiration_date;
    std::string filing_id;
};

// Spectrum allocation record
struct SpectrumAllocation {
    double freq_lower_mhz;
    double freq_upper_mhz;
    SpectrumBand band;
    std::string allocation_type;    // "primary", "secondary", "shared"
    std::string service;            // "fixed-satellite", "mobile", "radiolocation", etc.
    std::string region;             // ITU Region 1/2/3 or "US"
    std::string status;             // "current", "proposed", "auction"
    std::string docket_number;
    std::string effective_date;
    std::string notes;
};

// ULS (Universal Licensing System) record
struct ULSRecord {
    std::string call_sign;
    std::string licensee;
    std::string service_code;       // e.g., "NN" (Part 25 space station)
    std::string status;
    double freq_lower_mhz;
    double freq_upper_mhz;
    double power_watts;
    double latitude;
    double longitude;
    std::string grant_date;
    std::string expiration_date;
    std::string market;
};

// Experimental license (Part 5)
struct ExperimentalLicense {
    std::string call_sign;
    std::string licensee;
    std::string experiment_type;    // "conventional", "program", "medical", "compliance"
    std::string purpose;
    double freq_lower_mhz;
    double freq_upper_mhz;
    double power_watts;
    double latitude;
    double longitude;
    double radius_km;               // Operating radius
    std::string grant_date;
    std::string expiration_date;
    std::string status;
    bool is_space_related;
};

// Equipment authorization
struct EquipmentAuth {
    std::string fcc_id;             // FCC ID (grantee + product code)
    std::string grantee_name;
    std::string product_description;
    std::string equipment_class;    // "DSS" (spread spectrum), "DTS" (transmitter), etc.
    double freq_lower_mhz;
    double freq_upper_mhz;
    double power_output_watts;
    std::string grant_date;
    std::string test_firm;
    bool is_sdr;                    // Software Defined Radio
    bool is_satellite_terminal;
};

// Alert for significant filing events
struct FilingAlert {
    std::string alert_id;
    std::string severity;           // "flash", "priority", "routine"
    std::string category;           // "new_constellation", "spectrum_reallocation", "interference", etc.
    std::string title;
    std::string summary;
    std::string filing_id;
    std::string docket_number;
    std::string timestamp;
    std::vector<std::string> affected_bands;
    std::vector<std::string> affected_services;
};

// API query parameters
struct ECFSQuery {
    std::string docket_number;
    std::string filer_name;
    std::string date_from;          // ISO 8601
    std::string date_to;            // ISO 8601
    std::string bureau;
    FilingType type;
    bool satellite_only;
    bool spectrum_only;
    uint32_t limit;
    uint32_t offset;
};

}  // namespace fcc
